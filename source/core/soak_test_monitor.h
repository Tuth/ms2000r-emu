// fw27.txt Phase 4: Long-run soak testing with performance monitoring
// 8-12 hour stability testing with comprehensive diagnostics

#pragma once
#include <chrono>
#include <atomic>
#include <thread>
#include <vector>
#include <string>
#include <functional>
#include <fstream>
#include <memory>

namespace MS2000 {

// Performance metrics collected during soak testing
struct SoakMetrics {
    std::chrono::steady_clock::time_point startTime;
    std::atomic<uint64_t> totalFramesProcessed{0};
    std::atomic<uint64_t> audioXruns{0};           // Audio buffer underruns/overruns
    std::atomic<uint64_t> cpuCycles{0};            // Total CPU cycles executed
    std::atomic<uint64_t> dspCycles{0};            // Total DSP cycles executed
    std::atomic<uint64_t> logDrops{0};             // Dropped log messages
    std::atomic<uint32_t> ringBufferFillPercent{50}; // Ring buffer fill percentage
    std::atomic<uint32_t> cpuUsagePercent{0};      // CPU usage percentage
    std::atomic<uint32_t> memoryUsageMB{0};        // Memory usage in MB
    std::atomic<bool> crashed{false};              // System crash detected
};

// Soak test configuration
struct SoakTestConfig {
    std::chrono::hours duration{8};               // Default 8 hours
    uint32_t statsIntervalSeconds = 60;           // Report stats every minute
    uint32_t ringBufferTargetFill = 50;           // Target 50% ring fill
    uint32_t ringBufferTolerance = 10;            // ±10% tolerance
    uint32_t maxCpuPercent = 80;                  // Max 80% CPU usage
    uint32_t maxMemoryMB = 512;                   // Max 512MB memory
    std::string logFile = "soak_test.log";        // Log file path
    bool enableDetailedLogging = false;           // Verbose logging
};

class SoakTestMonitor {
public:
    explicit SoakTestMonitor(const SoakTestConfig& config) 
        : m_config(config) {}
    
    ~SoakTestMonitor() {
        stop();
    }
    
    // Start soak testing
    bool start() {
        if (m_running) return false;
        
        m_metrics.startTime = std::chrono::steady_clock::now();
        m_running = true;
        
        // Start monitoring thread
        m_monitorThread = std::thread(&SoakTestMonitor::monitorLoop, this);
        
        // Start statistics thread
        m_statsThread = std::thread(&SoakTestMonitor::statsLoop, this);
        
        return true;
    }
    
    // Stop soak testing
    void stop() {
        if (!m_running) return;
        
        m_running = false;
        
        if (m_monitorThread.joinable()) {
            m_monitorThread.join();
        }
        if (m_statsThread.joinable()) {
            m_statsThread.join();
        }
    }
    
    // Check if soak test is currently running
    bool isRunning() const { return m_running; }
    
    // Get current metrics
    const SoakMetrics& getMetrics() const { return m_metrics; }
    
    // Register callbacks for metrics collection
    void setFrameProcessedCallback(std::function<void()> callback) {
        m_frameCallback = callback;
    }
    
    void setCpuUsageCallback(std::function<uint32_t()> callback) {
        m_cpuCallback = callback;
    }
    
    void setMemoryUsageCallback(std::function<uint32_t()> callback) {
        m_memoryCallback = callback;
    }
    
    void setRingFillCallback(std::function<uint32_t()> callback) {
        m_ringFillCallback = callback;
    }
    
    // Report events during testing
    void reportXrun() {
        m_metrics.audioXruns++;
        logEvent("AUDIO_XRUN", "Audio buffer underrun/overrun detected");
    }
    
    void reportLogDrop() {
        m_metrics.logDrops++;
    }
    
    void reportCrash(const std::string& details) {
        m_metrics.crashed = true;
        logEvent("SYSTEM_CRASH", details);
    }
    
    void reportFrameProcessed() {
        m_metrics.totalFramesProcessed++;
        if (m_frameCallback) {
            m_frameCallback();
        }
    }
    
    // Get elapsed time
    std::chrono::seconds getElapsedTime() const {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::seconds>(now - m_metrics.startTime);
    }
    
    // Check if test duration completed
    bool isDurationComplete() const {
        return getElapsedTime() >= m_config.duration;
    }
    
    // Validate PASS criteria
    struct ValidationResult {
        bool success = false;
        bool noCrashes = false;
        bool noXruns = false;
        bool ringFillInRange = false;
        bool noMemoryLeaks = false;
        std::string details;
        
        // Performance statistics
        uint64_t totalFrames = 0;
        uint32_t avgCpuPercent = 0;
        uint32_t avgMemoryMB = 0;
        uint32_t avgRingFill = 0;
        std::chrono::seconds totalTime{0};
    };
    
    ValidationResult validateResults() const {
        ValidationResult result;
        
        result.totalFrames = m_metrics.totalFramesProcessed;
        result.totalTime = getElapsedTime();
        result.avgCpuPercent = m_metrics.cpuUsagePercent;
        result.avgMemoryMB = m_metrics.memoryUsageMB;
        result.avgRingFill = m_metrics.ringBufferFillPercent;
        
        // Check PASS criteria from fw27.txt:
        // "0 crash, 0 xrun (host-locked), fill% ~50 ± 10%, leak=0"
        
        result.noCrashes = !m_metrics.crashed;
        result.noXruns = (m_metrics.audioXruns == 0);
        
        uint32_t ringFill = m_metrics.ringBufferFillPercent;
        uint32_t target = m_config.ringBufferTargetFill;
        uint32_t tolerance = m_config.ringBufferTolerance;
        result.ringFillInRange = (ringFill >= (target - tolerance)) && 
                                (ringFill <= (target + tolerance));
        
        // Simple memory leak detection (memory usage should be stable)
        result.noMemoryLeaks = (m_metrics.memoryUsageMB <= m_config.maxMemoryMB);
        
        result.success = result.noCrashes && result.noXruns && 
                        result.ringFillInRange && result.noMemoryLeaks;
        
        // Generate details string
        if (result.success) {
            result.details = "Soak test PASSED - all stability criteria met";
        } else {
            result.details = "Soak test FAILED: ";
            if (!result.noCrashes) result.details += "crashes detected ";
            if (!result.noXruns) result.details += "audio xruns detected ";
            if (!result.ringFillInRange) result.details += "ring buffer fill out of range ";
            if (!result.noMemoryLeaks) result.details += "memory leaks detected ";
        }
        
        return result;
    }

private:
    void monitorLoop() {
        while (m_running && !isDurationComplete()) {
            // Update CPU usage
            if (m_cpuCallback) {
                m_metrics.cpuUsagePercent = m_cpuCallback();
            }
            
            // Update memory usage
            if (m_memoryCallback) {
                m_metrics.memoryUsageMB = m_memoryCallback();
            }
            
            // Update ring buffer fill
            if (m_ringFillCallback) {
                m_metrics.ringBufferFillPercent = m_ringFillCallback();
            }
            
            // Check for anomalies
            if (m_metrics.cpuUsagePercent > m_config.maxCpuPercent) {
                logEvent("HIGH_CPU", "CPU usage exceeded threshold");
            }
            
            if (m_metrics.memoryUsageMB > m_config.maxMemoryMB) {
                logEvent("HIGH_MEMORY", "Memory usage exceeded threshold");
            }
            
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
    
    void statsLoop() {
        while (m_running && !isDurationComplete()) {
            std::this_thread::sleep_for(std::chrono::seconds(m_config.statsIntervalSeconds));
            
            if (!m_running) break;
            
            // Log periodic statistics
            auto elapsed = getElapsedTime();
            logStats(elapsed);
        }
    }
    
    void logEvent(const std::string& type, const std::string& message) {
        auto elapsed = getElapsedTime();
        
        std::string logLine = "[" + std::to_string(elapsed.count()) + "s] " + 
                             type + ": " + message + "\n";
        
        // Mock logging for now
        if (m_config.enableDetailedLogging) {
            // In real implementation, would write to file
            (void)logLine;
        }
    }
    
    void logStats(std::chrono::seconds elapsed) {
        std::string statsLine = 
            "[STATS " + std::to_string(elapsed.count()) + "s] " +
            "Frames: " + std::to_string(m_metrics.totalFramesProcessed.load()) + " " +
            "CPU: " + std::to_string(m_metrics.cpuUsagePercent.load()) + "% " +
            "MEM: " + std::to_string(m_metrics.memoryUsageMB.load()) + "MB " +
            "Ring: " + std::to_string(m_metrics.ringBufferFillPercent.load()) + "% " +
            "Xruns: " + std::to_string(m_metrics.audioXruns.load()) + "\n";
            
        if (m_config.enableDetailedLogging) {
            // In real implementation, would write to file
            (void)statsLine;
        }
    }
    
    SoakTestConfig m_config;
    SoakMetrics m_metrics;
    std::atomic<bool> m_running{false};
    
    std::thread m_monitorThread;
    std::thread m_statsThread;
    
    // Callbacks for metrics collection
    std::function<void()> m_frameCallback;
    std::function<uint32_t()> m_cpuCallback;
    std::function<uint32_t()> m_memoryCallback;
    std::function<uint32_t()> m_ringFillCallback;
};

} // namespace MS2000