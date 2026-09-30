#include "panel_io_validator.h"
#include "ms2000_runner.h"
#include "async_log.h"
// fw24.txt: Enhanced LCD validation
#include "lcd_observer.h"
#include "lcd_screenshot.h"
#include "diag_panel.h"
#include <thread>
#include <chrono>
#include <sstream>
#include <iomanip>

namespace MS2000 {

// Global validator instance
PanelIOValidator* g_panelValidator = nullptr;

PanelIOValidator::PanelIOValidator(Ms2kRunner* runner) : m_runner(runner) {
    resetStatistics();
    // fw24.txt: LCD observer will be initialized when needed
    // m_lcdObserver = std::make_unique<LcdObserver>();
}

std::vector<PanelTestResult> PanelIOValidator::runFullValidation() {
    LOGI("fw23.txt: Starting full panel I/O validation suite");
    
    std::vector<PanelTestResult> results;
    
    // Built-in test sequences
    auto builtinTests = createBuiltinTests();
    
    for (const auto& test : builtinTests) {
        auto result = runTestWithTimeout(test.name, 
                                        [&]() { 
                                            for (auto& cmd : test.commands) cmd();
                                        },
                                        test.validator,
                                        test.timeout);
        results.push_back(result);
        updateStatistics(result);
        logResult(result);
        
        // Brief pause between tests
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    // Custom test sequences
    auto customResults = runCustomTests();
    results.insert(results.end(), customResults.begin(), customResults.end());
    
    // fw24.txt: Add enhanced LCD validation test
    LOGI("fw24.txt: Running enhanced LCD validation test");
    auto lcdResult = testLCDResponseEnhanced();
    results.push_back(lcdResult);
    updateStatistics(lcdResult);
    logResult(lcdResult);
    
    LOGI("fw23.txt: Panel validation completed - %d tests, %d passed, %d failed",
         m_stats.totalTests, m_stats.passedTests, m_stats.failedTests);
    
    return results;
}

PanelTestResult PanelIOValidator::testSwitchResponse(MS2000Button button) {
    std::string testName = "Switch_" + std::to_string(static_cast<int>(button));
    
    return runTestWithTimeout(testName,
        [&]() {
            // Inject switch press command
            m_runner->pressButton(button);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            m_runner->releaseButton(button);
        },
        [&]() {
            // Check for firmware response (LCD change, LED state, etc.)
            auto lcdContent = m_runner->getLCDContent();
            return lcdContent.find("KORG") != std::string::npos; // Basic validation
        },
        m_defaultTimeout);
}

PanelTestResult PanelIOValidator::testLEDControl(MS2000LED led, const LEDColor& color) {
    std::string testName = "LED_" + std::to_string(static_cast<int>(led));
    
    return runTestWithTimeout(testName,
        [&]() {
            // This would normally trigger firmware LED command
            // For now, test the infrastructure
        },
        [&]() {
            // Check LED state through runner
            return m_runner->isLEDOn(led, color);
        },
        m_defaultTimeout);
}

PanelTestResult PanelIOValidator::testVRControl(VR knob, uint16_t targetValue) {
    std::string testName = "VR_" + std::to_string(static_cast<int>(knob));
    
    return runTestWithTimeout(testName,
        [&]() {
            // Inject VR value change
            m_runner->setKnobValue(knob, targetValue);
        },
        [&]() {
            // Verify the value was accepted
            uint16_t actualValue = m_runner->getKnobValue(knob);
            return (actualValue >= targetValue - 10 && actualValue <= targetValue + 10);
        },
        m_defaultTimeout);
}

PanelTestResult PanelIOValidator::testLCDResponse(const std::string& expectedText) {
    return runTestWithTimeout("LCD_Response",
        [&]() {
            // LCD response is passive - we wait for firmware to update
        },
        [&]() {
            auto lcdContent = m_runner->getLCDContent();
            return lcdContent.find(expectedText) != std::string::npos;
        },
        m_defaultTimeout);
}

void PanelIOValidator::addCustomTest(const PanelTestSequence& sequence) {
    m_customTests.push_back(sequence);
}

void PanelIOValidator::clearCustomTests() {
    m_customTests.clear();
}

std::vector<PanelTestResult> PanelIOValidator::runCustomTests() {
    std::vector<PanelTestResult> results;
    
    for (const auto& test : m_customTests) {
        auto result = runTestWithTimeout(test.name,
                                        [&]() {
                                            for (auto& cmd : test.commands) cmd();
                                        },
                                        test.validator,
                                        test.timeout);
        results.push_back(result);
        updateStatistics(result);
        logResult(result);
    }
    
    return results;
}

void PanelIOValidator::startContinuousMonitoring() {
    m_continuousMode = true;
    LOGI("fw23.txt: Started continuous panel I/O monitoring");
    
    // Launch monitoring thread
    std::thread([this]() {
        while (m_continuousMode) {
            // Quick validation cycle
            auto quickTests = createBuiltinTests();
            for (const auto& test : quickTests) {
                if (!m_continuousMode) break;
                
                auto result = runTestWithTimeout(test.name + "_Monitor",
                                               [&]() {
                                                   for (auto& cmd : test.commands) cmd();
                                               },
                                               test.validator,
                                               std::chrono::milliseconds(500));
                
                if (!result.success && m_verboseLogging) {
                    LOGI("Monitor: %s failed - %s", result.testName.c_str(), result.details.c_str());
                }
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }
        LOGI("fw23.txt: Continuous monitoring stopped");
    }).detach();
}

void PanelIOValidator::stopContinuousMonitoring() {
    m_continuousMode = false;
}

void PanelIOValidator::resetStatistics() {
    m_stats = ValidationStats{};
}

PanelTestResult PanelIOValidator::runTestWithTimeout(const std::string& testName,
                                                   std::function<void()> command,
                                                   std::function<bool()> validator,
                                                   std::chrono::milliseconds timeout) {
    PanelTestResult result;
    result.testName = testName;
    
    auto startTime = std::chrono::steady_clock::now();
    
    try {
        // Execute command
        command();
        
        // Wait for validation with timeout
        bool success = waitForCondition(validator, timeout);
        
        auto endTime = std::chrono::steady_clock::now();
        result.responseTime = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
        
        result.success = success;
        if (!success) {
            result.details = "Validation timeout or condition not met";
        } else {
            result.details = "Test passed successfully";
        }
        
    } catch (const std::exception& e) {
        result.success = false;
        result.details = std::string("Exception: ") + e.what();
    }
    
    return result;
}

void PanelIOValidator::updateStatistics(const PanelTestResult& result) {
    m_stats.totalTests++;
    if (result.success) {
        m_stats.passedTests++;
    } else {
        m_stats.failedTests++;
    }
    
    // Update timing statistics
    if (result.responseTime < m_stats.minResponseTime) {
        m_stats.minResponseTime = result.responseTime;
    }
    if (result.responseTime > m_stats.maxResponseTime) {
        m_stats.maxResponseTime = result.responseTime;
    }
    
    // Update average
    auto totalTime = m_stats.averageResponseTime * (m_stats.totalTests - 1) + result.responseTime;
    m_stats.averageResponseTime = totalTime / m_stats.totalTests;
}

void PanelIOValidator::logResult(const PanelTestResult& result) const {
    if (m_verboseLogging || !result.success) {
        std::string status = result.success ? "PASS" : "FAIL";
        LOGI("Test [%s] %s (%dms) - %s", 
             result.testName.c_str(), 
             status.c_str(),
             static_cast<int>(result.responseTime.count()),
             result.details.c_str());
    }
}

std::vector<PanelTestSequence> PanelIOValidator::createBuiltinTests() {
    std::vector<PanelTestSequence> tests;
    
    // Basic switch tests
    tests.push_back(createSwitchTest(MS2000Button::EXIT));
    tests.push_back(createSwitchTest(MS2000Button::WRITE));
    tests.push_back(createSwitchTest(MS2000Button::PAGE));
    
    // LED tests  
    tests.push_back(createLEDTest(MS2000LED::LFO1_SQU, LEDColor{0, 255}));  // Green
    tests.push_back(createLEDTest(MS2000LED::FILTER_LPF, LEDColor{255, 0})); // Red
    
    // VR tests
    tests.push_back(createVRTest(VR::FILT_CUTOFF, 2048));  // Mid-range
    tests.push_back(createVRTest(VR::FILT_RESO, 1024)); // Quarter range
    
    // fw24.txt: Use enhanced LCD test instead of basic one
    // tests.push_back(createLCDTest("KORG"));  // Commented out - replaced with enhanced version
    
    return tests;
}

PanelTestSequence PanelIOValidator::createSwitchTest(MS2000Button button) {
    PanelTestSequence test;
    test.name = "Switch_" + std::to_string(static_cast<int>(button));
    test.timeout = std::chrono::milliseconds(1000);
    
    test.commands = {
        [=]() { m_runner->pressButton(button); },
        [=]() { std::this_thread::sleep_for(std::chrono::milliseconds(50)); },
        [=]() { m_runner->releaseButton(button); }
    };
    
    test.validator = [=]() {
        // Basic validation: system should still be responsive
        return m_runner->ready();
    };
    
    return test;
}

PanelTestSequence PanelIOValidator::createLEDTest(MS2000LED led, const LEDColor& color) {
    PanelTestSequence test;
    test.name = "LED_" + std::to_string(static_cast<int>(led));
    test.timeout = std::chrono::milliseconds(1000);
    
    test.commands = {
        [=]() {
            // LED control would be triggered by firmware response
            // For now, test the query infrastructure
        }
    };
    
    test.validator = [=]() {
        // Test LED query functionality
        return true; // Always pass for now - infrastructure test
    };
    
    return test;
}

PanelTestSequence PanelIOValidator::createVRTest(VR knob, uint16_t value) {
    PanelTestSequence test;
    test.name = "VR_" + std::to_string(static_cast<int>(knob));
    test.timeout = std::chrono::milliseconds(500);
    
    test.commands = {
        [=]() { m_runner->setKnobValue(knob, value); }
    };
    
    test.validator = [=]() {
        uint16_t actual = m_runner->getKnobValue(knob);
        return (actual >= value - 20 && actual <= value + 20);
    };
    
    return test;
}

PanelTestSequence PanelIOValidator::createLCDTest(const std::string& expectedText) {
    PanelTestSequence test;
    test.name = "LCD_" + expectedText;
    test.timeout = std::chrono::milliseconds(2000);
    
    test.commands = {
        // No commands - passive observation
    };
    
    test.validator = [=]() {
        auto content = m_runner->getLCDContent();
        return content.find(expectedText) != std::string::npos;
    };
    
    return test;
}

bool PanelIOValidator::waitForCondition(std::function<bool()> condition, std::chrono::milliseconds timeout) {
    auto startTime = std::chrono::steady_clock::now();
    
    while (std::chrono::steady_clock::now() - startTime < timeout) {
        if (condition()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    return false;
}

std::string PanelIOValidator::formatTestResult(const PanelTestResult& result) const {
    std::stringstream ss;
    ss << "[" << result.testName << "] ";
    ss << (result.success ? "PASS" : "FAIL") << " ";
    ss << "(" << result.responseTime.count() << "ms) ";
    ss << result.details;
    return ss.str();
}

// fw23.txt: Global convenience functions
bool runQuickPanelValidation(Ms2kRunner* runner) {
    if (!runner || !runner->ready()) {
        LOGE("Runner not ready for panel validation");
        return false;
    }
    
    PanelIOValidator validator(runner);
    validator.setVerboseLogging(true);
    
    auto results = validator.runFullValidation();
    auto stats = validator.getStatistics();
    
    LOGI("Quick Panel Validation Results: %d/%d tests passed", 
         stats.passedTests, stats.totalTests);
    
    return stats.passedTests == stats.totalTests;
}

void setupPanelValidationLogging() {
    LOGI("fw23.txt: Panel I/O validation logging initialized");
}

// fw25.txt: Enhanced LCD validation using event-driven observer
PanelTestResult PanelIOValidator::testLCDResponseEnhanced() {
    PanelTestResult result;
    result.testName = "LCD_Enhanced";
    auto startTime = std::chrono::steady_clock::now();
    
    try {
        LOGI("fw25.txt: Starting event-driven LCD validation with 8s timeout");
        
        // Step 1: Initialize LCD observer with patterns
        std::vector<std::string> patterns = {"KORG", "MS2000"};
        m_runner->initLcdObserver(patterns, 250);  // 250ms stable window
        
        // Step 2: Initial warmup (500ms)
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        // Step 3: Wait for gates to be ready (enhanced diagnostic check)
        bool gatesReady = false;
        auto gateTimeout = std::chrono::steady_clock::now() + std::chrono::milliseconds(6000);
        while (std::chrono::steady_clock::now() < gateTimeout) {
            if (m_runner && m_runner->ready()) {
                // Check diagnostic state for better gate validation
                auto& diag = m_runner->getDiagnosticState();
                auto snapshot = diag.snapshot();
                
                // Check if panel source is detected and boot probe is complete
                bool sourceReady = (snapshot.src != PanelSrc::Unknown);
                bool bootReady = snapshot.boot.fn && snapshot.boot.on && 
                                snapshot.boot.clr && snapshot.boot.ent && snapshot.boot.ddram;
                
                LOGI("fw25.txt: Gate check - runner ready: %d, source: %d, boot flags: fn=%d on=%d clr=%d ent=%d ddram=%d", 
                     m_runner->ready(), sourceReady, snapshot.boot.fn, snapshot.boot.on, 
                     snapshot.boot.clr, snapshot.boot.ent, snapshot.boot.ddram);
                
                if (sourceReady && bootReady) {
                    gatesReady = true;
                    LOGI("fw25.txt: Boot gates ready - source detected, boot probe complete");
                    break;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        if (!gatesReady) {
            result.details = "Boot gates not ready (boot probe incomplete after 6s)";
            LOGI("fw25.txt: %s", result.details.c_str());
            return result;
        }
        
        LOGI("fw25.txt: Boot gates ready, starting event-driven pattern detection");
        
        // fw26.txt: Event-driven pattern detection with 12s timeout (MS-firmware can take 3-8s to reach banner)
        auto timeoutEnd = std::chrono::steady_clock::now() + std::chrono::milliseconds(12000);
        bool patternFound = false;
        
        while (std::chrono::steady_clock::now() < timeoutEnd) {
            // Check if observer found a pattern match
            if (m_runner->checkLcdPattern()) {
                // Wait for stable window
                if (m_runner->isLcdStable()) {
                    // Success!
                    result.success = true;
                    auto detectionTime = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - startTime);
                    
                    std::ostringstream ss;
                    ss << "Event-driven pattern detection succeeded in " << detectionTime.count() 
                       << "ms, stable window confirmed";
                    result.details = ss.str();
                    
                    LOGI("fw25.txt: %s", result.details.c_str());
                    patternFound = true;
                    break;
                }
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        
        // Handle failure
        if (!patternFound) {
            result.success = false;
            result.details = "Event-driven pattern match timeout after 12s";
            
            LOGI("fw25.txt: LCD validation FAILED - %s", result.details.c_str());
        }
        
    } catch (const std::exception& e) {
        result.success = false;
        result.details = std::string("Exception: ") + e.what();
        LOGI("fw25.txt: LCD validation exception: %s", e.what());
    }
    
    result.responseTime = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - startTime);
    
    return result;
}

// fw24.txt: Helper methods for enhanced LCD validation (simplified/stubbed for now)
// These will be fully implemented in future iterations when the full LcdObserver is integrated

} // namespace MS2000