// Host audio out (WASAPI shared) + MIDI in (winmm). See host_io_win.h.
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <avrt.h>
#include <cstring>
#include <cstdlib>
#include <atomic>
#include <cstdio>
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "avrt.lib")

#include "host_io_win.h"

#ifndef AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM
#define AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM 0x80000000
#endif
#ifndef AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY
#define AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY 0x08000000
#endif

namespace MS2000 {

namespace {
struct ComScope {
    bool ok = false;
    ComScope() { const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED); ok = SUCCEEDED(hr); }
    ~ComScope() { if (ok) CoUninitialize(); }
};
template<class T> void rel(T*& p) { if (p) { p->Release(); p = nullptr; } }
std::string utf8(const wchar_t* w)
{
    if (!w) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n - 1 : 0, '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}
std::string friendlyName(IMMDevice* dev)
{
    IPropertyStore* ps = nullptr; std::string name = "?";
    if (SUCCEEDED(dev->OpenPropertyStore(STGM_READ, &ps))) {
        PROPVARIANT v; PropVariantInit(&v);
        if (SUCCEEDED(ps->GetValue(PKEY_Device_FriendlyName, &v)) && v.vt == VT_LPWSTR) name = utf8(v.pwszVal);
        PropVariantClear(&v); ps->Release();
    }
    return name;
}
} // namespace

static std::vector<HostDevice> listFlow(EDataFlow flow)
{
    ComScope com;
    std::vector<HostDevice> out;
    IMMDeviceEnumerator* en = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&en)))
        return out;
    IMMDevice* def = nullptr;
    if (SUCCEEDED(en->GetDefaultAudioEndpoint(flow, eConsole, &def))) {
        out.push_back({ L"", "System default (" + friendlyName(def) + ")" });
        rel(def);
    }
    IMMDeviceCollection* col = nullptr;
    if (SUCCEEDED(en->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &col))) {
        UINT n = 0; col->GetCount(&n);
        for (UINT i = 0; i < n; ++i) {
            IMMDevice* d = nullptr;
            if (FAILED(col->Item(i, &d))) continue;
            LPWSTR id = nullptr;
            if (SUCCEEDED(d->GetId(&id))) { out.push_back({ id, friendlyName(d) }); CoTaskMemFree(id); }
            rel(d);
        }
        rel(col);
    }
    rel(en);
    return out;
}

std::vector<HostDevice> HostAudioOut::list() { return listFlow(eRender); }
std::vector<HostDevice> HostAudioIn::list()  { return listFlow(eCapture); }

// ---------------------------------------------------------------- AUDIO IN (AUDIO-IN round, 2026-09-26)
// WASAPI capture, shared mode, asked for 48 kHz stereo float with Windows converting from the
// device's own format. Packets go to the sink as they arrive; silent packets as zeros.
bool HostAudioIn::open(const std::wstring& deviceId, Sink sink)
{
    close();
    m_sink = std::move(sink);
    m_stop = false; m_frames = 0;
    { std::lock_guard<std::mutex> l(m_errMx); m_err.clear(); }
    m_dead = false;
    m_thread = std::thread([this, deviceId] {
        run(deviceId);
        if (!m_stop) { m_dead = true; printf("[HOST-AUDIO] input stream ended: %s\n", lastError().c_str()); fflush(stdout); }
    });
    return true;
}

void HostAudioIn::close()
{
    m_stop = true;
    if (m_thread.joinable()) m_thread.join();
}

void HostAudioIn::run(std::wstring id)
{
    ComScope com;
    IMMDeviceEnumerator* en = nullptr; IMMDevice* dev = nullptr; IAudioClient* ac = nullptr;
    IAudioCaptureClient* cc = nullptr; HANDLE ev = nullptr; HANDLE mmcss = nullptr;
    auto fail = [&](const char* e) { std::lock_guard<std::mutex> l(m_errMx); m_err = e; };
    auto cleanup = [&] {
        if (ac) ac->Stop();
        rel(cc); rel(ac); rel(dev); rel(en);
        if (ev) CloseHandle(ev);
        if (mmcss) AvRevertMmThreadCharacteristics(mmcss);
    };
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&en))) { fail("no MMDeviceEnumerator"); cleanup(); return; }
    const HRESULT hd = id.empty() ? en->GetDefaultAudioEndpoint(eCapture, eConsole, &dev) : en->GetDevice(id.c_str(), &dev);
    if (FAILED(hd)) { fail("device not found"); cleanup(); return; }
    if (FAILED(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&ac))) { fail("IAudioClient activate failed"); cleanup(); return; }
    WAVEFORMATEXTENSIBLE wf{};
    wf.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    wf.Format.nChannels = 2;
    wf.Format.nSamplesPerSec = 48000;
    wf.Format.wBitsPerSample = 32;
    wf.Format.nBlockAlign = 8;
    wf.Format.nAvgBytesPerSec = 48000 * 8;
    wf.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    wf.Samples.wValidBitsPerSample = 32;
    wf.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
    wf.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
    const DWORD flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
    const HRESULT hr = ac->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, 200000 /* 20 ms */, 0, &wf.Format, nullptr);
    if (FAILED(hr)) { char b[80]; snprintf(b, sizeof(b), "IAudioClient::Initialize 0x%08lX", (unsigned long)hr); fail(b); cleanup(); return; }
    ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    ac->SetEventHandle(ev);
    if (FAILED(ac->GetService(__uuidof(IAudioCaptureClient), (void**)&cc))) { fail("no IAudioCaptureClient"); cleanup(); return; }
    DWORD task = 0; mmcss = AvSetMmThreadCharacteristicsW(L"Pro Audio", &task);
    if (FAILED(ac->Start())) { fail("IAudioClient::Start failed"); cleanup(); return; }
    std::vector<float> zeros;
    auto failHr = [&](const char* what, HRESULT h) { char b[96]; snprintf(b, sizeof(b), "%s 0x%08lX", what, (unsigned long)h); fail(b); };
    int idle = 0; bool dead = false;
    while (!m_stop && !dead) {
        if (WaitForSingleObject(ev, 200) != WAIT_OBJECT_0) {
            // No buffer event: a reset or unplugged device stops signalling without saying so.
            UINT32 np = 0; const HRESULT h = cc->GetNextPacketSize(&np);
            if (FAILED(h)) { failHr("capture GetNextPacketSize", h); break; }
            if (++idle >= 10) { fail("capture: no buffer event for 2 s"); break; }
            continue;
        }
        idle = 0;
        for (;;) {
            BYTE* p = nullptr; UINT32 n = 0; DWORD fl = 0;
            const HRESULT g = cc->GetBuffer(&p, &n, &fl, nullptr, nullptr);
            if (FAILED(g)) { failHr("capture GetBuffer", g); dead = true; break; }
            if (g == AUDCLNT_S_BUFFER_EMPTY) break;
            if (!n) { cc->ReleaseBuffer(0); break; }
            const float* lr = reinterpret_cast<const float*>(p);
            if (fl & AUDCLNT_BUFFERFLAGS_SILENT) { zeros.assign(size_t(n) * 2, 0.0f); lr = zeros.data(); }
            if (m_sink) m_sink(lr, n);
            m_frames += n;
            cc->ReleaseBuffer(n);
        }
    }
    cleanup();
}

bool HostAudioOut::open(const std::wstring& deviceId, Pull pull, uint32_t bufferMs)
{
    close();
    m_pull = std::move(pull);
    m_bufferMs = bufferMs;
    m_stop = false; m_underrun = 0; m_devRate = 0;
    fail("");
    m_dead = false;
    // HOST-AUDIO-STALL: whatever ends the stream while nobody asked it to stop marks it dead, so the
    // GUI can stop pacing on it and reopen it (the device can be reset or lose its clock under us).
    m_thread = std::thread([this, deviceId] {
        run(deviceId);
        if (!m_stop) { m_dead = true; printf("[HOST-AUDIO] output stream ended: %s\n", lastError().c_str()); fflush(stdout); }
    });
    return true;
}

void HostAudioOut::close()
{
    m_stop = true;
    if (m_thread.joinable()) m_thread.join();
}

void HostAudioOut::run(std::wstring id)
{
    ComScope com;
    IMMDeviceEnumerator* en = nullptr; IMMDevice* dev = nullptr; IAudioClient* ac = nullptr;
    IAudioRenderClient* rc = nullptr; HANDLE ev = nullptr; HANDLE mmcss = nullptr;
    auto cleanup = [&] {
        if (ac) ac->Stop();
        rel(rc); rel(ac); rel(dev); rel(en);
        if (ev) CloseHandle(ev);
        if (mmcss) AvRevertMmThreadCharacteristics(mmcss);
    };
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&en))) { fail("no MMDeviceEnumerator"); cleanup(); return; }
    const HRESULT hd = id.empty() ? en->GetDefaultAudioEndpoint(eRender, eConsole, &dev) : en->GetDevice(id.c_str(), &dev);
    if (FAILED(hd)) { fail("device not found"); cleanup(); return; }
    if (FAILED(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&ac))) { fail("IAudioClient activate failed"); cleanup(); return; }

    WAVEFORMATEX* mix = nullptr;
    if (SUCCEEDED(ac->GetMixFormat(&mix))) { m_devRate = mix->nSamplesPerSec; CoTaskMemFree(mix); }

    WAVEFORMATEXTENSIBLE wf{};
    wf.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    wf.Format.nChannels = 2;
    wf.Format.nSamplesPerSec = 48000;
    wf.Format.wBitsPerSample = 32;
    wf.Format.nBlockAlign = 8;
    wf.Format.nAvgBytesPerSec = 48000 * 8;
    wf.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    wf.Samples.wValidBitsPerSample = 32;
    wf.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
    wf.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;

    const REFERENCE_TIME dur = REFERENCE_TIME(m_bufferMs) * 10000;
    const DWORD flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
    HRESULT hr = ac->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, dur, 0, &wf.Format, nullptr);
    if (FAILED(hr)) { char b[80]; snprintf(b, sizeof(b), "IAudioClient::Initialize 0x%08lX", (unsigned long)hr); fail(b); cleanup(); return; }
    ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    ac->SetEventHandle(ev);
    UINT32 bufFrames = 0; ac->GetBufferSize(&bufFrames);
    if (FAILED(ac->GetService(__uuidof(IAudioRenderClient), (void**)&rc))) { fail("no IAudioRenderClient"); cleanup(); return; }
    { BYTE* p = nullptr; if (SUCCEEDED(rc->GetBuffer(bufFrames, &p))) rc->ReleaseBuffer(bufFrames, AUDCLNT_BUFFERFLAGS_SILENT); }
    DWORD task = 0; mmcss = AvSetMmThreadCharacteristicsW(L"Pro Audio", &task);
    if (FAILED(ac->Start())) { fail("IAudioClient::Start failed"); cleanup(); return; }

    auto failHr = [&](const char* what, HRESULT h) { char b[96]; snprintf(b, sizeof(b), "%s 0x%08lX", what, (unsigned long)h); fail(b); };
    int idle = 0;
    // MS2K_TEST_AUDIOSTALL=<s> (test input, default off): after s seconds the FIRST output stream stops
    // seeing buffer events, as a reset device does - to exercise the stall detection and the GUI's reopen.
    static const double stallAt = [] { const char* e = getenv("MS2K_TEST_AUDIOSTALL"); return e ? atof(e) : 0.0; }();
    static std::atomic<bool> stallDone{false};
    const ULONGLONG t0 = GetTickCount64();
    while (!m_stop) {
        const bool fake = stallAt > 0 && !stallDone && double(GetTickCount64() - t0) / 1000.0 >= stallAt;
        if (fake) Sleep(200);
        if (fake || WaitForSingleObject(ev, 200) != WAIT_OBJECT_0) {
            if (fake && idle == 9) stallDone = true;
            // No buffer event: a reset device (driver resync, clock change) can stop signalling
            // without an error. Before HOST-AUDIO-STALL this loop waited forever and, with the
            // emulator paced on this stream, the whole machine froze (Tamas, 2026-09-27).
            UINT32 pd = 0; const HRESULT h = ac->GetCurrentPadding(&pd);
            if (FAILED(h)) { failHr("render GetCurrentPadding", h); break; }
            if (++idle >= 10) { fail("render: no buffer event for 2 s"); break; }
            continue;
        }
        idle = 0;
        UINT32 pad = 0; { const HRESULT h = ac->GetCurrentPadding(&pad); if (FAILED(h)) { failHr("render GetCurrentPadding", h); break; } }
        const UINT32 avail = bufFrames - pad;
        if (!avail) continue;
        BYTE* p = nullptr;
        { const HRESULT h = rc->GetBuffer(avail, &p); if (FAILED(h)) { failHr("render GetBuffer", h); break; } }
        float* lr = reinterpret_cast<float*>(p);
        const uint32_t got = m_pull ? m_pull(lr, avail) : 0;
        if (got < avail) {
            std::memset(lr + 2 * got, 0, size_t(avail - got) * 2 * sizeof(float));
            m_underrun += avail - got;
        }
        rc->ReleaseBuffer(avail, 0);
    }
    cleanup();
}

// ---------------------------------------------------------------- MIDI OUT
std::vector<std::string> HostMidiOut::list()
{
    std::vector<std::string> out;
    const UINT n = midiOutGetNumDevs();
    for (UINT i = 0; i < n; ++i) {
        MIDIOUTCAPSW c{};
        if (midiOutGetDevCapsW(i, &c, sizeof(c)) == MMSYSERR_NOERROR) out.push_back(utf8(c.szPname));
        else out.push_back("?");
    }
    return out;
}

bool HostMidiOut::open(unsigned index)
{
    close();
    std::lock_guard<std::mutex> l(m_mx);
    HMIDIOUT h = nullptr;
    if (midiOutOpen(&h, index, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) return false;
    m_handle = h; m_status = 0; m_have = m_need = 0; m_inSysex = false; m_sysex.clear();
    return true;
}

void HostMidiOut::close()
{
    std::lock_guard<std::mutex> l(m_mx);
    if (!m_handle) return;
    HMIDIOUT h = static_cast<HMIDIOUT>(m_handle);
    midiOutReset(h);
    for (int i = 0; i < 100 && !m_pending.empty(); ++i) { reapDone(); if (!m_pending.empty()) Sleep(5); }
    midiOutClose(h);
    m_handle = nullptr;
}

void HostMidiOut::reapDone()
{
    HMIDIOUT h = static_cast<HMIDIOUT>(m_handle);
    for (size_t i = 0; i < m_pending.size();) {
        auto* hdr = static_cast<MIDIHDR*>(m_pending[i]);
        if (hdr->dwFlags & MHDR_DONE) {
            midiOutUnprepareHeader(h, hdr, sizeof(MIDIHDR));
            delete[] hdr->lpData; delete hdr;
            m_pending[i] = m_pending.back(); m_pending.pop_back();
        } else ++i;
    }
}

void HostMidiOut::sendShort(const uint8_t* m, size_t n)
{
    DWORD w = m[0];
    if (n > 1) w |= DWORD(m[1]) << 8;
    if (n > 2) w |= DWORD(m[2]) << 16;
    midiOutShortMsg(static_cast<HMIDIOUT>(m_handle), w);
}

void HostMidiOut::sendSysex()
{
    reapDone();
    auto* hdr = new MIDIHDR{};
    hdr->lpData = new char[m_sysex.size()];
    std::memcpy(hdr->lpData, m_sysex.data(), m_sysex.size());
    hdr->dwBufferLength = hdr->dwBytesRecorded = DWORD(m_sysex.size());
    HMIDIOUT h = static_cast<HMIDIOUT>(m_handle);
    if (midiOutPrepareHeader(h, hdr, sizeof(MIDIHDR)) == MMSYSERR_NOERROR && midiOutLongMsg(h, hdr, sizeof(MIDIHDR)) == MMSYSERR_NOERROR)
        m_pending.push_back(hdr);
    else { midiOutUnprepareHeader(h, hdr, sizeof(MIDIHDR)); delete[] hdr->lpData; delete hdr; }
}

void HostMidiOut::byte(uint8_t b)
{
    std::lock_guard<std::mutex> l(m_mx);
    if (!m_handle) return;
    ++m_bytes;
    if (b >= 0xF8) { sendShort(&b, 1); return; }                 // realtime: anywhere, even inside SysEx
    if (b == 0xF0) { m_inSysex = true; m_sysex.assign(1, b); m_status = 0; return; }
    if (m_inSysex) {
        if (b == 0xF7) { m_sysex.push_back(b); sendSysex(); m_inSysex = false; m_sysex.clear(); return; }
        if (b & 0x80) { m_inSysex = false; m_sysex.clear(); }  // any other status ends a broken SysEx
        else { m_sysex.push_back(b); return; }
    }
    if (b & 0x80) {
        m_status = 0; m_have = 0;
        if (b < 0xF0) { m_status = b; m_need = ((b & 0xE0) == 0xC0) ? 1 : 2; m_msg[0] = b; return; }
        if (b == 0xF1 || b == 0xF3) { m_msg[0] = b; m_need = 1; m_status = b; return; }
        if (b == 0xF2) { m_msg[0] = b; m_need = 2; m_status = b; return; }
        if (b == 0xF6) sendShort(&b, 1);                       // tune request; F4/F5/F7 alone: dropped
        return;
    }
    if (!m_status) return;                                       // data with no status: dropped
    m_msg[1 + m_have++] = b;
    if (m_have == m_need) {
        sendShort(m_msg, 1 + m_need);
        m_have = 0;
        if (m_status >= 0xF0) m_status = 0;                      // system common has no running status
    }
}

// ---------------------------------------------------------------- MIDI IN
static void CALLBACK midiInProc(HMIDIIN, UINT msg, DWORD_PTR inst, DWORD_PTR p1, DWORD_PTR)
{
    auto* self = reinterpret_cast<HostMidiIn*>(inst);
    if (msg == MIM_DATA) self->onShort(uint32_t(p1));
    else if (msg == MIM_LONGDATA) self->onLong(reinterpret_cast<void*>(p1));
}

std::vector<std::string> HostMidiIn::list()
{
    std::vector<std::string> out;
    const UINT n = midiInGetNumDevs();
    for (UINT i = 0; i < n; ++i) {
        MIDIINCAPSW c{};
        if (midiInGetDevCapsW(i, &c, sizeof(c)) == MMSYSERR_NOERROR) out.push_back(utf8(c.szPname));
        else out.push_back("?");
    }
    return out;
}

bool HostMidiIn::open(unsigned index, Sink sink)
{
    close();
    m_sink = std::move(sink);
    m_closing = false;
    HMIDIIN h = nullptr;
    if (midiInOpen(&h, index, (DWORD_PTR)&midiInProc, (DWORD_PTR)this, CALLBACK_FUNCTION) != MMSYSERR_NOERROR) return false;
    m_handle = h;
    m_sysexBufs.assign(4, std::vector<uint8_t>(1024));
    for (auto& b : m_sysexBufs) {
        auto* hdr = new MIDIHDR{};
        hdr->lpData = reinterpret_cast<LPSTR>(b.data());
        hdr->dwBufferLength = DWORD(b.size());
        midiInPrepareHeader(h, hdr, sizeof(MIDIHDR));
        midiInAddBuffer(h, hdr, sizeof(MIDIHDR));
        m_hdrs.push_back(hdr);
    }
    midiInStart(h);
    return true;
}

void HostMidiIn::close()
{
    if (!m_handle) return;
    m_closing = true;
    HMIDIIN h = static_cast<HMIDIIN>(m_handle);
    midiInStop(h);
    midiInReset(h);
    for (void* v : m_hdrs) { auto* hdr = static_cast<MIDIHDR*>(v); midiInUnprepareHeader(h, hdr, sizeof(MIDIHDR)); delete hdr; }
    m_hdrs.clear();
    midiInClose(h);
    m_handle = nullptr;
}

void HostMidiIn::onShort(uint32_t msg)
{
    const uint8_t b[3] = { uint8_t(msg), uint8_t(msg >> 8), uint8_t(msg >> 16) };
    const uint8_t st = b[0];
    size_t n = 3;
    if (st < 0xF0) n = ((st & 0xE0) == 0xC0) ? 2 : 3;      // Cn / Dn carry one data byte
    else if (st == 0xF1 || st == 0xF3) n = 2;
    else if (st == 0xF2) n = 3;
    else n = 1;                                            // F6, F8-FF
    m_bytes += n;
    if (m_sink) m_sink(b, n);
}

void HostMidiIn::onLong(void* v)
{
    auto* hdr = static_cast<MIDIHDR*>(v);
    if (hdr->dwBytesRecorded && m_sink) { m_bytes += hdr->dwBytesRecorded; m_sink(reinterpret_cast<const uint8_t*>(hdr->lpData), hdr->dwBytesRecorded); }
    if (!m_closing && m_handle) midiInAddBuffer(static_cast<HMIDIIN>(m_handle), hdr, sizeof(MIDIHDR));
}

} // namespace MS2000
#endif // _WIN32
