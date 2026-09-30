// fw14.txt: Panel Recorder/Replay GUI widget
#pragma once
#include "../core/panel_recorder.h"
#include <imgui.h>
#include <string>
#include <memory>

class PanelRecorderWidget {
public:
    PanelRecorderWidget() : m_recorder(std::make_unique<PanelRecorder>()),
                           m_replayer(std::make_unique<PanelReplayer>()) {}
    
    void render() {
        ImGui::Begin("Panel Recorder/Replay", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        
        renderRecorder();
        ImGui::Separator();
        renderReplayer();
        ImGui::Separator();
        renderFileOperations();
        
        ImGui::End();
    }
    
    // Call this to capture panel data
    void captureData(uint8_t data, bool isHPI = false, int sciId = 0) {
        if (m_recorder) {
            m_recorder->captureData(data, isHPI, sciId);
        }
    }
    
    // Call this periodically to get replay data
    bool getReplayData(uint8_t& data, bool& isHPI, int& sciId) {
        if (m_replayer) {
            return m_replayer->getNextReplayData(data, isHPI, sciId);
        }
        return false;
    }
    
    bool isReplaying() const {
        return m_replayer && m_replayer->isPlaying();
    }

private:
    void renderRecorder() {
        ImGui::Text("Recording");
        
        bool recording = m_recorder->isRecording();
        if (recording) {
            ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "● REC");
            ImGui::SameLine();
            if (ImGui::Button("Stop")) {
                m_recorder->stopRecording();
            }
        } else {
            if (ImGui::Button("Start Recording")) {
                m_recorder->startRecording();
            }
        }
        
        ImGui::Text("Captured: %zu bytes", m_recorder->getCaptureCount());
    }
    
    void renderReplayer() {
        ImGui::Text("Replay");
        
        bool playing = m_replayer->isPlaying();
        if (playing) {
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "▶ PLAY");
            ImGui::SameLine();
            if (ImGui::Button("Stop##Replay")) {
                m_replayer->stopReplay();
            }
            
            // Progress bar
            float progress = m_replayer->getProgress();
            ImGui::ProgressBar(progress, ImVec2(-1, 0), 
                              (std::to_string(m_replayer->getCurrentIndex()) + "/" + 
                               std::to_string(m_replayer->getTotalCount())).c_str());
        } else {
            if (ImGui::Button("Start Replay") && m_replayer->getTotalCount() > 0) {
                m_replayer->startReplay();
            }
        }
        
        ImGui::Text("Total: %zu captures", m_replayer->getTotalCount());
    }
    
    void renderFileOperations() {
        ImGui::Text("File Operations");
        
        // Filename input
        static char filename[256] = "panel_capture.mpcap";
        ImGui::InputText("Filename", filename, sizeof(filename));
        
        // Save/Load buttons
        if (ImGui::Button("Save Capture")) {
            if (m_recorder->saveTo(std::string(filename))) {
                m_statusMessage = "Capture saved successfully!";
                m_statusColor = ImVec4(0.0f, 1.0f, 0.0f, 1.0f);
            } else {
                m_statusMessage = "Failed to save capture!";
                m_statusColor = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
            }
        }
        
        ImGui::SameLine();
        if (ImGui::Button("Load Capture")) {
            PanelRecorder tempRecorder;
            if (tempRecorder.loadFrom(std::string(filename))) {
                m_replayer->loadCapture(tempRecorder.getCaptures());
                m_statusMessage = "Capture loaded successfully!";
                m_statusColor = ImVec4(0.0f, 1.0f, 0.0f, 1.0f);
            } else {
                m_statusMessage = "Failed to load capture!";
                m_statusColor = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
            }
        }
        
        // Status message
        if (!m_statusMessage.empty()) {
            ImGui::TextColored(m_statusColor, "%s", m_statusMessage.c_str());
        }
        
        // Quick presets
        ImGui::Text("Quick Actions:");
        if (ImGui::Button("Record KORG Boot")) {
#ifdef _WIN32
            strcpy_s(filename, sizeof(filename), "korg_boot_sequence.mpcap");
#else
            strncpy(filename, "korg_boot_sequence.mpcap", sizeof(filename) - 1);
            filename[sizeof(filename) - 1] = '\0';
#endif
            m_recorder->startRecording();
            m_statusMessage = "Recording KORG boot sequence...";
            m_statusColor = ImVec4(1.0f, 1.0f, 0.0f, 1.0f);
        }
        
        ImGui::SameLine();
        if (ImGui::Button("Load & Replay")) {
            PanelRecorder tempRecorder;
            if (tempRecorder.loadFrom(std::string(filename))) {
                m_replayer->loadCapture(tempRecorder.getCaptures());
                m_replayer->startReplay();
                m_statusMessage = "Playing back capture...";
                m_statusColor = ImVec4(0.0f, 1.0f, 1.0f, 1.0f);
            }
        }
    }
    
    std::unique_ptr<PanelRecorder> m_recorder;
    std::unique_ptr<PanelReplayer> m_replayer;
    std::string m_statusMessage;
    ImVec4 m_statusColor = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
};