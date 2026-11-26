#pragma once

#include <windows.h>
#include <d3d11.h>
#include <imgui.h>
#include <atomic>
#include <mutex>
#include <string>

inline constexpr const char* kAppVersion = "1.0.3";

struct AppState {
    std::atomic_bool busy{ false };
    std::atomic_bool hasError{ false };
    std::atomic_bool autoLaunch{ false };
    std::atomic_bool remoteChecked{ false };
    std::atomic_bool maintenance{ false };
    std::atomic_bool showDisclaimer{ false };
    std::atomic<float> progress{ 0.0f };

    std::string statusText = "Durum kontrol ediliyor…";
    std::string patchState = "KONTROL EDİLİYOR";
    std::string maintenanceMessage = "Uygulama şu anda bakımdadır. Lütfen daha sonra tekrar deneyin.";
    std::mutex mutex;
};

extern AppState g;
extern ID3D11Device* gDevice;
extern ID3D11DeviceContext* gContext;
extern IDXGISwapChain* gSwapChain;
extern ID3D11RenderTargetView* gRenderTarget;
extern HWND gWindow;

inline const ImVec4 kBg(0.012f, 0.008f, 0.009f, 1.00f);
inline const ImVec4 kPanel(0.046f, 0.016f, 0.018f, 0.96f);
inline const ImVec4 kPanelRaised(0.072f, 0.026f, 0.030f, 0.98f);
inline const ImVec4 kLine(0.62f, 0.22f, 0.26f, 0.28f);
inline const ImVec4 kAccent(0.72f, 0.16f, 0.20f, 1.00f);
inline const ImVec4 kGreen(0.34f, 0.88f, 0.61f, 1.00f);
inline const ImVec4 kRed(0.96f, 0.37f, 0.43f, 1.00f);
inline const ImVec4 kMuted(0.70f, 0.58f, 0.58f, 1.00f);

void StartRemoteCheck();
void AcceptDisclaimer();
void StartPatchAction(int mode); // 0 = check status, 1 = install, 2 = uninstall
void LaunchSteamGame();
void OpenDiscord();
void OpenLogFile();
void DrawStartup();
void DrawDisclaimer();
void DrawMaintenance();
void DrawMain();
void MenuApplyStyle();
void MenuShutdownAssets();
