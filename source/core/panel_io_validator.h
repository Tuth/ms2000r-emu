#pragma once
#include <functional>
#include <chrono>
#include <string>
#include <vector>
#include <atomic>
#include <map>
#include "ms2000_switch_matrix.h"
#include "ms2000_led_matrix.h"
#include "ms2000_panel_adc.h"

// fw23.txt: End-to-end panel I/O validation system
// Tests the complete signal chain: GUI → MP RX → FW → echo → LED/LCD response

namespace MS2000 {

// Panel I/O test result
struct PanelTestResult {
    bool success = false;
    std::string testName;
    std::string details;
    std::chrono::milliseconds responseTime{0};
    
    // Validation data
    struct {
        uint16_t expectedVR = 0;
        uint16_t actualVR = 0;
        bool expectedLED = false;
        bool actualLED = false;
        std::string expectedLCD;
        std::string actualLCD;
    } validation;
};

// Test sequence configuration
struct PanelTestSequence {
    std::string name;
    std::vector<std::function<void()>> commands;
    std::function<bool()> validator;
    std::chrono::milliseconds timeout{2000};
};

// Forward declarations
class Ms2kRunner;
class LcdObserver;

class PanelIOValidator {
public:
    explicit PanelIOValidator(Ms2kRunner* runner);
    ~PanelIOValidator() = default;

    // fw23.txt: Main validation functions
    std::vector<PanelTestResult> runFullValidation();
    PanelTestResult testSwitchResponse(MS2000Button button);
    PanelTestResult testLEDControl(MS2000LED led, const LEDColor& color);
    PanelTestResult testVRControl(VR knob, uint16_t targetValue);
    PanelTestResult testLCDResponse(const std::string& expectedText);
    
    // fw24.txt: Enhanced LCD validation with gate-based and event-based detection  
    PanelTestResult testLCDResponseEnhanced();
    
    // Test sequence management
    void addCustomTest(const PanelTestSequence& sequence);
    void clearCustomTests();
    std::vector<PanelTestResult> runCustomTests();
    
    // Real-time monitoring
    void startContinuousMonitoring();
    void stopContinuousMonitoring();
    bool isContinuousMonitoring() const { return m_continuousMode; }
    
    // Statistics and reporting
    struct ValidationStats {
        uint32_t totalTests = 0;
        uint32_t passedTests = 0;
        uint32_t failedTests = 0;
        std::chrono::milliseconds averageResponseTime{0};
        std::chrono::milliseconds maxResponseTime{0};
        std::chrono::milliseconds minResponseTime{9999};
    };
    
    ValidationStats getStatistics() const { return m_stats; }
    void resetStatistics();
    
    // Configuration
    void setDefaultTimeout(std::chrono::milliseconds timeout) { m_defaultTimeout = timeout; }
    void setVerboseLogging(bool enabled) { m_verboseLogging = enabled; }
    
private:
    Ms2kRunner* m_runner;
    std::atomic<bool> m_continuousMode{false};
    std::chrono::milliseconds m_defaultTimeout{2000};
    bool m_verboseLogging = false;
    
    // Test sequences
    std::vector<PanelTestSequence> m_customTests;
    
    // Statistics tracking
    mutable ValidationStats m_stats;
    
    // fw24.txt: LCD observer for enhanced validation (will be added in full implementation)
    // std::unique_ptr<LcdObserver> m_lcdObserver;
    
    // Internal test methods
    PanelTestResult runTestWithTimeout(const std::string& testName, 
                                      std::function<void()> command,
                                      std::function<bool()> validator,
                                      std::chrono::milliseconds timeout);
    
    void updateStatistics(const PanelTestResult& result);
    void logResult(const PanelTestResult& result) const;
    
    // Built-in test sequences
    std::vector<PanelTestSequence> createBuiltinTests();
    PanelTestSequence createSwitchTest(MS2000Button button);
    PanelTestSequence createLEDTest(MS2000LED led, const LEDColor& color);
    PanelTestSequence createVRTest(VR knob, uint16_t value);
    PanelTestSequence createLCDTest(const std::string& expectedText);
    
    // Helper methods
    bool waitForCondition(std::function<bool()> condition, std::chrono::milliseconds timeout);
    std::string formatTestResult(const PanelTestResult& result) const;
    
    // fw24.txt: Enhanced LCD validation helper methods (will be added in full implementation)
    // bool waitForGatesReady(uint32_t timeoutMs);
    // void connectLcdCallbacks();
    // void saveFailureScreenshot();
};

// fw23.txt: Global validator instance for CLI integration
extern PanelIOValidator* g_panelValidator;

// Convenience functions for CLI
bool runQuickPanelValidation(Ms2kRunner* runner);
void setupPanelValidationLogging();

} // namespace MS2000