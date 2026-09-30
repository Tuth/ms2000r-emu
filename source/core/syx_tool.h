// syx_tool.h - SYX-1 (2026-10-01): .syx import and program-bank export through the machine's own MIDI.
//
// Nothing here writes the machine's memory: a .syx file is sent into the emulated MIDI IN and the FIRMWARE
// receives it and stores it (flash), exactly as the unit does from an editor; its answers on MIDI OUT say how
// it went. Protocol: docs/MS2000_MIDIimp.TXT (KORG MS2000 MIDI Implementation, a text file):
//   - Device Inquiry F0 7E 7F 06 01 F7 ("Any Channel") -> Identity Reply F0 7E 0g 06 02 42 58 00 ..: g = the
//     machine's Global MIDI channel. A dump only reaches it on that channel, so every MS2000 message of the file
//     (F0 42 3x 58 ..) gets 3g before it is sent.
//   - Every dump received -> F0 42 3g 58 23 F7 (DATA LOAD COMPLETED) or 24 (DATA LOAD ERROR, "ex. protect");
//     26 = DATA FORMAT ERROR. The messages are sent one by one, each after the answer to the one before.
//   - Export: PROGRAM DATA DUMP REQUEST F0 42 3g 58 1C F7 -> F0 42 3g 58 4C <37157 bytes> F7 (all 128 programs).
// The emulated SCI1 serialises MIDI IN at 31,250 baud (320 us a byte), so a whole bank takes ~12 s of machine
// time - the queue can be filled at once. Thread use: feed() from the thread that runs the machine (the MIDI OUT
// sink), everything else from one UI thread.
#pragma once
#include <cstdint>
#include <cstdio>
#include <chrono>
#include <functional>
#include <mutex>
#include <string>
#include <vector>
#include <deque>

namespace MS2000 {

class SyxTool {
public:
    using Send = std::function<void(const uint8_t*, size_t)>;
    void setSend(Send s) { m_send = std::move(s); }

    // a byte of the firmware's MIDI OUT (TDR1); only whole SysEx messages are kept
    void feed(uint8_t b)
    {
        if (b >= 0xF8) return;                             // realtime may sit inside SysEx
        std::lock_guard<std::mutex> l(m_mx);
        if (b == 0xF0) { m_cur.assign(1, b); m_in = true; return; }
        if (!m_in) return;
        if (b == 0xF7) { m_cur.push_back(b); m_rx.push_back(std::move(m_cur)); m_cur.clear(); m_in = false; return; }
        if (b & 0x80) { m_in = false; m_cur.clear(); return; }
        m_cur.push_back(b);
    }

    bool busy() const { return m_st != Idle; }
    std::string status() const { return m_status; }

    // Loads a .syx file and starts sending its MS2000 messages. False (and a status) if it has none.
    bool startImport(const std::string& path)
    {
        if (busy()) return false;
        std::vector<uint8_t> f;
        if (FILE* fp = std::fopen(path.c_str(), "rb")) {
            uint8_t buf[4096]; size_t n;
            while ((n = std::fread(buf, 1, sizeof buf, fp)) > 0) f.insert(f.end(), buf, buf + n);
            std::fclose(fp);
        } else { m_status = "Cannot open " + path; return false; }
        m_msgs.clear();
        size_t skipped = 0;
        for (size_t i = 0; i < f.size();) {
            if (f[i] != 0xF0) { ++i; continue; }
            size_t j = i + 1;
            while (j < f.size() && f[j] != 0xF7 && !(f[j] & 0x80)) ++j;
            if (j >= f.size() || f[j] != 0xF7) { ++skipped; i = j; continue; }   // not a whole message
            std::vector<uint8_t> m(f.begin() + ptrdiff_t(i), f.begin() + ptrdiff_t(j) + 1);
            if (m.size() >= 6 && m[1] == 0x42 && (m[2] & 0xF0) == 0x30 && m[3] == 0x58) m_msgs.push_back(std::move(m));
            else ++skipped;
            i = j + 1;
        }
        if (m_msgs.empty()) { m_status = "No MS2000 SysEx in " + path; return false; }
        m_name = baseName(path); m_next = 0; m_skipped = skipped;
        begin(Import);
        return true;
    }

    // Requests all 128 programs and writes them to path when they arrive.
    bool startExport(const std::string& path)
    {
        if (busy()) return false;
        m_exportPath = path; m_name = baseName(path);
        begin(Export);
        return true;
    }

    // Drive it: call a few times a second from the UI thread.
    void poll()
    {
        if (m_st == Idle) return;
        std::deque<std::vector<uint8_t>> rx;
        { std::lock_guard<std::mutex> l(m_mx); rx.swap(m_rx); }
        const double t = now() - m_t0;
        for (auto& m : rx) {
            if (m_st == Inquiry && m.size() >= 8 && m[1] == 0x7E && m[3] == 0x06 && m[4] == 0x02 && m[5] == 0x42 && m[6] == 0x58) {
                m_ch = m[2] & 0x0F;
                if (m_mode == Import) sendNext(); else requestBank();
                return;
            }
            if (m.size() < 5 || m[1] != 0x42 || m[3] != 0x58) continue;
            const uint8_t fn = m[4];
            if (m_st == WaitAck && (fn == 0x23 || fn == 0x24 || fn == 0x26)) {
                if (fn != 0x23) { finish(std::string("The MS2000 answered ") + (fn == 0x24 ? "DATA LOAD ERROR (memory protect on? GLOBAL)" : "DATA FORMAT ERROR")
                                      + " to message " + std::to_string(m_next) + " of " + m_name); return; }
                if (m_next < m_msgs.size()) { sendNext(); return; }
                finish("Loaded " + m_name + ": the MS2000 answered DATA LOAD COMPLETED" + (m_msgs.size() > 1 ? " to all " + std::to_string(m_msgs.size()) + " messages" : "")
                       + (m_skipped ? " (" + std::to_string(m_skipped) + " other messages left out)" : ""));
                return;
            }
            if (m_st == WaitDump && fn == 0x4C) {
                bool ok = false;
                if (FILE* fp = std::fopen(m_exportPath.c_str(), "wb")) { ok = std::fwrite(m.data(), 1, m.size(), fp) == m.size(); std::fclose(fp); }
                finish(ok ? "Saved all 128 programs to " + m_name + " (" + std::to_string(m.size()) + " bytes)" : "Could not write " + m_exportPath);
                return;
            }
            if (m_st == WaitDump && fn == 0x24) { finish("The MS2000 answered DATA LOAD ERROR to the dump request"); return; }
        }
        if (m_st == Inquiry && t > 3.0) { finish("No answer to the Device Inquiry - is the machine running?"); return; }
        if ((m_st == WaitAck || m_st == WaitDump) && t > m_deadline) { finish("No answer from the MS2000 in time (" + m_name + ")"); return; }
        if (m_st == WaitAck) { char b[160]; std::snprintf(b, sizeof b, "Loading %s: message %zu of %zu, %.0f of ~%.0f s",
                                                          m_name.c_str(), m_next, m_msgs.size(), t, m_expect); m_status = b; }
        if (m_st == WaitDump) { char b[160]; std::snprintf(b, sizeof b, "Receiving the programs: %.0f of ~12 s", t); m_status = b; }
    }

private:
    enum Mode { Import, Export };
    enum St { Idle, Inquiry, WaitAck, WaitDump };
    static double now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
    static std::string baseName(const std::string& p) { const auto k = p.find_last_of("\\/"); return k == std::string::npos ? p : p.substr(k + 1); }
    void send(const std::vector<uint8_t>& m) { if (m_send) m_send(m.data(), m.size()); }
    void begin(Mode md)
    {
        { std::lock_guard<std::mutex> l(m_mx); m_rx.clear(); }
        m_mode = md; m_st = Inquiry; m_t0 = now();
        m_status = "Asking the MS2000 for its MIDI channel...";
        send({ 0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7 });
    }
    void sendNext()
    {
        auto m = m_msgs[m_next++];
        m[2] = uint8_t(0x30 | m_ch);
        m_expect = double(m.size()) * 10.0 / 31250.0;                // the line time
        m_deadline = m_expect + 10.0; m_t0 = now(); m_st = WaitAck;
        send(m);
    }
    void requestBank()
    {
        m_deadline = 30.0; m_t0 = now(); m_st = WaitDump;
        send({ 0xF0, 0x42, uint8_t(0x30 | m_ch), 0x58, 0x1C, 0xF7 });
    }
    void finish(const std::string& s) { m_status = s; m_st = Idle; }

    Send m_send;
    std::mutex m_mx;
    std::vector<uint8_t> m_cur; bool m_in = false;
    std::deque<std::vector<uint8_t>> m_rx;
    Mode m_mode = Import; St m_st = Idle;
    std::vector<std::vector<uint8_t>> m_msgs; size_t m_next = 0, m_skipped = 0;
    uint8_t m_ch = 0;
    double m_t0 = 0, m_deadline = 0, m_expect = 0;
    std::string m_status, m_name, m_exportPath;
};

} // namespace MS2000
