// fw17.txt: Diagnostic panel system for real-time MS2000 system monitoring
// Based on GPT5 specifications for developer radar functionality
#pragma once
#include <mutex>
#include <array>
#include <cstdint>
#include <string>

struct SciDiag {
    bool present = false;
    uint32_t baud = 0;
    char parity = 'N';
    int dataBits = 8, stopBits = 1;
    bool txEnabled = false, syncMode = false; // TE, CM
    uint64_t txBytes = 0;
};

enum class PanelSrc { Unknown, SCI0, SCI1, SCI2, HPI };
enum class PanelMode { Probe, LenPref, Header, RawLCD };
enum class HpiOrder { Auto, HiLo, LoHi };

struct BootProbeDiag {
    bool fn = false, on = false, clr = false, ent = false, ddram = false;
    uint32_t dataCount = 0;
};

struct DiagSnapshot {
    PanelSrc   src = PanelSrc::Unknown;
    PanelMode  mode = PanelMode::Probe;
    HpiOrder   hpiOrder = HpiOrder::Auto;
    SciDiag    sci[3];
    uint64_t   mpBytes = 0, mpFrames = 0;
    BootProbeDiag boot{};
    std::string notes; // free text notes
    
    // fw17.txt: Additional diagnostic info
    bool recording = false;
    bool replaying = false;
    uint64_t deterministicSeed = 0;
    uint64_t tickCount = 0;
    
    // fw20.txt: Async log diagnostic info
    uint64_t droppedLogs = 0;
    
    // i17.txt: IRQ diagnostic counters
    uint64_t irqServicedCount = 0;
    int irqLastVector = -1;
    bool irqInService = false;
    uint8_t exrI = 0;  // EXR.I bit status
    uint8_t ccrI = 0;  // CCR.I bit status
};

class DiagState {
public:
    void setPanelSrc(PanelSrc s) { 
        std::lock_guard<std::mutex> lk(m_); 
        src_ = s; 
    }
    
    void setPanelMode(PanelMode m) { 
        std::lock_guard<std::mutex> lk(m_); 
        mode_ = m; 
    }
    
    void setHpiOrder(HpiOrder o) { 
        std::lock_guard<std::mutex> lk(m_); 
        hpiOrder_ = o; 
    }
    
    void addMpBytes(size_t n) { 
        std::lock_guard<std::mutex> lk(m_); 
        mpBytes_ += n; 
    }
    
    void incMpFrames() { 
        std::lock_guard<std::mutex> lk(m_); 
        mpFrames_++; 
    }
    
    void setSci(int i, const SciDiag& d) { 
        std::lock_guard<std::mutex> lk(m_); 
        if (i >= 0 && i < 3) sci_[i] = d; 
    }
    
    void setBoot(const BootProbeDiag& b) { 
        std::lock_guard<std::mutex> lk(m_); 
        boot_ = b; 
    }
    
    void addNote(std::string n) { 
        std::lock_guard<std::mutex> lk(m_); 
        notes_ = std::move(n); 
    }
    
    void setRecording(bool r) {
        std::lock_guard<std::mutex> lk(m_);
        recording_ = r;
    }
    
    void setReplaying(bool r) {
        std::lock_guard<std::mutex> lk(m_);
        replaying_ = r;
    }
    
    void setDeterministicState(uint64_t seed, uint64_t tickCount) {
        std::lock_guard<std::mutex> lk(m_);
        deterministicSeed_ = seed;
        tickCount_ = tickCount;
    }

    void setRecordingFile(const std::string& filename) {
        std::lock_guard<std::mutex> lk(m_);
        recordingFile_ = filename;
    }

    // fw20.txt: Set dropped logs count
    void setDroppedLogs(uint64_t count) {
        std::lock_guard<std::mutex> lk(m_);
        droppedLogs_ = count;
    }
    
    // i17.txt: IRQ diagnostic methods
    void incIrqServiced() {
        std::lock_guard<std::mutex> lk(m_);
        irqServicedCount_++;
    }
    
    void setIrqLastVector(int vec) {
        std::lock_guard<std::mutex> lk(m_);
        irqLastVector_ = vec;
    }
    
    void setIrqInService(bool inService) {
        std::lock_guard<std::mutex> lk(m_);
        irqInService_ = inService;
    }
    
    void setCpuFlags(uint8_t exrI, uint8_t ccrI) {
        std::lock_guard<std::mutex> lk(m_);
        exrI_ = exrI;
        ccrI_ = ccrI;
    }

    DiagSnapshot snapshot() const {
        std::lock_guard<std::mutex> lk(m_);
        DiagSnapshot s;
        s.src = src_;
        s.mode = mode_;
        s.hpiOrder = hpiOrder_;
        for (int i = 0; i < 3; i++) s.sci[i] = sci_[i];
        s.mpBytes = mpBytes_;
        s.mpFrames = mpFrames_;
        s.boot = boot_;
        s.notes = notes_;
        s.recording = recording_;
        s.replaying = replaying_;
        s.deterministicSeed = deterministicSeed_;
        s.tickCount = tickCount_;
        s.droppedLogs = droppedLogs_;
        s.irqServicedCount = irqServicedCount_;
        s.irqLastVector = irqLastVector_;
        s.irqInService = irqInService_;
        s.exrI = exrI_;
        s.ccrI = ccrI_;
        return s;
    }

private:
    mutable std::mutex m_;
    PanelSrc src_{PanelSrc::Unknown};
    PanelMode mode_{PanelMode::Probe};
    HpiOrder hpiOrder_{HpiOrder::Auto};
    SciDiag sci_[3]{};
    uint64_t mpBytes_{0}, mpFrames_{0};
    BootProbeDiag boot_{};
    std::string notes_;
    bool recording_{false};
    bool replaying_{false};
    std::string recordingFile_;
    uint64_t deterministicSeed_{0};
    uint64_t tickCount_{0};
    uint64_t droppedLogs_{0}; // fw20.txt: dropped log messages count
    
    // i17.txt: IRQ diagnostic private members
    uint64_t irqServicedCount_{0};
    int irqLastVector_{-1};
    bool irqInService_{false};
    uint8_t exrI_{0};
    uint8_t ccrI_{0};
};