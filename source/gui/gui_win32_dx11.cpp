// fw28.txt: Win32/DX11 GUI implementation for MS2000 Emulator
// GPT5 Testvér GUI skeleton - Main GUI loop with ImGui integration

#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <tchar.h>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

#include "gui_win32_dx11.h"

// Include MS2000 core systems
#include "../core/ms2000_runner.h"
#include "../core/lcd_gui_imgui.h"
#include "../core/diag_panel.h"

// DX11 globals (from ImGui example)
static ID3D11Device*            g_pd3dDevice = nullptr;
static ID3D11DeviceContext*     g_pd3dDeviceContext = nullptr;
static IDXGISwapChain*          g_pSwapChain = nullptr;
static ID3D11RenderTargetView*  g_mainRenderTargetView = nullptr;

// Forward declarations
static void CreateRenderTarget();
static void CleanupRenderTarget();
static bool CreateDeviceD3D(HWND hWnd);
static void CleanupDeviceD3D();
extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);
static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// --------------------------- GUI MAIN ---------------------------
int run_gui_dx11(MS2000::Ms2kRunner& runner, const AppConfig& cfg)
{
    // Win32 window creation
    WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC, WndProc, 0L, 0L,
                      GetModuleHandle(NULL), NULL, NULL, NULL, NULL,
                      _T("MS2000GUI"), NULL };
    ::RegisterClassEx(&wc);
    
    HWND hwnd = ::CreateWindow(wc.lpszClassName, cfg.title.c_str(),
                               WS_OVERLAPPEDWINDOW, 100, 100, cfg.width, cfg.height,
                               NULL, NULL, wc.hInstance, NULL);

    if (!CreateDeviceD3D(hwnd)) {
        CleanupDeviceD3D();
        ::UnregisterClass(wc.lpszClassName, wc.hInstance);
        return -1;
    }
    CreateRenderTarget();
    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);

    // ImGui initialization
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.IniFilename = "imgui_ms2k.ini";
    io.LogFilename = "imgui_ms2k.log";

    ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    // Runner starts (audio/mmcss, etc.)
    runner.start();

    // Window state
    bool show_diag = true;
    bool show_panel = true;
    bool show_lcd = true;
    bool show_midi = false;
    
    // Hotkey state tracking
    static bool lastTildeState = false;
    static bool lastF5State = false;

    // Main loop
    MSG msg;
    ZeroMemory(&msg, sizeof(msg));
    bool running = true;
    while (running) {
        // Message handling
        while (::PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { running = false; break; }
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
        }
        if (!running) break;

        // Frame start
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // HOTKEYS (with edge detection)
        bool currentTildeState = (GetAsyncKeyState(VK_OEM_3) & 0x8000) != 0; // '~'
        if (currentTildeState && !lastTildeState) {
            show_diag = !show_diag;
        }
        lastTildeState = currentTildeState;
        
        bool currentF5State = (GetAsyncKeyState(VK_F5) & 0x8000) != 0;
        if (currentF5State && !lastF5State) {
            // PNG screenshot (fw22.txt functionality)
            auto snap = runner.getLcdGuiSnapshot();
            // Note: This would call the fw22 screenshot function
            // runner.saveLcdPng(snap, "screens", "manual");
            // For now, just show in console
            OutputDebugStringA("[GUI] F5 pressed - LCD screenshot requested\n");
        }
        lastF5State = currentF5State;

        // Main window - no docking, 3 panels
        ImGui::SetNextWindowPos(ImVec2(8, 8), ImGuiCond_Once);
        ImGui::SetNextWindowSize(ImVec2((float)cfg.width - 32, (float)cfg.height - 64), ImGuiCond_Once);
        ImGui::Begin("MS2000 Emulator - fw28.txt GUI", nullptr, ImGuiWindowFlags_NoCollapse);

        // LCD Display
        if (show_lcd) {
            ImGui::TextUnformatted("LCD Display");
            auto snap = runner.getLcdGuiSnapshot();


            DrawLcdImGui(snap); // From existing LCD GUI system
        }

        // Panel Controls
        if (show_panel) {
            ImGui::Separator();
            ImGui::TextUnformatted("Panel Controls");
            DrawPanelControls(
                [&](uint8_t id, bool on) { 
                    // Send switch command to runner
                    // runner.panel().sendSwitch(id, on); 
                    // For now, mock implementation
                    (void)id; (void)on;
                },
                [&](uint8_t id, bool on) { 
                    // Send LED command to runner
                    // runner.panel().sendLed(id, on); 
                    // For now, mock implementation
                    (void)id; (void)on;
                },
                [&](uint8_t id, float v) { 
                    // Send VR command to runner
                    // runner.panel().sendVr(id, (uint16_t)(v * 1023.0f)); 
                    // For now, mock implementation
                    (void)id; (void)v;
                }
            );
        }

        // pw_on.txt: Master Volume and Power Controls
        ImGui::Separator();
        ImGui::TextUnformatted("Master Volume / Power");

        static float master_volume_percent = 75.0f; // Default 75%
        static bool power_on = true;  // System starts powered ON
        static bool mute = true;      // System starts muted
        
        // Power On/Off Button
        if (ImGui::Button(power_on ? "POWER: ON" : "POWER: OFF")) {
            power_on = !power_on;
            // Connect to Ms2kRunner power state management
            runner.setPowerState(power_on);

            // Send firmware power-on command - try EXIT button press to wake firmware
            if (power_on) {
                // Simulate EXIT button press+release to wake firmware from standby
                runner.pressButton(MS2000::MS2000Button::EXIT);  // Press EXIT to wake from standby
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0,1,0,1), "→ Firmware wake-up signal sent");
            }
        }
        ImGui::SameLine();

        // Mute Button
        if (ImGui::Button(mute ? "MUTE: ON" : "MUTE: OFF")) {
            mute = !mute;
            // Connect to Ms2kRunner mute state management
            runner.setMuteState(mute);
        }
        
        // Master Volume Slider (0-100%)
        ImGui::SliderFloat("Master Volume", &master_volume_percent, 0.0f, 100.0f, "%.1f%%");
        if (ImGui::IsItemEdited()) {
            // Apply volume change
            // runner.getAudioEngine().setMasterVolumePercent(master_volume_percent);
        }
        
        // Status display - get actual state from runner
        float db = -80.0f + 80.0f * (master_volume_percent / 100.0f);
        bool actual_power_on = runner.isPowerOn();
        bool actual_mute = runner.isMuted();
        ImGui::Text("Status: %.1f dB (power=%s, mute=%s)",
                    db, actual_power_on ? "ON" : "OFF", actual_mute ? "ON" : "OFF");

        // Sync local GUI state with runner state
        if (power_on != actual_power_on) power_on = actual_power_on;
        if (mute != actual_mute) mute = actual_mute;

        // Diagnostics Panel
        if (show_diag) {
            ImGui::Separator();
            ImGui::TextUnformatted("Diagnostics - fw17.txt Diagnostic Panel");
            
            // Mock diagnostic data for now
            // In full implementation, would use: auto s = runner.getDiagSnapshot();
            ImGui::Text("Source: SCI0  Mode: PanelMP  HPI Order: Normal");
            ImGui::Text("PanelMP: bytes=12345 frames=678");
            ImGui::Text("LCD Boot: FN=1 ON=1 CLR=1 ENT=1 DDRAM=1 DATA=15");
            ImGui::Text("Deterministic: tick=987654 seed=42");
            
            // Audio mini-meter (ring fill / xruns)
            DrawAudioMiniMeter(0, 0.5f); // Mock: 0 xruns, 50% fill
            ImGui::Text("MMCSS: ON");
            ImGui::Text("DroppedLogs: 0");
        }

        // Menu bar for panel toggles
        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("View")) {
                ImGui::MenuItem("LCD Display", nullptr, &show_lcd);
                ImGui::MenuItem("Panel Controls", nullptr, &show_panel);
                ImGui::MenuItem("Diagnostics", nullptr, &show_diag);
                ImGui::MenuItem("MIDI Monitor", nullptr, &show_midi);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Help")) {
                if (ImGui::MenuItem("About fw28.txt")) {
                    // Could show about dialog
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }

        ImGui::End();

        // Render
        ImGui::Render();
        const float clear_color_with_alpha[4] = { 0.08f, 0.10f, 0.12f, 1.0f };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, NULL);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color_with_alpha);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        // Present
        g_pSwapChain->Present(cfg.vsync ? 1 : 0, 0);
        
        // Frame rate cap (60 FPS)
        Sleep(16);
    }

    // Cleanup
    runner.stop();

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupRenderTarget();
    CleanupDeviceD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClass(wc.lpszClassName, wc.hInstance);
    return 0;
}

// --------------------- DX11 boilerplate (from ImGui example) ---------------------
static void CreateRenderTarget()
{
    ID3D11Texture2D* pBackBuffer = nullptr;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, NULL, &g_mainRenderTargetView);
    if (pBackBuffer) pBackBuffer->Release();
}

static void CleanupRenderTarget()
{
    if (g_mainRenderTargetView) { 
        g_mainRenderTargetView->Release(); 
        g_mainRenderTargetView = nullptr; 
    }
}

static bool CreateDeviceD3D(HWND hWnd)
{
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    if (D3D11CreateDeviceAndSwapChain(
            NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, createDeviceFlags,
            featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain,
            &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext) != S_OK)
        return false;
    return true;
}

static void CleanupDeviceD3D()
{
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;
    switch (msg) {
    case WM_SIZE:
        if (g_pd3dDevice != NULL && wParam != SIZE_MINIMIZED) {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
        }
        return 0;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProc(hWnd, msg, wParam, lParam);
}