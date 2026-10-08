// Thin GUI for user tests (2026-09-24): audio out + MIDI in selection, the 2x16 LCD, status.
// Everything shown is measured from the running emulation; nothing is simulated here.
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <tchar.h>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "winmm.lib")
#include <timeapi.h>

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

#include "thin_gui.h"
#include "../core/ms2000_runner.h"
#include "../core/master_vr.h"
#include <functional>
#include "../core/h8s2350_emulator.h"
#include "../core/dsp56362_emulator.h"
#include "../core/lcd_gui.h"
#include "vector_panel_imgui.h"
#include "../core/syx_tool.h"
#include "../core/knob_follow.h"
#include <commdlg.h>
#include <cmath>
#include <algorithm>
#include "../audio/host_io_win.h"

#include <atomic>
#include <mutex>
#include <ctime>
#include <direct.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
#include <deque>
#include <thread>

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace {
ID3D11Device*           g_dev = nullptr;
ID3D11DeviceContext*    g_ctx = nullptr;
IDXGISwapChain*         g_swap = nullptr;
ID3D11RenderTargetView* g_rtv = nullptr;

void createRT()  { ID3D11Texture2D* bb = nullptr; g_swap->GetBuffer(0, IID_PPV_ARGS(&bb)); if (bb) { g_dev->CreateRenderTargetView(bb, nullptr, &g_rtv); bb->Release(); } }
void cleanupRT() { if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; } }
bool createDevice(HWND h)
{
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2; sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; sd.OutputWindow = h;
    sd.SampleDesc.Count = 1; sd.Windowed = TRUE; sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    const D3D_FEATURE_LEVEL fl[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    D3D_FEATURE_LEVEL got;
    return D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, fl, 2, D3D11_SDK_VERSION,
                                         &sd, &g_swap, &g_dev, &got, &g_ctx) == S_OK;
}
void cleanupDevice()
{
    cleanupRT();
    if (g_swap) { g_swap->Release(); g_swap = nullptr; }
    if (g_ctx) { g_ctx->Release(); g_ctx = nullptr; }
    if (g_dev) { g_dev->Release(); g_dev = nullptr; }
}
LRESULT WINAPI wndProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (ImGui_ImplWin32_WndProcHandler(h, m, w, l)) return true;
    if (m == WM_SIZE && g_dev && w != SIZE_MINIMIZED) {
        cleanupRT(); g_swap->ResizeBuffers(0, LOWORD(l), HIWORD(l), DXGI_FORMAT_UNKNOWN, 0); createRT(); return 0;
    }
    if (m == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProc(h, m, w, l);
}

// HD44780 A00 character generator, if the local dump is present (16 bytes a character, rows
// 0-7 used, 5 low bits). Without it the LCD is drawn as text.
struct CgRom { bool ok = false; uint8_t d[256][8]{}; };
CgRom loadCgRom()
{
    CgRom r;
    for (const char* p : { "hd44780_a00.bin", "..\\hd44780_a00.bin", "..\\..\\hd44780_a00.bin" }) {
        std::ifstream f(p, std::ios::binary);
        if (!f) continue;
        std::vector<char> b((std::istreambuf_iterator<char>(f)), {});
        if (b.size() < 4096) continue;
        for (int c = 0; c < 256; ++c) for (int y = 0; y < 8; ++y) r.d[c][y] = uint8_t(b[c * 16 + y]) & 0x1F;
        r.ok = true; break;
    }
    return r;
}

void drawLcd(const LcdGuiSnapshot& s, const CgRom& rom, float dot)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    // LCD-GRID (2026-09-27, Tamas: the bottom line's dots ran together): every edge on a whole pixel. The old
    // line pitch was 8.5 x dot = 47.5 px and the gap 0.75 px, so line 2 sat half a pixel off and its gaps
    // rounded away. Now the dot, gap, pitches and origin are integers and the gap is at least one pixel.
    dot = std::floor(dot);
    const ImVec2 cp = ImGui::GetCursorScreenPos();
    const ImVec2 o(std::floor(cp.x), std::floor(cp.y));
    // VECTOR-PANEL: below 4 px a dot keeps no gap - a 1-px gap in a 2-px dot halves the lit area and the text fades.
    const float gap = dot < 4.0f ? 0.0f : (std::max)(1.0f, std::floor(dot * 0.2f + 0.5f)), cw = 6 * dot, ch = 10 * dot, pad = dot * 3;
    const ImVec2 size(16 * cw + 2 * pad, 2 * ch + 2 * pad);
    const ImU32 bg = IM_COL32(40, 60, 20, 255), on = IM_COL32(8, 14, 4, 255), off = IM_COL32(52, 76, 28, 255);
    dl->AddRectFilled(o, ImVec2(o.x + size.x, o.y + size.y), bg, dot);
    for (int ln = 0; ln < 2; ++ln)
        for (int col = 0; col < 16; ++col) {
            const uint8_t c = uint8_t((ln ? s.line1 : s.line0)[col]);
            const float x0 = o.x + pad + col * cw, y0 = o.y + pad + ln * ch;
            for (int y = 0; y < 8; ++y) {
                const uint8_t bits = !s.displayOn ? 0 : (c < 16 ? s.cgram[c & 7][y] : (rom.ok ? rom.d[c][y] : 0));
                for (int x = 0; x < 5; ++x) {
                    const bool lit = (bits >> (4 - x)) & 1;
                    dl->AddRectFilled(ImVec2(x0 + x * dot, y0 + y * dot), ImVec2(x0 + (x + 1) * dot - gap, y0 + (y + 1) * dot - gap), lit ? on : off);
                }
            }
            if (!rom.ok && s.displayOn && c >= 0x20) {
                char t[2] = { char(c), 0 };
                dl->AddText(ImVec2(x0 + dot, y0 + dot), on, t);
            }
        }
    ImGui::Dummy(size);
}

// VECTOR-PANEL: the LCD as it sits in the panel - a backlit yellow-green glass (Tamas' photo of the unit),
// the 16 x 2 cells of 5 x 8 dots fitted into (a, b). Dots are placed on whole pixels one by one, so their
// size may differ by a pixel but every edge is sharp; below 3 px a dot keeps no gap.
void drawLcdPanel(const LcdGuiSnapshot& s, const CgRom& rom, ImVec2 a, ImVec2 b)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 glass = IM_COL32(186, 220, 64, 255), on = IM_COL32(26, 38, 16, 255), off = IM_COL32(172, 206, 58, 255);
    dl->AddRectFilled(a, b, glass, 2.0f);
    const float dot = (std::min)((b.x - a.x) / 101.0f, (b.y - a.y) / 23.0f);
    const float ox = std::floor((a.x + b.x) * 0.5f - 47.5f * dot), oy = std::floor((a.y + b.y) * 0.5f - 8.5f * dot);
    const float gap = dot >= 3.0f ? (std::max)(1.0f, std::floor(dot * 0.15f + 0.5f)) : 0.0f;
    for (int ln = 0; ln < 2; ++ln)
        for (int col = 0; col < 16; ++col) {
            const uint8_t c = uint8_t((ln ? s.line1 : s.line0)[col]);
            for (int y = 0; y < 8; ++y) {
                const uint8_t bits = !s.displayOn ? 0 : (c < 16 ? s.cgram[c & 7][y] : (rom.ok ? rom.d[c][y] : 0));
                const float y0 = std::floor(oy + (ln * 9 + y) * dot), y1 = std::floor(oy + (ln * 9 + y + 1) * dot) - gap;
                for (int x = 0; x < 5; ++x) {
                    const float x0 = std::floor(ox + (col * 6 + x) * dot), x1 = std::floor(ox + (col * 6 + x + 1) * dot) - gap;
                    dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), ((bits >> (4 - x)) & 1) ? on : off);
                }
            }
        }
}

// Saves this window's own back buffer as a BMP (MS2K_GUISHOT=<seconds>: once, that long after
// start, to thin_gui_shot.bmp). Only the GUI's own pixels - never the rest of the screen.
void saveBackBuffer(const char* path)
{
    ID3D11Texture2D* bb = nullptr;
    if (FAILED(g_swap->GetBuffer(0, IID_PPV_ARGS(&bb))) || !bb) return;
    D3D11_TEXTURE2D_DESC d; bb->GetDesc(&d);
    d.Usage = D3D11_USAGE_STAGING; d.BindFlags = 0; d.CPUAccessFlags = D3D11_CPU_ACCESS_READ; d.MiscFlags = 0;
    ID3D11Texture2D* st = nullptr;
    if (SUCCEEDED(g_dev->CreateTexture2D(&d, nullptr, &st))) {
        g_ctx->CopyResource(st, bb);
        D3D11_MAPPED_SUBRESOURCE m;
        if (SUCCEEDED(g_ctx->Map(st, 0, D3D11_MAP_READ, 0, &m))) {
            FILE* f = fopen(path, "wb");
            if (f) {
                const uint32_t w = d.Width, h = d.Height, row = w * 4, size = 54 + row * h;
                uint8_t hdr[54] = { 'B', 'M' };
                auto put = [&](int o, uint32_t v) { hdr[o] = uint8_t(v); hdr[o + 1] = uint8_t(v >> 8); hdr[o + 2] = uint8_t(v >> 16); hdr[o + 3] = uint8_t(v >> 24); };
                put(2, size); put(10, 54); put(14, 40); put(18, w); put(22, h); hdr[26] = 1; hdr[28] = 32; put(34, row * h);
                fwrite(hdr, 1, 54, f);
                std::vector<uint8_t> line(row);
                for (int y = int(h) - 1; y >= 0; --y) {
                    const uint8_t* src = static_cast<const uint8_t*>(m.pData) + size_t(y) * m.RowPitch;
                    for (uint32_t x = 0; x < w; ++x) { line[4*x] = src[4*x+2]; line[4*x+1] = src[4*x+1]; line[4*x+2] = src[4*x]; line[4*x+3] = 255; }
                    fwrite(line.data(), 1, row, f);
                }
                fclose(f);
            }
            g_ctx->Unmap(st, 0);
        }
        st->Release();
    }
    bb->Release();
}

struct Settings {
    std::string audio, midi, midiOut; float volume = 0.7f;
    bool powerSwitch = true;                          // PWR-SW-1: the VOLUME pot's switch - off at the minimum
    int boostDb = 0;                                  // OUT-BOOST-1: master output boost after the VOLUME pot, 0..12 dB
    bool vectorPanel = true;                          // VECTOR-PANEL: the MS2000R panel drawing (false = the test panel)
    bool knobFollow = false;                          // KNOB-FOLLOW: the pots show the program being edited
    // AIN-SOURCES: what feeds each MS2000 input jack - a capture endpoint and one of its channels
    // (0 = left, 1 = right, 2 = (L+R)/2); an empty device = nothing plugged in.
    struct InSrc { std::string dev; int ch = 0; };
    InSrc in[2];
    float vr30 = 1.0f, vr31 = 1.0f;                   // Input 1 / Input 2 level knobs (KOD-A30413)
    bool mic2 = false, dac20 = false;                 // SW1 (Input 2 MIC/LINE), AK4522 DAC 20-bit option
    // PANEL-IO: where the 32 pots stand (IC1..IC4 = AN4..AN7, X0..X7, 0..1023) - a real unit keeps its knob
    // positions across power cycles, so the GUI does too. Default: every pot at its centre.
    uint16_t knob[4][8] = { {512,512,512,512,512,512,512,512}, {512,512,512,512,512,512,512,512},
                            {512,512,512,512,512,512,512,512}, {512,512,512,512,512,512,512,512} };
};
Settings loadSettings()
{
    Settings s; std::ifstream f("thin_gui.ini"); std::string line;
    while (std::getline(f, line)) {
        const auto eq = line.find('='); if (eq == std::string::npos) continue;
        const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        if (k == "power_switch") s.powerSwitch = v != "0"; else if (k == "boost_db") s.boostDb = std::clamp(std::atoi(v.c_str()), 0, 12); else if (k == "audio") s.audio = v; else if (k == "midi") s.midi = v; else if (k == "midi_out") s.midiOut = v; else if (k == "vector_panel") s.vectorPanel = v != "0"; else if (k == "knob_follow") s.knobFollow = v == "1"; else if (k == "volume") s.volume = float(atof(v.c_str()));
        else if (k == "audio_in") { if (!v.empty()) { s.in[0] = { v, 0 }; s.in[1] = { v, 1 }; } }   // old single setting: L -> IN1, R -> IN2
        else if (k == "in1_src" || k == "in2_src") {
            Settings::InSrc& d = s.in[k == "in2_src" ? 1 : 0]; const auto bar = v.rfind('|');
            if (bar == std::string::npos) d = { v, 0 }; else d = { v.substr(0, bar), atoi(v.c_str() + bar + 1) };
            if (d.ch < 0 || d.ch > 2) d.ch = 0;
        }
        else if (k == "in1_level") s.vr30 = float(atof(v.c_str()));
        else if (k == "in2_level") s.vr31 = float(atof(v.c_str())); else if (k == "in2_mic") s.mic2 = v == "1";
        else if (k == "dac20") s.dac20 = v == "1";
        else if (k == "knobs") { const char* q = v.c_str(); for (int i = 0; i < 32 && *q; ++i) { s.knob[i / 8][i % 8] = uint16_t((std::min)(1023, (std::max)(0, atoi(q)))); while (*q && *q != ',') ++q; if (*q) ++q; } }
    }
    return s;
}
void saveSettings(const Settings& s)
{
    std::ofstream f("thin_gui.ini");
    f << "power_switch=" << (s.powerSwitch ? 1 : 0) << "\nboost_db=" << s.boostDb << "\naudio=" << s.audio << "\nmidi=" << s.midi << "\nmidi_out=" << s.midiOut << "\nvector_panel=" << (s.vectorPanel ? 1 : 0) << "\nknob_follow=" << (s.knobFollow ? 1 : 0) << "\nvolume=" << s.volume << "\n"
      << "in1_src=" << s.in[0].dev << "|" << s.in[0].ch << "\nin2_src=" << s.in[1].dev << "|" << s.in[1].ch
      << "\nin1_level=" << s.vr30 << "\nin2_level=" << s.vr31
      << "\nin2_mic=" << (s.mic2 ? 1 : 0) << "\ndac20=" << (s.dac20 ? 1 : 0) << "\n";
    f << "knobs=";
    for (int i = 0; i < 32; ++i) f << (i ? "," : "") << s.knob[i / 8][i % 8];
    f << "\n";
}
} // namespace

int run_thin_gui(MS2000::Ms2kRunner& runner, std::function<std::unique_ptr<MS2000::Ms2kRunner>()> makeRunner)
{
    using namespace MS2000;
    // PWR-SW-1 (standalone, 2026-10-08): the VOLUME pot's switch section turns the machine off at the minimum and a
    // power-on boots a NEW machine (makeRunner), as the plugin does - flash state kept by the runner's stop()/init().
    // R = the running machine, nullptr while switched off.
    MS2000::Ms2kRunner* R = &runner;
    std::unique_ptr<MS2000::Ms2kRunner> owned;
    bool off = false;
    static VPanel::IO vio;                                // the vector panel's state (held keys survive a power cycle)
    static bool swHeld[7][8] = {};                        // the test panel's held keys
    // An emulator is a real-time load: opt out of Windows' execution-speed throttling (EcoQoS),
    // which is applied to windows that are not in the foreground, and ask for 1 ms timer
    // granularity so the audio-paced 1 ms waits are 1 ms.
    {
        PROCESS_POWER_THROTTLING_STATE pt{};
        pt.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
        pt.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
        pt.StateMask = 0;
        SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &pt, sizeof(pt));
        timeBeginPeriod(1);
    }
    WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC, wndProc, 0, 0, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, _T("MS2000Thin"), nullptr };
    RegisterClassEx(&wc);
    HWND hwnd = CreateWindow(wc.lpszClassName, _T("MS2000R emulator v") MS2K_VERSION, WS_OVERLAPPEDWINDOW, 30, 20, 1600, 1060, nullptr, nullptr, wc.hInstance, nullptr);
    if (!createDevice(hwnd)) { cleanupDevice(); UnregisterClass(wc.lpszClassName, wc.hInstance); return -1; }
    createRT();
    ShowWindow(hwnd, SW_SHOWDEFAULT); UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::StyleColorsDark();
    // VECTOR-PANEL legends: a narrow bold sans from Windows, loaded large so it scales down crisp; the
    // default font stays the UI font (added first). None found -> the default font.
    ImFont* panelFont = nullptr;
    {
        ImGuiIO& fio = ImGui::GetIO();
        fio.Fonts->AddFontDefault();
        const char* cands[] = { "C:\\Windows\\Fonts\\ARIALNB.TTF", "C:\\Windows\\Fonts\\arialbd.ttf", "C:\\Windows\\Fonts\\segoeuib.ttf" };
        for (const char* cf : cands)
            if (GetFileAttributesA(cf) != INVALID_FILE_ATTRIBUTES) { panelFont = fio.Fonts->AddFontFromFileTTF(cf, 40.0f); break; }
    }
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_dev, g_ctx);

    const CgRom rom = loadCgRom();
    Settings st = loadSettings();

    DSP56362Emulator* dsp = R->getEmulator().dsp();
    if (dsp) dsp->enableAudioRing(true);
    auto outGain = [&st] { return ms2kMasterVrGain(st.volume) * ms2kBoostGain(st.boostDb); };   // MVR-1 pot, OUT-BOOST-1
    std::atomic<float> gain{ outGain() };
    std::atomic<bool>  primed{ false };

    // WAV RECORDER (2026-09-25, GUI-3). What the DSP put on TX0 slot 0/1 - the same 24-bit
    // samples the host output plays, taken BEFORE the host volume - into
    // recordings\ms2000_YYYYMMDD_HHMMSS.wav, 48 kHz, 24-bit PCM stereo. Written from the
    // audio thread; the header is patched when recording stops.
    struct Recorder {
        std::mutex mx; FILE* f = nullptr; uint64_t frames = 0; std::string path;
        std::atomic<bool> active{false};   // PERF-124: the audio callback must not take a lock per sample when idle
        void start() {
            std::lock_guard<std::mutex> l(mx); if (f) return;
            _mkdir("recordings");
            char nm[96]; time_t t = time(nullptr); tm lt; localtime_s(&lt, &t);
            strftime(nm, sizeof nm, "recordings\\ms2000_%Y%m%d_%H%M%S.wav", &lt);
            path = nm; f = fopen(nm, "wb"); frames = 0;
            if (f) { uint8_t h[44] = {}; fwrite(h, 1, 44, f); active.store(true, std::memory_order_release); }
        }
        void push(int32_t l, int32_t r) {       // lock only while a file is open
            if (!active.load(std::memory_order_acquire)) return;
            std::lock_guard<std::mutex> g(mx); if (!f) return;
            uint8_t b[6] = { uint8_t(l), uint8_t(l >> 8), uint8_t(l >> 16), uint8_t(r), uint8_t(r >> 8), uint8_t(r >> 16) };
            fwrite(b, 1, 6, f); ++frames;
        }
        void stop() {
            std::lock_guard<std::mutex> l(mx); if (!f) return;
            active.store(false, std::memory_order_release);
            const uint32_t data = uint32_t(frames * 6), rate = 48000;
            auto w32 = [&](uint32_t v) { fwrite(&v, 4, 1, f); }; auto w16 = [&](uint16_t v) { fwrite(&v, 2, 1, f); };
            fseek(f, 0, SEEK_SET);
            fwrite("RIFF", 1, 4, f); w32(36 + data); fwrite("WAVEfmt ", 1, 8, f);
            w32(16); w16(1); w16(2); w32(rate); w32(rate * 6); w16(6); w16(24);
            fwrite("data", 1, 4, f); w32(data);
            fclose(f); f = nullptr;
            printf("[THIN-GUI] recorded %.1f s to %s\n", double(frames) / rate, path.c_str());
        }
        bool on() { return active.load(std::memory_order_acquire); }
        double seconds() { std::lock_guard<std::mutex> l(mx); return double(frames) / 48000.0; }
    };
    static Recorder rec;

    HostAudioOut audio;
    HostMidiIn midi;
    HostMidiOut midiOut;                                     // MIDI OUT (2026-09-27): TDR1 bytes to a host port
    std::vector<std::string> midiOutDevs = HostMidiOut::list();
    int midiOutSel = -1;
    for (int i = 0; i < int(midiOutDevs.size()); ++i) if (midiOutDevs[i] == st.midiOut) midiOutSel = i;
    auto openMidiOut = [&]() {
        midiOut.close();
        if (midiOutSel < 0 || !midiOut.open(unsigned(midiOutSel))) { midiOutSel = -1; st.midiOut.clear(); return; }
        st.midiOut = midiOutDevs[midiOutSel];
    };
    // MIDI-OUT-TIMING (2026-09-28): the emulator runs ~50 ms (the audio ring) ahead of what is heard, so a byte
    // sent the moment the firmware writes TDR1 left that much EARLY - a clock slave ran ahead of the synth. Now
    // each byte is stamped with the DSP frame it belongs to and a 1 ms thread sends it when that frame is heard:
    // heard(t) = frames handed to the device at its last callback - the device buffer (STATED: the 20 ms asked
    // for, 960 frames) + the time since that callback. Before the audio stream runs (boot) or without one,
    // bytes go out at once, as before.
    struct MidiOutClock {
        struct E { uint64_t frame; uint8_t b; int64_t qpc; };
        std::mutex mx; std::deque<E> q; std::atomic<double> lagSum{0}; std::atomic<uint64_t> lagN{0};
        std::atomic<uint64_t> base{0}; std::atomic<int64_t> baseQpc{0}; std::atomic<bool> on{false}, stop{false};
        std::thread th;
    };
    static MidiOutClock moc;
    static MS2000::SyxTool syx;   // SYX-1: .syx import / program export through the machine's own MIDI
    syx.setSend([&R](const uint8_t* p, size_t n) { R->sendMIDIData(p, n); });
    auto setSink = [&]() { R->getEmulator().setMidiOutSink([&midiOut, dsp](uint8_t b) {
        syx.feed(b);
        if (dsp && moc.on.load(std::memory_order_acquire)) { LARGE_INTEGER t; QueryPerformanceCounter(&t); std::lock_guard<std::mutex> l(moc.mx); moc.q.push_back({ dsp->txFrames(), b, t.QuadPart }); }
        else midiOut.byte(b);
    }); };
    setSink();
    moc.th = std::thread([&midiOut] {
        LARGE_INTEGER f; QueryPerformanceFrequency(&f);
        std::vector<uint8_t> out;
        while (!moc.stop.load()) {
            Sleep(1);
            out.clear();
            {
                std::lock_guard<std::mutex> l(moc.mx);
                if (!moc.on.load()) { for (auto& e : moc.q) out.push_back(e.b); moc.q.clear(); }
                else if (!moc.q.empty()) {
                    LARGE_INTEGER now; QueryPerformanceCounter(&now);
                    const uint64_t base = moc.base.load();
                    const double dt = double(now.QuadPart - moc.baseQpc.load()) / double(f.QuadPart);
                    const double heard = double(base) - 960.0 + (std::min)(dt, 0.05) * 48000.0;
                    while (!moc.q.empty() && double(moc.q.front().frame) <= heard) {
                        moc.lagSum.store(moc.lagSum.load() + double(now.QuadPart - moc.q.front().qpc) * 1000.0 / double(f.QuadPart)); ++moc.lagN;
                        out.push_back(moc.q.front().b); moc.q.pop_front();
                    }
                }
            }
            for (uint8_t b : out) midiOut.byte(b);
        }
    });
    std::vector<HostDevice> audioDevs = HostAudioOut::list();
    std::vector<std::string> midiDevs = HostMidiIn::list();
    int audioSel = -1, midiSel = -1;
    for (int i = 0; i < int(audioDevs.size()); ++i) if (audioDevs[i].name == st.audio) audioSel = i;
    if (audioSel < 0 && !audioDevs.empty()) audioSel = 0;
    if (getenv("MS2K_GUI_NOAUDIO")) audioSel = -1;          // measurement: wall-clock pacing, no device
    for (int i = 0; i < int(midiDevs.size()); ++i) if (midiDevs[i] == st.midi) midiSel = i;

    auto openAudio = [&]() {
        audio.close();
        if (R) R->setAudioPaced(false);
        if (audioSel < 0 || !dsp || !R) return;
        { int32_t l, r; while (dsp->popAudio(l, r)) {} }             // start from an empty ring
        primed = false;
        audio.open(audioDevs[audioSel].id, [dsp, &gain, &primed](float* lr, uint32_t frames) -> uint32_t {
            const float g = gain.load(std::memory_order_relaxed) / 8388608.0f;
            uint32_t n = 0; int32_t l, r;
            while (n < frames && dsp->popAudio(l, r)) { lr[2 * n] = l * g; lr[2 * n + 1] = r * g; rec.push(l, r); ++n; }
            if (n) {
                primed = true;
                LARGE_INTEGER now; QueryPerformanceCounter(&now);   // MIDI-OUT-TIMING: where the listener is
                moc.base.store(dsp->txFrames() - dsp->audioFill()); moc.baseQpc.store(now.QuadPart);
            }
            // Before the DSP's first frame (boot) an empty ring is not an underrun - it is the
            // silence of a machine that has not started its audio yet.
            if (!primed) { for (uint32_t i = 0; i < 2 * frames; ++i) lr[i] = 0.0f; return frames; }
            return n;
        });
        R->setAudioPaced(true, 2400);                             // 50 ms of emulated audio ahead
        st.audio = audioDevs[audioSel].name;
    };
    // AUDIO-IN / AIN-SOURCES: each MS2000 input jack takes one channel of a capture endpoint (the UMC1820
    // shows its inputs as stereo pairs, "IN 03-04": left = 03, right = 04). One capture stream per distinct
    // endpoint; its sink hands each input that listens to it the chosen channel as a mono stream.
    HostAudioIn ain[2];
    std::vector<HostDevice> inDevs = HostAudioIn::list();
    auto devIndex = [&](const std::string& name) { for (int i = 0; i < int(inDevs.size()); ++i) if (inDevs[i].name == name) return i; return -1; };
    auto openAudioIn = [&]() {
        ain[0].close(); ain[1].close();
        if (dsp) dsp->stopAudioIn();
        if (!dsp) return;
        std::string opened[2];
        int nOpen = 0;
        for (int in = 0; in < 2; ++in) {
            const std::string dev = st.in[in].dev;
            if (dev.empty() || devIndex(dev) < 0) continue;
            if ((nOpen > 0 && opened[0] == dev) || (nOpen > 1 && opened[1] == dev)) continue;
            // the routes of this endpoint: which inputs, which channel
            int route[2] = { -1, -1 };
            for (int j = 0; j < 2; ++j) if (st.in[j].dev == dev) route[j] = st.in[j].ch;
            ain[nOpen].open(inDevs[devIndex(dev)].id, [dsp, route](const float* lr, uint32_t n) {
                thread_local std::vector<float> mono;
                mono.resize(n);
                for (int j = 0; j < 2; ++j) {
                    if (route[j] < 0) continue;
                    for (uint32_t k = 0; k < n; ++k)
                        mono[k] = route[j] == 2 ? 0.5f * (lr[2 * k] + lr[2 * k + 1]) : lr[2 * k + route[j]];
                    dsp->pushAudioIn(j, mono.data(), n);
                }
            });
            opened[nOpen++] = dev;
        }
    };
    auto srcLabel = [](const std::string& dev, int ch) {
        if (dev.empty()) return std::string("(none)");
        static const char* c[3] = { " - L", " - R", " - L+R" };
        return dev + c[ch < 0 || ch > 2 ? 0 : ch];
    };
    if (dsp) { dsp->setInputStage(st.vr30, st.vr31, st.mic2); dsp->setDac20(st.dac20); }
    auto openMidi = [&]() {
        midi.close();
        if (midiSel < 0) { st.midi.clear(); return; }
        st.midi = midiDevs[midiSel];
        if (!R) return;                                   // switched off: opened again at the power-on
        midi.open(unsigned(midiSel), [&R](const uint8_t* d, size_t n) { if (R) R->sendMIDIData(d, n); });
    };
    // PWR-SW-1: a fresh machine gets the panel as it stands (pots, input stage, DAC, held keys) before it runs
    auto wireMachine = [&]() {
        dsp = R->getEmulator().dsp();
        if (dsp) { dsp->enableAudioRing(true); dsp->setInputStage(st.vr30, st.vr31, st.mic2); dsp->setDac20(st.dac20); }
        setSink();
        for (unsigned m = 0; m < 4; ++m) for (unsigned x = 0; x < 8; ++x) R->getEmulator().setPanelKnob(m, x, st.knob[m][x]);
        for (unsigned c = 0; c < 7; ++c) for (unsigned r = 0; r < 8; ++r)
            if (vio.swHeld[c][r] || swHeld[c][r]) R->getEmulator().setPanelSwitch(c, r, true);
    };
    auto powerOff = [&]() {
        midi.close(); audio.close(); ain[0].close(); ain[1].close();
        if (R) { R->setAudioPaced(false); R->getEmulator().setMidiOutSink(nullptr); R->stop(); }
        R = nullptr; owned.reset(); dsp = nullptr; off = true; primed = false;
        printf("[THIN-GUI] POWER off (VOLUME at its minimum)\n"); fflush(stdout);
    };
    auto powerOn = [&]() {
        static ULONGLONG lastTry = 0;
        if (GetTickCount64() - lastTry < 2000) return;
        lastTry = GetTickCount64();
        owned = makeRunner ? makeRunner() : nullptr;
        if (!owned) { printf("[THIN-GUI] power on FAILED (machine did not init)\n"); fflush(stdout); return; }
        R = owned.get(); off = false;
        wireMachine();
        R->start();
        openAudio(); openAudioIn(); openMidi();
        printf("[THIN-GUI] POWER on - cold boot\n"); fflush(stdout);
    };

    // PANEL-IO: the pots stand where they stood before the firmware first reads them.
    for (unsigned m = 0; m < 4; ++m) for (unsigned x = 0; x < 8; ++x) R->getEmulator().setPanelKnob(m, x, st.knob[m][x]);
    openMidiOut();
    if (st.powerSwitch && ms2kPowerSwitchOpen(st.volume)) { R = nullptr; dsp = nullptr; off = true; }   // switched off: no boot
    else R->start();
    openAudio();
    openAudioIn();
    openMidi();

    ULONGLONG shotAt = 0;
    if (const char* e = getenv("MS2K_GUISHOT")) shotAt = GetTickCount64() + ULONGLONG(atof(e) * 1000.0);
    int demoPhase = 0; ULONGLONG demoT0 = 0, demoAt = 0;
    if (const char* e = getenv("MS2K_GUI_DEMO")) demoAt = GetTickCount64() + ULONGLONG(atof(e) * 1000.0);   // test: press at t
    bool testNote = false;
    bool done = false;
    while (!done) {
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessage(&msg); if (msg.message == WM_QUIT) done = true; }
        if (done) break;
        {   // PWR-SW-1: the VOLUME pot's switch section
            const bool open = st.powerSwitch && ms2kPowerSwitchOpen(st.volume);
            if (open && !off) powerOff(); else if (!open && off) powerOn();
        }

        ImGui_ImplDX11_NewFrame(); ImGui_ImplWin32_NewFrame(); ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGui::Begin("main", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);

        {   // HOST-AUDIO-STALL (2026-09-27): a host stream that ended on its own (device reset, lost clock)
            // no longer freezes the machine - stop pacing on it at once, then reopen it every 2 s.
            static ULONGLONG lastOut = 0, lastIn = 0;
            const ULONGLONG now = GetTickCount64();
            moc.on.store(audio.isOpen() && !audio.dead() && primed.load(), std::memory_order_release);   // MIDI-OUT-TIMING
            if (audio.dead()) {
                if (R && R->isAudioPaced()) R->setAudioPaced(false);
                if (now - lastOut >= 2000) { lastOut = now; printf("[THIN-GUI] audio out ended (%s) - reopening\n", audio.lastError().c_str()); fflush(stdout); openAudio(); }
            }
            if ((ain[0].dead() || ain[1].dead()) && now - lastIn >= 2000) {
                lastIn = now; printf("[THIN-GUI] audio in ended - reopening\n"); fflush(stdout); openAudioIn();
            }
        }
        ImGui::SetNextItemWidth(320);
        if (ImGui::BeginCombo("Audio out", audioSel >= 0 ? audioDevs[audioSel].name.c_str() : "(none)")) {
            if (ImGui::Selectable("(none)", audioSel < 0)) { audioSel = -1; openAudio(); st.audio.clear(); }
            for (int i = 0; i < int(audioDevs.size()); ++i)
                if (ImGui::Selectable(audioDevs[i].name.c_str(), i == audioSel)) { audioSel = i; openAudio(); }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(220);
        if (ImGui::BeginCombo("MIDI in", midiSel >= 0 ? midiDevs[midiSel].c_str() : "(none)")) {
            if (ImGui::Selectable("(none)", midiSel < 0)) { midiSel = -1; openMidi(); }
            for (int i = 0; i < int(midiDevs.size()); ++i)
                if (ImGui::Selectable(midiDevs[i].c_str(), i == midiSel)) { midiSel = i; openMidi(); }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(220);
        if (ImGui::BeginCombo("MIDI out", midiOutSel >= 0 ? midiOutDevs[midiOutSel].c_str() : "(none)")) {
            if (ImGui::Selectable("(none)", midiOutSel < 0)) { midiOutSel = -1; openMidiOut(); }
            for (int i = 0; i < int(midiOutDevs.size()); ++i)
                if (ImGui::Selectable(midiOutDevs[i].c_str(), i == midiOutSel)) { midiOutSel = i; openMidiOut(); }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::Button("Rescan")) {
            const std::string a = audioSel >= 0 ? audioDevs[audioSel].name : "", m = midiSel >= 0 ? midiDevs[midiSel] : "";
            audioDevs = HostAudioOut::list(); midiDevs = HostMidiIn::list();
            inDevs = HostAudioIn::list(); openAudioIn();
            audioSel = -1; midiSel = -1;
            for (int i = 0; i < int(audioDevs.size()); ++i) if (audioDevs[i].name == a) audioSel = i;
            for (int i = 0; i < int(midiDevs.size()); ++i) if (midiDevs[i] == m) midiSel = i;
            const std::string mo = midiOutSel >= 0 ? midiOutDevs[midiOutSel] : "";
            midiOutDevs = HostMidiOut::list(); midiOutSel = -1;
            for (int i = 0; i < int(midiOutDevs.size()); ++i) if (midiOutDevs[i] == mo) midiOutSel = i;
            openMidiOut();
        }

        ImGui::Spacing();
        if (!st.vectorPanel) { drawLcd(R ? R->getLcdGuiSnapshot() : LcdGuiSnapshot{}, rom, 5.0f); ImGui::Spacing(); }

        float vol = st.volume;
        ImGui::SetNextItemWidth(200);
        if (ImGui::SliderFloat("VOLUME", &vol, 0.0f, 1.0f, "%.2f")) { st.volume = vol; gain = outGain(); }
        ImGui::SameLine(); ImGui::SetNextItemWidth(130);
        if (ImGui::SliderInt("Output boost", &st.boostDb, 0, 12, "+%d dB")) gain = outGain();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Master output boost after the VOLUME pot, 1 dB steps (above 0 dB full scale the output clips)");
        ImGui::SameLine(); ImGui::Checkbox("POWER switch", &st.powerSwitch);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("The VOLUME pot's switch: at the minimum the machine is off, as on the unit");
        if (off) { ImGui::SameLine(); ImGui::TextColored(ImVec4(1, 0.45f, 0.35f, 1), "POWER OFF - turn VOLUME up"); }

        // AUDIO-IN row: capture device, the two input level knobs, SW1, the DAC option, ADC clip counts.
        bool stageChanged = false;
        for (int in = 0; in < 2; ++in) {
            ImGui::PushID(in);
            ImGui::SetNextItemWidth(300);
            const std::string cur = srcLabel(st.in[in].dev, st.in[in].ch);
            if (ImGui::BeginCombo(in ? "AUDIO IN 2" : "AUDIO IN 1", cur.c_str())) {
                if (ImGui::Selectable("(none)", st.in[in].dev.empty())) { st.in[in] = {}; openAudioIn(); }
                for (const auto& d : inDevs)
                    for (int ch = 0; ch < 3; ++ch) {
                        const std::string lab = srcLabel(d.name, ch);
                        if (ImGui::Selectable(lab.c_str(), st.in[in].dev == d.name && st.in[in].ch == ch)) { st.in[in] = { d.name, ch }; openAudioIn(); }
                    }
                ImGui::EndCombo();
            }
            ImGui::SameLine(); ImGui::SetNextItemWidth(110);
            stageChanged |= ImGui::SliderFloat("level", in ? &st.vr31 : &st.vr30, 0.0f, 1.0f, "%.2f");
            ImGui::PopID();
        }
        ImGui::SameLine();
        stageChanged |= ImGui::Checkbox("IN2 MIC", &st.mic2);
        if (stageChanged && dsp) dsp->setInputStage(st.vr30, st.vr31, st.mic2);
        ImGui::SameLine();
        if (ImGui::Checkbox("DAC 20-bit", &st.dac20) && dsp) dsp->setDac20(st.dac20);
        if (dsp && (dsp->audioInLive(0) || dsp->audioInLive(1))) {
            ImGui::Text("ADC clip %llu/%llu  in %.0f/%.0f ms  in-underrun %llu", (unsigned long long)dsp->audioInClips(0),
                        (unsigned long long)dsp->audioInClips(1), dsp->audioInFill(0) / 48.0, dsp->audioInFill(1) / 48.0,
                        (unsigned long long)dsp->audioInUnderruns());
        }
        for (auto& a : ain)
            if (a.isOpen() && !a.lastError().empty()) { ImGui::SameLine(); ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", a.lastError().c_str()); }
        ImGui::SameLine();
        ImGui::Button("Test note C4 (hold)");
        const bool held = ImGui::IsItemActive();
        if (held != testNote) {
            testNote = held;
            const uint8_t m[3] = { uint8_t(held ? 0x90 : 0x80), 0x3C, 0x64 };
            if (R) R->sendMIDIData(m, 3);
        }

        // PANEL (2026-09-27, PANEL-IO): the MS2000R front panel, section by section (owner's manual p.5), wired
        // to the three circuits of the service manual - switches KOD-A30414 (SS column, T row), knobs KOD-A30415
        // (HC4051 IC1..IC4 = AN4..AN7, input X0..X7) and LEDs KOD-A30416 (row LSn, column LDm). Buttons are
        // momentary while the mouse holds them. Knobs: drag up/down (Shift = fine), wheel, double-click = centre.
        H8S2350Emulator* cpu = R ? &R->getEmulator() : nullptr;   // PWR-SW-1: nullptr while switched off
        const H8S2350Emulator::PanelLeds leds = cpu ? cpu->panelLeds() : H8S2350Emulator::PanelLeds{};
        {   // MS2K_GUILEDLOG=1 (R2 diagnostic, default off): once a second, the LEDs exactly as this frame draws them.
            static const bool on = getenv("MS2K_GUILEDLOG") != nullptr; static ULONGLONG last = 0;
            if (on && GetTickCount64() - last >= 1000) {
                last = GetTickCount64();
                printf("[GUI-LEDS] emu=%.2f", dsp ? double(dsp->txFrames()) / 48000.0 : 0.0);
                for (unsigned r = 0; r < 8; ++r) for (unsigned c = 0; c < 12; ++c)
                    if (leds.lit[r][c] > 0.25f) printf(" LS%u.LD%02u(%.2f)", r, c, double(leds.lit[r][c]));
                printf("\n"); fflush(stdout);
            }
        }
        auto sw = [&](const char* label, unsigned col, unsigned row, float w = 0) {
            ImGui::Button(label, ImVec2(w, 0));
            const bool h = ImGui::IsItemActive();
            if (h != swHeld[col][row] && demoPhase == 0) { swHeld[col][row] = h; if (cpu) cpu->setPanelSwitch(col, row, h); }
        };
        auto ledColor = [&](float r, float g) {
            r = (std::min)(1.0f, r * 1.15f); g = (std::min)(1.0f, g * 1.15f);
            return ImGui::GetColorU32(ImVec4(0.22f + 0.78f * r, 0.07f + 0.83f * g + 0.08f * r, 0.07f, 1.0f));
        };
        auto ledDot = [&](float r, float g) {
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float h = ImGui::GetTextLineHeight();
            ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + 5, p.y + h * 0.5f), 4.5f, ledColor(r, g));
            ImGui::Dummy(ImVec2(12, h));
        };
        static bool shown[8][12]; std::memset(shown, 0, sizeof shown);   // which LED positions the panel draws
        auto led = [&](const char* label, unsigned ls, unsigned ld) {   // one red LED with its legend
            shown[ls][ld] = true;
            ledDot(leds.lit[ls][ld], 0.0f); ImGui::SameLine(0, 2); ImGui::TextUnformatted(label);
        };
        auto led2 = [&](const char* label, unsigned lsG, unsigned lsR) {   // bicolor LED on LD11: G row, R row
            shown[lsG][11] = shown[lsR][11] = true;
            ledDot(leds.lit[lsR][11], leds.lit[lsG][11]); ImGui::SameLine(0, 2); ImGui::TextUnformatted(label);
        };
        auto knob = [&](const char* label, unsigned mux, unsigned x, bool detent) {
            uint16_t& v = st.knob[mux][x];
            ImGui::PushID(int(mux * 8 + x) + 1000);
            ImGui::BeginGroup();
            const float r = 17.0f, w = 70.0f;
            const ImVec2 p = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton("k", ImVec2(w, 2 * r + 4));
            ImGuiIO& io = ImGui::GetIO();
            int nv = v;
            if (ImGui::IsItemActive() && io.MouseDelta.y != 0.0f) nv -= int(io.MouseDelta.y * (io.KeyShift ? 1.0f : 4.0f));
            if (ImGui::IsItemHovered() && io.MouseWheel != 0.0f) nv += int(io.MouseWheel * (io.KeyShift ? 1.0f : 16.0f));
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) nv = 512;
            nv = nv < 0 ? 0 : nv > 1023 ? 1023 : nv;
            if (nv != v) { v = uint16_t(nv); if (cpu) cpu->setPanelKnob(mux, x, v); }
            const char* le = strstr(label, "##"); if (!le) le = label + strlen(label);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%.*s  %u / 1023  (AN%u X%u)", int(le - label), label, unsigned(v), mux + 4, x);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 c(p.x + w * 0.5f, p.y + r + 2);
            dl->AddCircleFilled(c, r, IM_COL32(40, 42, 46, 255), 32);
            dl->AddCircle(c, r, IM_COL32(150, 150, 150, 255), 32, 1.5f);
            if (detent) dl->AddLine(ImVec2(c.x, c.y - r - 3), ImVec2(c.x, c.y - r + 1), IM_COL32(220, 220, 220, 255), 2.0f);
            const float a = 3.14159265f * (0.75f + 1.5f * float(v) / 1023.0f);
            dl->AddLine(c, ImVec2(c.x + std::cos(a) * (r - 2), c.y + std::sin(a) * (r - 2)), IM_COL32(245, 245, 245, 255), 2.5f);
            const float tw = ImGui::CalcTextSize(label, le).x;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (std::max)(0.0f, (w - tw) * 0.5f));
            ImGui::TextUnformatted(label, le);
            ImGui::EndGroup();
            ImGui::PopID();
        };
        auto section = [&](const char* title) { ImGui::BeginGroup(); ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1), "%s", title); };
        auto endSection = [&]() { ImGui::EndGroup(); ImGui::SameLine(0, 22); };

        ImGui::SameLine();
        ImGui::Checkbox("Vector panel", &st.vectorPanel);
        if (st.vectorPanel) { ImGui::SameLine(); ImGui::Checkbox("Knobs show the program", &st.knobFollow); ImGui::SameLine(); ImGui::TextDisabled("(Shift+click a key: it stays held)"); }
        if (st.vectorPanel) {
            // VECTOR-PANEL (vector_panel.h): the MS2000R front panel, fitted to the window below this line.
            vio.lit = leds.lit; vio.shown = shown; vio.knobs = st.knob;
            // KNOB-FOLLOW (knob_follow.h): drawn from the edit buffer; a pot being turned keeps the mouse's value
            // until 300 ms after its last move; turning a pot always moves the physical pot (st.knob).
            static uint16_t disp[4][8]; static int lastMoved = -1; static ULONGLONG lastMs = 0;
            if (st.knobFollow && cpu) {
                uint8_t prog[254];
                for (int i = 0; i < 254; ++i) prog[i] = cpu->peekExternal(MS2000::kEditBufferAddr + uint32_t(i));
                uint16_t want[4][8]; bool fol[4][8];
                MS2000::programKnobs(prog, leds.lit[5][2] > 0.5f ? 1 : 0, want, fol);
                const bool held = ImGui::IsMouseDown(0) || GetTickCount64() - lastMs < 300;
                for (int m = 0; m < 4; ++m) for (int x = 0; x < 8; ++x)
                    if (!(held && m * 8 + x == lastMoved)) disp[m][x] = fol[m][x] ? want[m][x] : st.knob[m][x];
                vio.knobs = disp;
            }
            vio.volume = &st.volume; vio.in1 = &st.vr30; vio.in2 = &st.vr31;
            vio.sw = [&cpu, &demoPhase](unsigned c, unsigned r, bool d) { if (demoPhase == 0) if (cpu) cpu->setPanelSwitch(c, r, d); };
            vio.knob = [&cpu, &st](unsigned m, unsigned x, uint16_t v) { st.knob[m][x] = v; if (cpu) cpu->setPanelKnob(m, x, v); lastMoved = int(m * 8 + x); lastMs = GetTickCount64(); };
            const LcdGuiSnapshot lcdSnap = R ? R->getLcdGuiSnapshot() : LcdGuiSnapshot{};
            VPanel::ImGuiBackend vbe;
            vbe.dl = ImGui::GetWindowDrawList(); vbe.f = panelFont ? panelFont : ImGui::GetFont();
            vbe.lcdFn = [&lcdSnap, &rom](ImVec2 a, ImVec2 b) { drawLcdPanel(lcdSnap, rom, a, b); };
            vio.stageChanged = vio.volumeChanged = false;
            const ImVec2 pos = ImGui::GetCursorScreenPos();
            ImVec2 avail = ImGui::GetContentRegionAvail(); avail.y -= 3.2f * ImGui::GetFrameHeightWithSpacing();
            VPanel::draw(vio, vbe, VPanel::V2(pos.x, pos.y), VPanel::V2(avail.x, avail.y));
            const float sc = (std::min)(avail.x / VPanel::kW, avail.y / VPanel::kH);
            ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + VPanel::kH * sc + 6.0f));
            ImGui::Dummy(ImVec2(VPanel::kW * sc, 1.0f));
            if (vio.volumeChanged) gain = outGain();
            if (vio.stageChanged && dsp) dsp->setInputStage(st.vr30, st.vr31, st.mic2);
        } else {
        ImGui::Separator();
        // ---- upper row: OSC1, OSC2, MIXER, FILTER, AMP, ARPEGGIATOR ----
        section("OSCILLATOR 1");
        ImGui::BeginGroup(); sw("WAVE##o1", 0, 7, 60);
        led("SAW", 2, 4); led("SQU", 3, 4); led("TRI", 4, 4); led("SIN/CROSS", 5, 4);
        ImGui::EndGroup(); ImGui::SameLine();
        ImGui::BeginGroup(); ImGui::Dummy(ImVec2(1, ImGui::GetFrameHeight())); led("VOX", 2, 5); led("DWGS", 3, 5); led("NOISE", 4, 5); led("AUDIO IN", 5, 5); ImGui::EndGroup();
        knob("CONTROL 1", 2, 6, false); ImGui::SameLine(); knob("CONTROL 2", 2, 1, false);
        endSection();
        section("OSCILLATOR 2");
        ImGui::BeginGroup(); sw("WAVE##o2", 1, 1, 60); led("SAW", 3, 6); led("SQU", 4, 6); led("TRI", 5, 6); ImGui::EndGroup(); ImGui::SameLine();
        ImGui::BeginGroup(); sw("OSC MOD", 1, 0, 70); led("RING", 2, 7); led("SYNC", 3, 7); ImGui::EndGroup();
        knob("SEMITONE", 2, 7, true); ImGui::SameLine(); knob("TUNE", 2, 0, true);
        endSection();
        section("MIXER");
        knob("OSC1 LVL", 2, 2, false); knob("OSC2 LVL", 2, 4, false); knob("NOISE", 2, 5, false);
        endSection();
        section("FILTER");
        ImGui::BeginGroup(); sw("TYPE", 1, 4, 60); led("24LPF", 2, 2); led("12LPF", 3, 2); led("12BPF", 4, 2); led("12HPF", 2, 3); ImGui::EndGroup();
        knob("CUTOFF", 3, 7, false); ImGui::SameLine(); knob("RESONANCE", 3, 2, false);
        knob("EG1 INT", 3, 4, true); ImGui::SameLine(); knob("KBD TRACK", 3, 6, true);
        endSection();
        section("AMP");
        ImGui::BeginGroup(); sw("DISTORTION", 1, 6, 90); led("DIST", 6, 5); sw("EG2/GATE", 1, 7, 90); led("lit = GATE", 6, 4); ImGui::EndGroup();
        knob("LEVEL", 3, 3, false); ImGui::SameLine(); knob("PANPOT", 3, 1, true);
        endSection();
        section("ARPEGGIATOR");
        ImGui::BeginGroup();
        sw("ON/OFF##arp", 2, 4, 70); ImGui::SameLine(); led("ON", 6, 7);
        sw("LATCH", 2, 3, 70); ImGui::SameLine(); led("LATCH", 5, 7);
        sw("TYPE##arp", 2, 1, 70); ImGui::SameLine(); sw("RANGE", 2, 2, 70);
        ImGui::EndGroup();
        ImGui::BeginGroup(); led("TEMPO", 6, 8); knob("TEMPO", 3, 0, false); ImGui::EndGroup(); ImGui::SameLine();
        ImGui::BeginGroup(); ImGui::Dummy(ImVec2(1, ImGui::GetTextLineHeight())); knob("GATE", 3, 5, false); ImGui::EndGroup();
        ImGui::EndGroup();
        ImGui::NewLine();

        // ---- lower row: KEYBOARD/TIMBRE/MOD SEQ/PORTAMENTO, LFO1, LFO2, EFFECTS, VIRTUAL PATCH, EG1, EG2 ----
        section("TIMBRE / SEQ");
        sw("KEYBOARD", 0, 4, 90); ImGui::SameLine(); led2("KBD", 1, 0);
        sw("TIMBRE SELECT", 1, 2, 110); ImGui::SameLine(); led("T1", 5, 3); ImGui::SameLine(); led("T2", 5, 2);
        // MOD SEQ / SEQ EDIT LEDs where the FIRMWARE drives them (measured, headless SS0/T6 and SS0/T3): ON/OFF =
        // LS7.LD01, SEQ1/LEVEL = LS6.LD01, SEQ2/PAN = LS7.LD00, SEQ3 = LS6.LD00. KOD-A30416 draws these four one
        // row lower (LED30 LS6.LD01, LED21 LS5.LD01, LED20 LS6.LD00, LED19 LS5.LD00) - the drawing and the unit
        // disagree there; a real R shows its SEQ LEDs, so the unit's wiring is the firmware's.
        sw("MOD SEQ ON/OFF", 0, 6, 110); ImGui::SameLine(); led("ON", 7, 1);
        sw("MOD SEQ REC", 1, 5, 110); ImGui::SameLine(); led("REC", 6, 6);
        sw("SEQ EDIT SELECT", 0, 3, 110); led("SEQ1/LEVEL", 6, 1); ImGui::SameLine(); led("SEQ2/PAN", 7, 0); ImGui::SameLine(); led("SEQ3", 6, 0);
        knob("PORTAMENTO", 2, 3, false);
        endSection();
        section("LFO 1");
        sw("WAVE##l1", 3, 5, 60); led("SAW", 4, 3); led("SQU", 7, 3); led("TRI", 7, 4); led("S/H", 7, 5);
        knob("FREQ##1", 1, 4, false);
        endSection();
        section("LFO 2");
        sw("WAVE##l2", 3, 4, 60); led("SAW", 3, 3); led("SQU(+)", 7, 6); led("SIN", 7, 7); led("S/H", 7, 8);
        knob("FREQ##2", 1, 6, false);
        endSection();
        section("EFFECTS");
        // OM p.5: the MOD/DELAY key has an LED, dark = MOD, lit = DELAY. It is LED26 (LS6/LD02), drawn
        // "OCTDOWN" on KOD-A30416 - the keyboard model's label; the R firmware drives it from this key (measured).
        sw("MOD/DELAY", 3, 3, 90); ImGui::SameLine(); led("lit = DELAY", 6, 2);
        knob("SPEED/TIME", 1, 2, false); knob("DEPTH/FB", 1, 5, false);
        endSection();
        section("VIRTUAL PATCH");
        ImGui::BeginGroup(); sw("SELECT", 3, 2, 70); led("1", 0, 8); led("2", 1, 8); led("3", 2, 8); led("4", 3, 8); ImGui::EndGroup(); ImGui::SameLine();
        ImGui::BeginGroup(); sw("SOURCE", 3, 1, 80); led("EG1", 0, 9); led("EG2", 1, 9); led("LFO1", 2, 9); led("LFO2", 3, 9);
        led("VELOCITY", 4, 9); led("KBD TRACK", 5, 9); led("MIDI 1", 6, 9); led("MIDI 2", 7, 9); ImGui::EndGroup(); ImGui::SameLine();
        ImGui::BeginGroup(); sw("DEST", 3, 0, 90); led("PITCH", 0, 10); led("OSC2 PITCH", 1, 10); led("OSC1 CTRL1", 2, 10); led("NOISE LEVEL", 3, 10);
        led("CUTOFF", 4, 10); led("AMP", 5, 10); led("PAN", 6, 10); led("LFO2 FREQ", 7, 10); ImGui::EndGroup(); ImGui::SameLine();
        ImGui::BeginGroup(); knob("PATCH 1", 1, 7, true); knob("PATCH 2", 1, 0, true); ImGui::EndGroup(); ImGui::SameLine();
        ImGui::BeginGroup(); knob("PATCH 3", 1, 3, true); knob("PATCH 4", 1, 1, true); ImGui::EndGroup();
        endSection();
        section("EG1 (FILTER)");
        ImGui::BeginGroup(); knob("ATTACK##1", 0, 4, false); knob("SUSTAIN##1", 0, 2, false); ImGui::EndGroup(); ImGui::SameLine();
        ImGui::BeginGroup(); knob("DECAY##1", 0, 6, false); knob("RELEASE##1", 0, 0, false); ImGui::EndGroup();
        endSection();
        section("EG2 (AMP)");
        ImGui::BeginGroup(); knob("ATTACK##2", 0, 7, false); knob("SUSTAIN##2", 0, 5, false); ImGui::EndGroup(); ImGui::SameLine();
        ImGui::BeginGroup(); knob("DECAY##2", 0, 1, false); knob("RELEASE##2", 0, 3, false); ImGui::EndGroup();
        ImGui::EndGroup();
        ImGui::NewLine();

        // ---- display row: the LCD's keys, ORIGINAL VALUE, BANK, programs 1-16 ----
        section("EDIT");
        sw("EDIT", 3, 7, 60); ImGui::SameLine(); sw("GLOBAL", 3, 6, 60); ImGui::SameLine(); sw("EXIT", 4, 0, 60); ImGui::SameLine(); sw("WRITE", 4, 1, 60);
        sw("PAGE -", 4, 3, 60); ImGui::SameLine(); sw("PAGE +", 4, 2, 60); ImGui::SameLine(); sw("- / NO", 4, 5, 60); ImGui::SameLine(); sw("+ / YES", 4, 4, 60);
        sw("< CURSOR", 4, 7, 60); ImGui::SameLine(); sw("CURSOR >", 4, 6, 60); ImGui::SameLine(); led("ORIGINAL VALUE", 4, 7);
        endSection();
        section("BANK");
        sw("BANK UP", 2, 6, 90); ImGui::SameLine(); led2("", 7, 6);
        sw("BANK DOWN", 2, 7, 90); ImGui::SameLine(); led2("", 5, 4);
        endSection();
        section("PROGRAM");
        for (unsigned k = 0; k < 16; ++k) {
            char lb[8]; snprintf(lb, sizeof lb, "%u", k + 1);
            if (k % 8) ImGui::SameLine();
            ImGui::BeginGroup();
            shown[k / 8][k % 8] = true; ledDot(leds.lit[k / 8][k % 8], 0.0f);
            sw(lb, k < 8 ? 6u : 5u, 7u - (k % 8), 34);   // SS6 T0..T7 = keys 8..1, SS5 T0..T7 = 16..9
            ImGui::EndGroup();
        }
        ImGui::EndGroup();
        }   // VECTOR-PANEL else: the test panel
        if (leds.codecMute) { ImGui::SameLine(); ImGui::TextColored(ImVec4(1, 0.5f, 0.3f, 1), "CODEC_MUTE"); }
        {   // any LED the firmware lights that this panel does not draw - so a wrong mapping shows itself
            std::string other;
            for (unsigned r = 0; r < 8; ++r) for (unsigned c = 0; c < 12; ++c)
                if (!shown[r][c] && leds.lit[r][c] > 0.25f) { char b[16]; snprintf(b, sizeof b, " LS%u.LD%02u", r, c); other += b; }
            if (!other.empty()) ImGui::TextDisabled("lit, not on this panel:%s", other.c_str());
        }
        // Owner's manual p.12 "Listen to the demo performance": hold [EXIT], then press and hold
        // [GLOBAL], about one second. Done here as that key sequence on the switch matrix.
        if (ImGui::Button("Demo songs (EXIT + GLOBAL)") && demoPhase == 0) { demoPhase = 1; demoT0 = GetTickCount64(); }
        {   // MS2K_GUI_REC="<start s>:<length s>" - test: the Record button pressed and released by time
            static double rs = -1, rl = 0; static ULONGLONG t0 = GetTickCount64(); static int ph = 0;
            if (rs < 0) { rs = 1e9; if (const char* e = getenv("MS2K_GUI_REC")) sscanf(e, "%lf:%lf", &rs, &rl); }
            const double now = double(GetTickCount64() - t0) / 1000.0;
            if (ph == 0 && now >= rs) { rec.start(); ph = 1; }
            else if (ph == 1 && now >= rs + rl) { rec.stop(); ph = 2; }
        }
        ImGui::SameLine();
        // GUI-3b: ImGui derives a widget's ID from its label. The stop label changed every frame
        // (the running seconds), so press and release landed on two different IDs and the click
        // never registered. "###recbtn" pins one ID for both states.
        if (!rec.on()) { if (ImGui::Button("Record WAV###recbtn")) rec.start(); }
        else {
            char lbl[64]; snprintf(lbl, sizeof lbl, "Stop recording (%.1f s)###recbtn", rec.seconds());
            if (ImGui::Button(lbl)) rec.stop();
        }
        {   // SYX-1: a .syx into the machine's MIDI IN / all 128 programs out of it (Windows file dialogs)
            auto dlg = [&](bool saving, std::string& out) {
                char file[MAX_PATH] = {};
                OPENFILENAMEA ofn{}; ofn.lStructSize = sizeof ofn; ofn.hwndOwner = hwnd;
                ofn.lpstrFilter = "SysEx (*.syx)\0*.syx\0All files\0*.*\0"; ofn.lpstrFile = file; ofn.nMaxFile = MAX_PATH;
                ofn.lpstrDefExt = "syx";
                ofn.Flags = OFN_NOCHANGEDIR | (saving ? OFN_OVERWRITEPROMPT : (OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST));
                if (!(saving ? GetSaveFileNameA(&ofn) : GetOpenFileNameA(&ofn))) return false;
                out = file; return true;
            };
            ImGui::SameLine();
            std::string f;
            if (ImGui::Button("Load .syx") && !syx.busy() && dlg(false, f)) syx.startImport(f);
            ImGui::SameLine();
            if (ImGui::Button("Save programs .syx") && !syx.busy() && dlg(true, f)) syx.startExport(f);
            syx.poll();
            if (!syx.status().empty()) { ImGui::SameLine(); ImGui::TextDisabled("%s", syx.status().c_str()); }
        }
        if (demoAt > 0 && GetTickCount64() >= demoAt && demoPhase == 0) { demoAt = 0; demoPhase = 1; demoT0 = GetTickCount64(); }
        if (demoPhase) {
            const ULONGLONG dt = GetTickCount64() - demoT0;
            if (demoPhase == 1) { if (cpu) cpu->setPanelSwitch(4, 0, true); demoPhase = 2; }                        // EXIT down
            else if (demoPhase == 2 && dt >= 200) { if (cpu) cpu->setPanelSwitch(3, 6, true); demoPhase = 3; }       // GLOBAL down
            else if (demoPhase == 3 && dt >= 1400) { if (cpu) cpu->setPanelSwitch(3, 6, false); if (cpu) cpu->setPanelSwitch(4, 0, false); demoPhase = 0; }
            ImGui::SameLine(); ImGui::TextDisabled(demoPhase == 2 ? "EXIT held" : demoPhase == 3 ? "EXIT + GLOBAL held" : "");
        }

        ImGui::Separator();
        {   // once a second on stdout: the same measured figures, with the wall clock
            static ULONGLONG t0 = GetTickCount64(), last = 0;
            const ULONGLONG now = GetTickCount64();
            if (dsp && now - last >= 1000) {
                last = now;
                printf("[THIN-GUI] wall=%.1f s emu=%.2f s ring=%.1f ms underrun=%llu overflow=%llu | in: fill=%.1f/%.1f ms underrun=%llu skip=%llu clip=%llu/%llu | shi-ovr=%llu | midi-out delay %.1f ms\n",
                       (now - t0) / 1000.0, double(dsp->txFrames()) / 48000.0, dsp->audioFill() / 48.0,
                       (unsigned long long)audio.underrunFrames(), (unsigned long long)dsp->audioDropped(),
                       dsp->audioInFill(0) / 48.0, dsp->audioInFill(1) / 48.0, (unsigned long long)dsp->audioInUnderruns(), (unsigned long long)dsp->audioInSkips(),
                       (unsigned long long)dsp->audioInClips(0), (unsigned long long)dsp->audioInClips(1), (unsigned long long)dsp->shiOverruns(), moc.lagN.load() ? moc.lagSum.load() / double(moc.lagN.load()) : 0.0);
                fflush(stdout);
            }
        }
        if (dsp) {
            const double emuSec = double(dsp->txFrames()) / 48000.0;
            ImGui::Text("Emulated audio time: %.1f s   ring: %.1f ms   host underrun frames: %llu   ring overflow frames: %llu",
                        emuSec, dsp->audioFill() / 48.0, (unsigned long long)audio.underrunFrames(),
                        (unsigned long long)dsp->audioDropped());
        } else {
            ImGui::TextColored(ImVec4(1, 0.4f, 0.3f, 1), "No DSP (boot ROM missing?) - no audio.");
        }
        ImGui::Text("Audio device: %s%s   MIDI in bytes: %llu   pacing: %s",
                    audio.isOpen() ? "open" : "closed",
                    audio.deviceRate() ? (" (" + std::to_string(audio.deviceRate()) + " Hz mix)").c_str() : "",
                    (unsigned long long)midi.bytesIn(), (R && R->isAudioPaced()) ? "audio clock" : "wall clock");
        const std::string err = audio.lastError();
        if (!err.empty()) ImGui::TextColored(ImVec4(1, 0.4f, 0.3f, 1), "Audio: %s", err.c_str());
        if (!rom.ok) ImGui::TextDisabled("hd44780_a00.bin not found - LCD drawn as text");
        ImGui::End();

        ImGui::Render();
        const float clear[4] = { 0.08f, 0.09f, 0.10f, 1.0f };
        g_ctx->OMSetRenderTargets(1, &g_rtv, nullptr);
        g_ctx->ClearRenderTargetView(g_rtv, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        if (shotAt > 0 && GetTickCount64() >= shotAt) { saveBackBuffer("thin_gui_shot.bmp"); shotAt = 0; }
        g_swap->Present(1, 0);
    }

    saveSettings(st);
    if (R) R->getEmulator().setMidiOutSink(nullptr);
    moc.stop = true; if (moc.th.joinable()) moc.th.join();
    midi.close();
    ain[0].close(); ain[1].close();
    audio.close();
    rec.stop();                                                        // finish the WAV header on exit
    if (R) { R->setAudioPaced(false); R->stop(); }

    timeEndPeriod(1);
    ImGui_ImplDX11_Shutdown(); ImGui_ImplWin32_Shutdown(); ImGui::DestroyContext();
    cleanupDevice();
    DestroyWindow(hwnd);
    UnregisterClass(wc.lpszClassName, wc.hInstance);
    return 0;
}
