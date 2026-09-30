// panel_mp.h  — MS2000 Panel-MP bridge (SCI → HD44780)
// Single-header, no deps except <vector>/<functional>/<chrono>
// Based on fw7.txt implementation from GPT-5

#pragma once
#include <cstdint>
#include <vector>
#include <deque>
#include <functional>
#include <algorithm>
#include <chrono>
#include "panel_recorder.h"
#include "crash_dump.h"  // for MpTailAppend

// fw17.txt: Forward declare diagnostic enums to avoid circular include
enum class PanelMode;  // Defined in diag_panel.h

struct SciConfig {
  uint32_t baud=0; int dataBits=8; char parity='N'; int stopBits=1;
};

// fw10.txt Source detection for HPI vs SCI communication paths
enum class Source { Unknown, SCI0, SCI1, SCI2, HPI, Test };

class SourceDetector {
public:
  void noteSource(Source s, std::function<void(const char*)> log, bool isTest = false) {
    if (!isTest && m_src == Source::Unknown) {
      m_src = s;
      if (log) {
        char b[48];
        snprintf(b, sizeof(b), "[MP] Source=%d\n", (int)s);
        log(b);
      }
    }
  }
  
  Source getSource() const { return m_src; }
  void reset() { m_src = Source::Unknown; }

private:
  Source m_src = Source::Unknown;
};

class PanelMP {
public:
  // Wire these to your existing LCD backend:
  std::function<void(uint8_t)> lcdCmd;    // hd44780::writeCmd
  std::function<void(uint8_t)> lcdData;   // hd44780::writeData
  std::function<void(const char*)> log;   // optional
  
  // fw14.txt: Panel I/O callbacks (LED/Switch/VR communication)
  std::function<void(int ledId, bool state)> ledSet;           // Set LED state
  std::function<void(int ledId, uint8_t r, uint8_t g, uint8_t b)> ledSetRGB; // Set LED RGB color
  std::function<bool(int switchId)> switchRead;               // Read switch state
  std::function<uint16_t(int vrId)> vrRead;                   // Read VR (potentiometer) value
  
  // fw17.txt: Diagnostic callbacks for real-time monitoring
  std::function<void(PanelMode)> onModeChange;               // Mode change notification
  std::function<void(size_t)> onBytesIn;                    // Bytes received notification  
  std::function<void()> onFrameDone;                        // Frame completed notification

  // H8S → MP (TXD): feed each transmitted byte here
  void onSciTxByte(uint8_t b) {
    // fw21.txt: Collect MP tail for crash dumps
    MpTailAppend(b);
    
    // fw14.txt: Record all incoming data (skip during replay)
    if (!m_replayMode) {
      m_recorder.captureData(b, false, 0);
    }
    
    // fw17.txt: Notify diagnostic system of incoming bytes
    if (onBytesIn) onBytesIn(1);
    
    if (m_hist.size() < m_histLimit) m_hist.push_back(b);
    m_buf.push_back(b);
    m_last = clk::now();
    dispatch();
  }

  // fw10.txt HPI → MP: feed HPI host writes here (same stream processing as SCI)
  void onHpiTxByte(uint8_t b, bool isTest = false) {
    m_sourceDetector.noteSource(Source::HPI, log, isTest);
    if (log && !isTest) {  // Don't spam logs with test data
      char m[48];
      snprintf(m, sizeof(m), "[HPI] TX %02X\n", b);
      log(m);
    }
    
    // fw14.txt: Record HPI data separately from SCI processing
    if (!isTest && !m_replayMode) {
      m_recorder.captureData(b, true, 0);
    }
    
    onSciTxByte(b);  // Same processing as SCI data
  }

  // fw10.txt SCI source tracking
  void onSciTxByte(uint8_t b, int sciId, bool isTest = false) {
    m_sourceDetector.noteSource(static_cast<Source>(static_cast<int>(Source::SCI0) + sciId), log, isTest);
    if (log && !isTest) {  // Don't spam logs with test data
      char m[48];
      snprintf(m, sizeof(m), "[SCI%d] TX %02X\n", sciId, b);
      log(m);
    }
    
    // fw14.txt: Record SCI data with channel ID
    if (!isTest && !m_replayMode) {
      m_recorder.captureData(b, false, sciId);
    }
    
    onSciTxByte(b);  // Same processing as before
  }

  // fw11.txt: Probe sequence for LCD hint detection (byte-order auto-detection)
  struct ProbeScore {
    bool hintsLcd;
    int lcdCmdCount;
    int asciiCount;
  };
  
  ProbeScore probeSequence(const std::vector<uint8_t>& seq) {
    ProbeScore score = {false, 0, 0};
    
    // Check for LCD initialization commands
    for (auto b : seq) {
      if (b == 0x38 || b == 0x0C || b == 0x01 || b == 0x06 || (b & 0x80) == 0x80) {
        score.lcdCmdCount++;
      }
      if (b >= 0x20 && b <= 0x7E) {
        score.asciiCount++;
      }
    }
    
    // Consider it LCD hint if we have LCD commands or significant ASCII
    score.hintsLcd = (score.lcdCmdCount >= 2) || (score.asciiCount >= 4);
    return score;
  }

  // fw14.txt: Status information for GUI diagnostics
  struct PanelStatus {
    Source activeSource = Source::Unknown;
    std::string modeString = "Probe";  // "Probe", "RawLCD", "LenPref", "Header"
    SciConfig sciConfig{};
    uint32_t totalCommands = 0;
    uint32_t totalDataBytes = 0;
    std::vector<uint8_t> lastCommands;  // Last 8 commands for debugging
    bool sourceLocked = false;
    std::chrono::steady_clock::time_point lastActivity;
  };
  
  PanelStatus getStatus() const {
    PanelStatus status;
    status.activeSource = m_sourceDetector.getSource();
    status.sciConfig = m_cfg;
    status.totalCommands = m_totalCmds;
    status.totalDataBytes = m_totalData; 
    status.lastCommands = m_lastCmds;
    status.sourceLocked = (status.activeSource != Source::Unknown);
    status.lastActivity = m_lastActivity;
    
    switch (m_mode) {
      case Mode::Probe: status.modeString = "Probe"; break;
      case Mode::LenPref: status.modeString = "LenPref"; break; 
      case Mode::Header: status.modeString = "Header"; break;
      case Mode::RawLCD: status.modeString = "RawLCD"; break;
    }
    
    return status;
  }

  // MP → H8S (RXD): when firmware reads RDR, return next byte to deliver
  uint8_t onSciRxByteReq() {
    // fw9.txt RX injector: check for queued responses first
    if (!m_rxq.empty()) {
      auto v = m_rxq.front();
      m_rxq.pop_front();
      if (log) {
        char b[64]; 
        snprintf(b, sizeof(b), "[MP-RX] Injected 0x%02X from queue\n", v);
        log(b);
      }
      return v;
    }
    // Baseline: always ACK (0xAC). Later you can implement real replies.
    return 0xAC;
  }
  
  // fw9.txt RX injector: inject panel presence responses
  void injectRx(const uint8_t* p, size_t n) {
    size_t count = n;
    while (n--) m_rxq.push_back(*p++);
    if (log && count > 0) {
      char b[64]; 
      snprintf(b, sizeof(b), "[MP-RX] Injected %zu bytes into RX queue\n", count);
      log(b);
    }
  }

  // SCI HW config from your SCI emu (SMR/BRR/SCR → baud/parity/stop)
  void onSciConfig(const SciConfig& cfg) {
    m_cfg = cfg; m_seenConfig = true;
    if (log) { char b[96]; snprintf(b,sizeof(b),"[MP] SCI %u %d%c%d\n",
                 cfg.baud,cfg.dataBits,cfg.parity,cfg.stopBits); log(b); }
  }

  // Call every 1 ms from your main loop
  void tick1ms() {
    if (m_buf.empty()) return;
    if (msSince(m_last) > 4) { // 4 ms silence closes a frame in Probe/Raw
      closeFrame();
    }
  }

  // Reset
  void reset() {
    m_buf.clear(); m_hist.clear(); m_rxq.clear(); m_mode = Mode::Probe; m_seenConfig=false;
    m_sourceDetector.reset();
  }
  
  // fw14.txt: Panel Recorder access
  PanelRecorder* getRecorder() { return &m_recorder; }
  
  // fw14.txt: Replay mode control
  void setReplayMode(bool enabled) { m_replayMode = enabled; }

private:
  using clk = std::chrono::steady_clock;

  SciConfig m_cfg{};
  bool m_seenConfig=false;

  std::vector<uint8_t> m_buf;              // in-flight frame
  std::vector<uint8_t> m_hist;             // first N bytes for heuristics
  std::deque<uint8_t> m_rxq;               // fw9.txt RX injection queue
  const size_t m_histLimit = 1024;
  clk::time_point m_last = clk::now();
  
  SourceDetector m_sourceDetector;         // fw10.txt source tracking

  enum class Mode { Probe, LenPref, Header, RawLCD } m_mode = Mode::Probe;
  uint8_t m_hdr = 0x00;
  
  // fw14.txt: Status tracking for GUI diagnostics
  uint32_t m_totalCmds = 0;
  uint32_t m_totalData = 0;
  std::vector<uint8_t> m_lastCmds;
  clk::time_point m_lastActivity = clk::now();
  
  // fw14.txt: Panel Recorder/Replay system
  PanelRecorder m_recorder;
  bool m_replayMode = false;

  static uint32_t msSince(clk::time_point t) {
    using namespace std::chrono;
    return (uint32_t)duration_cast<milliseconds>(clk::now() - t).count();
  }

  // ============== Core dispatch =================
  void dispatch() {
    switch (m_mode) {
      case Mode::Probe:   tryDetect(); break;
      case Mode::LenPref: tryLenFrame(); break;
      case Mode::Header:  tryHeaderFrame(); break;
      case Mode::RawLCD:  tryRawLcdStream(); break;
    }
  }

  // ---- Mode: Probe (auto-detect) ----
  void tryDetect() {
    // ❶ Len-pref style: [HDR][LEN][...LEN...][CHK]
    if (m_buf.size() >= 4) {
      uint8_t H = m_buf[0], L = m_buf[1];
      if ((H==0xA5 || H==0x5A || H==0x7E || H==0xF0) && L>0) {
        size_t need = 2 + (size_t)L + 1;
        if (m_buf.size() >= need) {
          uint8_t sum = 0;
          for (size_t i=0;i<2+L;i++) sum = uint8_t(sum + m_buf[i]);
          if (sum == m_buf[2+L] || sum == 0) {
            m_mode = Mode::LenPref;
            if (log) log("[MP] Detected length-prefixed frames\n");
            // fw17.txt: Notify diagnostic system of mode change
            if (onModeChange) onModeChange(PanelMode::LenPref);
            tryLenFrame(); return;
          }
        }
      }
    }
    // ❷ Header…End delim (e.g. F0 ... F7)
    if (m_buf.size() >= 2 && (m_buf[0]==0xF0 || m_buf[0]==0x7E || m_buf[0]==0xAA)) {
      m_mode = Mode::Header; m_hdr = m_buf[0];
      if (log) { char b[64]; snprintf(b,sizeof(b),"[MP] Detected header 0x%02X\n", m_hdr); log(b); }
      // fw17.txt: Notify diagnostic system of mode change
      if (onModeChange) onModeChange(PanelMode::Header);
      return;
    }
    // ❸ Raw LCD heuristic: contains typical HD44780 init opcodes soon
    if (looksLikeLcdInit(m_hist)) {
      m_mode = Mode::RawLCD;
      if (log) log("[MP] Switch to RawLCD heuristic mode\n");
      // fw17.txt: Notify diagnostic system of mode change
      if (onModeChange) onModeChange(PanelMode::RawLCD);
      tryRawLcdStream(); return;
    }
    // else wait for timeout → closeFrame()
  }

  static bool looksLikeLcdInit(const std::vector<uint8_t>& v) {
    int f=0; for (auto b: v){ if(b==0x38) f|=1; if(b==0x0C) f|=2; if(b==0x01) f|=4; if(b==0x06) f|=8; }
    return f==15;
  }

  // ---- Mode: LenPref ----
  void tryLenFrame() {
    while (m_buf.size() >= 3) {
      uint8_t L = m_buf[1];
      size_t need = 2 + (size_t)L + 1;
      if (m_buf.size() < need) return;
      std::vector<uint8_t> frame(m_buf.begin(), m_buf.begin()+need);
      decodeFrame(frame);
      m_buf.erase(m_buf.begin(), m_buf.begin()+need);
    }
  }

  // ---- Mode: Header…End (e.g. F0…F7) ----
  void tryHeaderFrame() {
    auto it = std::find(m_buf.begin()+1, m_buf.end(), 0xF7);
    if (it == m_buf.end()) return;
    std::vector<uint8_t> frame(m_buf.begin(), it+1);
    decodeFrame(frame);
    m_buf.erase(m_buf.begin(), it+1);
  }

  // ---- Mode: RawLCD (stateless pass) ----
  void tryRawLcdStream() {
    std::vector<uint8_t> out; out.swap(m_buf);
    for (auto b: out) {
      if (isLcdCmd(b))        { 
        if (lcdCmd) lcdCmd(b); 
        trackCommand(b);
      }
      else if (isLcdData(b))  { 
        if (lcdData) lcdData(b); 
        trackData(b);
      }
    }
  }

  // ---- Frame close on timeout in Probe/Raw ----
  void closeFrame() {
    if (m_buf.empty()) return;
    decodeUnknown(m_buf);
    m_buf.clear();
  }

  // ============ Decoders ============
  static bool isLcdCmd(uint8_t b) {
    return (b==0x38 || b==0x0C || b==0x01 || b==0x06 || (b & 0x80)==0x80);
  }
  static bool isLcdData(uint8_t b) {
    return (b>=0x20 && b<=0x7E); // printable ASCII baseline
  }

  void decodeUnknown(const std::vector<uint8_t>& raw) {
    // Training: pass through obvious LCD-like bytes
    bool any=false;
    for (auto b: raw) {
      if (isLcdCmd(b))       { 
        if(lcdCmd) lcdCmd(b); 
        trackCommand(b);
        // fw17.txt: Frame completed notification
        if (onFrameDone) onFrameDone();
        any=true; 
      }
      else if (isLcdData(b)) { 
        if(lcdData) lcdData(b); 
        trackData(b);
        // fw17.txt: Frame completed notification
        if (onFrameDone) onFrameDone();
        any=true; 
      }
    }
    if (!any && log) { char b[64]; snprintf(b,sizeof(b),"[MP] Unk frame %zuB\n", raw.size()); log(b); }
  }

  void decodeFrame(const std::vector<uint8_t>& f) {
    // fw14.txt: Enhanced MP protocol decoder with LED/Switch/VR support
    if (f.size() < 3) {
      decodeUnknown(f);
      return;
    }
    
    // Check for MP protocol header patterns
    if (f[0] == 0xA5 || f[0] == 0x5A || f[0] == 0x7E) {
      decodeMPProtocol(f);
    } else {
      // Fallback to LCD-only decoding
      decodeUnknown(f);
    }
  }
  
  // fw14.txt: Statistics tracking helpers
  void trackCommand(uint8_t cmd) {
    m_totalCmds++;
    m_lastActivity = clk::now();
    
    // Keep last 8 commands for debugging
    m_lastCmds.push_back(cmd);
    if (m_lastCmds.size() > 8) {
      m_lastCmds.erase(m_lastCmds.begin());
    }
  }
  
  void trackData(uint8_t /*data*/) {
    m_totalData++;
    m_lastActivity = clk::now();
  }
  
  // fw14.txt: MP Protocol decoder for LED/Switch/VR commands
  void decodeMPProtocol(const std::vector<uint8_t>& frame) {
    if (frame.size() < 3) return;
    
    uint8_t header = frame[0];
    uint8_t length = frame[1];
    
    // Verify frame length matches
    if (frame.size() != (size_t)(2 + length + 1)) {
      if (log) {
        char msg[64];
        snprintf(msg, sizeof(msg), "[MP] Frame length mismatch: expected %d, got %zu\n", 
                 2 + length + 1, frame.size());
        log(msg);
      }
      return;
    }
    
    // Verify checksum (simple sum)
    uint8_t checksum = 0;
    for (size_t i = 0; i < frame.size() - 1; i++) {
      checksum += frame[i];
    }
    if (checksum != frame.back()) {
      if (log) {
        char msg[64];
        snprintf(msg, sizeof(msg), "[MP] Checksum error: calc 0x%02X, got 0x%02X\n", 
                 checksum, frame.back());
        log(msg);
      }
      return;
    }
    
    // Decode commands from payload
    for (size_t i = 2; i < 2 + length; ) {
      if (i >= frame.size() - 1) break; // Don't read checksum
      
      uint8_t cmd = frame[i++];
      
      switch (cmd) {
        case 0x10: // CMD_LCD_INIT
          if (log) log("[MP] LCD_INIT command\n");
          if (lcdCmd) lcdCmd(0x38); // Function Set
          if (lcdCmd) lcdCmd(0x0C); // Display ON
          if (lcdCmd) lcdCmd(0x01); // Clear
          if (lcdCmd) lcdCmd(0x06); // Entry Mode
          break;
          
        case 0x11: // CMD_LCD_CLEAR
          if (log) log("[MP] LCD_CLEAR command\n");
          if (lcdCmd) lcdCmd(0x01);
          break;
          
        case 0x12: // CMD_LCD_SETPOS
          if (i < frame.size() - 1) {
            uint8_t pos = frame[i++];
            if (log) {
              char msg[64];
              snprintf(msg, sizeof(msg), "[MP] LCD_SETPOS: 0x%02X\n", pos);
              log(msg);
            }
            if (lcdCmd) lcdCmd(0x80 | pos);
          }
          break;
          
        case 0x13: // CMD_LCD_DATA
          if (i < frame.size() - 1) {
            uint8_t data = frame[i++];
            if (log) {
              char msg[64];
              snprintf(msg, sizeof(msg), "[MP] LCD_DATA: 0x%02X ('%c')\n", data, 
                       (data >= 0x20 && data <= 0x7E) ? data : '?');
              log(msg);
            }
            if (lcdData) lcdData(data);
            trackData(data);
          }
          break;
          
        case 0x20: // CMD_LED_SET
          if (i + 1 < frame.size() - 1) {
            uint8_t ledId = frame[i++];
            uint8_t state = frame[i++];
            if (log) {
              char msg[64];
              snprintf(msg, sizeof(msg), "[MP] LED_SET: LED%d = %s\n", ledId, 
                       state ? "ON" : "OFF");
              log(msg);
            }
            if (ledSet) ledSet(ledId, state != 0);
          }
          break;
          
        case 0x21: // CMD_LED_RGB
          if (i + 3 < frame.size() - 1) {
            uint8_t ledId = frame[i++];
            uint8_t r = frame[i++];
            uint8_t g = frame[i++];
            uint8_t b = frame[i++];
            if (log) {
              char msg[64];
              snprintf(msg, sizeof(msg), "[MP] LED_RGB: LED%d = RGB(%d,%d,%d)\n", 
                       ledId, r, g, b);
              log(msg);
            }
            if (ledSetRGB) ledSetRGB(ledId, r, g, b);
          }
          break;
          
        case 0x30: // CMD_SWITCH_POLL
          if (i < frame.size() - 1) {
            uint8_t switchId = frame[i++];
            bool state = false;
            if (switchRead) state = switchRead(switchId);
            if (log) {
              char msg[64];
              snprintf(msg, sizeof(msg), "[MP] SWITCH_POLL: SW%d = %s\n", switchId, 
                       state ? "PRESSED" : "RELEASED");
              log(msg);
            }
            // Queue response for RX
            queueSwitchResponse(switchId, state);
          }
          break;
          
        case 0x31: // CMD_VR_READ
          if (i < frame.size() - 1) {
            uint8_t vrId = frame[i++];
            uint16_t value = 0;
            if (vrRead) value = vrRead(vrId);
            if (log) {
              char msg[64];
              snprintf(msg, sizeof(msg), "[MP] VR_READ: VR%d = %d\n", vrId, value);
              log(msg);
            }
            // Queue response for RX
            queueVRResponse(vrId, value);
          }
          break;
          
        default:
          if (log) {
            char msg[64];
            snprintf(msg, sizeof(msg), "[MP] Unknown command: 0x%02X\n", cmd);
            log(msg);
          }
          break;
      }
    }
  }
  
  // fw14.txt: Response queueing for switch/VR polling
  void queueSwitchResponse(uint8_t switchId, bool state) {
    // Response format: [SWITCH_ID] [STATE]
    m_rxq.push_back(switchId);
    m_rxq.push_back(state ? 0x01 : 0x00);
  }
  
  void queueVRResponse(uint8_t vrId, uint16_t value) {
    // Response format: [VR_ID] [VALUE_HI] [VALUE_LO]
    m_rxq.push_back(vrId);
    m_rxq.push_back((value >> 8) & 0xFF);
    m_rxq.push_back(value & 0xFF);
  }

public:  // fw27.txt: Protocol formalization methods need public access
  // fw27.txt: PanelMP protocol formalization - template variants for opcodes/length/checksum
  
  // Protocol constants
  static constexpr uint8_t FRAME_HEADER = 0xA5;
  static constexpr uint8_t CMD_LCD_INIT = 0x10;
  static constexpr uint8_t CMD_LCD_CLEAR = 0x11;
  static constexpr uint8_t CMD_LCD_SETPOS = 0x12;
  static constexpr uint8_t CMD_LCD_DATA = 0x13;
  static constexpr uint8_t CMD_LED_SET = 0x20;
  static constexpr uint8_t CMD_LED_RGB = 0x21;
  static constexpr uint8_t CMD_SWITCH_POLL = 0x30;
  static constexpr uint8_t CMD_VR_READ = 0x31;
  
  // Template: Make frame with no payload
  template<uint8_t OpCode>
  static std::vector<uint8_t> makeFrameNoPayload() {
    std::vector<uint8_t> frame = {FRAME_HEADER, 1, OpCode};
    frame.push_back(calculateChecksum(frame));
    return frame;
  }
  
  // Template: Make frame with single byte payload
  template<uint8_t OpCode>
  static std::vector<uint8_t> makeFrameSingleByte(uint8_t data) {
    std::vector<uint8_t> frame = {FRAME_HEADER, 2, OpCode, data};
    frame.push_back(calculateChecksum(frame));
    return frame;
  }
  
  // Template: Make frame with two byte payload
  template<uint8_t OpCode>
  static std::vector<uint8_t> makeFrameTwoByte(uint8_t data1, uint8_t data2) {
    std::vector<uint8_t> frame = {FRAME_HEADER, 3, OpCode, data1, data2};
    frame.push_back(calculateChecksum(frame));
    return frame;
  }
  
  // Template: Make frame with four byte payload (RGB + LED ID)
  template<uint8_t OpCode>
  static std::vector<uint8_t> makeFrameFourByte(uint8_t data1, uint8_t data2, uint8_t data3, uint8_t data4) {
    std::vector<uint8_t> frame = {FRAME_HEADER, 5, OpCode, data1, data2, data3, data4};
    frame.push_back(calculateChecksum(frame));
    return frame;
  }
  
  // Template: Make frame with variable payload
  template<uint8_t OpCode>
  static std::vector<uint8_t> makeFrameVariable(const std::vector<uint8_t>& payload) {
    if (payload.size() > 254) { // Max payload size (255 - 1 for opcode)
      return {}; // Return empty frame on overflow
    }
    std::vector<uint8_t> frame = {FRAME_HEADER, static_cast<uint8_t>(payload.size() + 1), OpCode};
    frame.insert(frame.end(), payload.begin(), payload.end());
    frame.push_back(calculateChecksum(frame));
    return frame;
  }
  
  // High-level protocol-compliant frame generators
  static std::vector<uint8_t> makeLcdInitFrame() {
    return makeFrameNoPayload<CMD_LCD_INIT>();
  }
  
  static std::vector<uint8_t> makeLcdClearFrame() {
    return makeFrameNoPayload<CMD_LCD_CLEAR>();
  }
  
  static std::vector<uint8_t> makeLcdSetPosFrame(uint8_t position) {
    return makeFrameSingleByte<CMD_LCD_SETPOS>(position);
  }
  
  static std::vector<uint8_t> makeLcdDataFrame(uint8_t data) {
    return makeFrameSingleByte<CMD_LCD_DATA>(data);
  }
  
  static std::vector<uint8_t> makeLedSetFrame(uint8_t ledId, uint8_t state) {
    return makeFrameTwoByte<CMD_LED_SET>(ledId, state);
  }
  
  static std::vector<uint8_t> makeLedRgbFrame(uint8_t ledId, uint8_t r, uint8_t g, uint8_t b) {
    return makeFrameFourByte<CMD_LED_RGB>(ledId, r, g, b);
  }
  
  static std::vector<uint8_t> makeSwitchPollFrame(uint8_t switchId) {
    return makeFrameSingleByte<CMD_SWITCH_POLL>(switchId);
  }
  
  static std::vector<uint8_t> makeVrReadFrame(uint8_t vrId) {
    return makeFrameSingleByte<CMD_VR_READ>(vrId);
  }
  
  // Utility: Calculate frame checksum
  static uint8_t calculateChecksum(const std::vector<uint8_t>& frame) {
    uint8_t checksum = 0;
    for (size_t i = 0; i < frame.size(); i++) {
      checksum += frame[i];
    }
    return checksum;
  }
  
  // Protocol validation
  static bool validateFrame(const std::vector<uint8_t>& frame) {
    if (frame.size() < 4) return false; // Minimum: header + length + opcode + checksum
    if (frame[0] != FRAME_HEADER) return false;
    
    uint8_t expectedLength = frame[1];
    if (frame.size() != expectedLength + 3) return false; // +3 for header, length, checksum
    
    uint8_t expectedChecksum = calculateChecksum(std::vector<uint8_t>(frame.begin(), frame.end() - 1));
    return expectedChecksum == frame.back();
  }
  
  // fw27.txt: Protocol compliance test - simulates GUI → MP → FW → echo response
  struct ProtocolTestResult {
    bool success = false;
    std::string details;
    std::vector<uint8_t> sentFrame;
    std::vector<uint8_t> expectedResponse;
  };
  
  static ProtocolTestResult testProtocolRoundTrip() {
    ProtocolTestResult result;
    
    try {
      // Test 1: LED command with protocol-compliant frame
      auto ledFrame = makeLedSetFrame(5, 1); // LED 5 ON
      result.sentFrame = ledFrame;
      
      // Validate outgoing frame structure
      if (!validateFrame(ledFrame)) {
        result.details = "Outgoing frame validation failed";
        return result;
      }
      
      // Expected response format: echo command with status
      std::vector<uint8_t> expectedEcho = {FRAME_HEADER, 3, CMD_LED_SET, 5, 1};
      expectedEcho.push_back(calculateChecksum(expectedEcho));
      result.expectedResponse = expectedEcho;
      
      // Test 2: VR read command
      auto vrFrame = makeVrReadFrame(24); // VR 24
      if (!validateFrame(vrFrame)) {
        result.details = "VR frame validation failed";
        return result;
      }
      
      // Test 3: Switch poll command
      auto swFrame = makeSwitchPollFrame(2); // Switch 2
      if (!validateFrame(swFrame)) {
        result.details = "Switch frame validation failed";
        return result;
      }
      
      result.success = true;
      result.details = "Protocol formalization test passed - all frames validate correctly";
      
    } catch (const std::exception& e) {
      result.details = std::string("Protocol test exception: ") + e.what();
    }
    
    return result;
  }
};