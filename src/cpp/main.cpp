#include <windows.h>
#include <d3d11.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <winhttp.h>
#include <filesystem>
#include <fstream>
#include <regex>
#include <string>
#include <thread>
#include <chrono>
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include "App.h"
#include "menu.h"
#include "obf.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "winhttp.lib")

namespace fs = std::filesystem;
constexpr wchar_t kWindowTitle[] = L"ROB RUN Türkçe Yama";

AppState g;
ID3D11Device* gDevice = nullptr;
ID3D11DeviceContext* gContext = nullptr;
IDXGISwapChain* gSwapChain = nullptr;
ID3D11RenderTargetView* gRenderTarget = nullptr;
HWND gWindow = nullptr;

namespace {

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), w.data(), len);
    return w;
}

std::wstring LocalAppDir() {
    PWSTR base = nullptr;
    std::wstring path;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &base)) && base) {
        path = fs::path(base).wstring() + L"\\RobRunTurkce";
        CoTaskMemFree(base);
        std::error_code ec;
        fs::create_directories(path, ec);
    }
    return path;
}

std::string NormalizeDriveUrl(const std::string& url) {
    // Google Drive paylaşım linkini doğrudan indirme linkine çevirir
    std::smatch m;
    std::regex re1(R"(drive\.google\.com/file/d/([A-Za-z0-9_-]+))");
    std::regex re2(R"(drive\.google\.com/open\?id=([A-Za-z0-9_-]+))");
    std::regex re3(R"(drive\.google\.com/uc\?.*id=([A-Za-z0-9_-]+))");
    std::string id;
    if (std::regex_search(url, m, re1) || std::regex_search(url, m, re2) || std::regex_search(url, m, re3)) {
        id = m[1].str();
        return "https://drive.usercontent.google.com/download?id=" + id + "&export=download&confirm=t";
    }
    return url;
}

bool HttpGetBytes(const std::wstring& url, std::string& outBody) {
    outBody.clear();
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256]{}, path[2048]{};
    uc.lpszHostName = host; uc.dwHostNameLength = ARRAYSIZE(host);
    uc.lpszUrlPath = path; uc.dwUrlPathLength = ARRAYSIZE(path);
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) return false;

    HINTERNET session = WinHttpOpen(L"RobRunTR/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return false;
    WinHttpSetTimeouts(session, 8000, 8000, 10000, 30000);

    DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(session, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy));

    HINTERNET conn = WinHttpConnect(session, host, uc.nPort, 0);
    if (!conn) { WinHttpCloseHandle(session); return false; }

    DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET req = WinHttpOpenRequest(conn, L"GET", path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!req) { WinHttpCloseHandle(conn); WinHttpCloseHandle(session); return false; }

    bool ok = false;
    if (WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(req, nullptr)) {
        DWORD status = 0, sz = sizeof(status);
        WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX);
        if (status >= 200 && status < 300) {
            DWORD avail = 0;
            while (WinHttpQueryDataAvailable(req, &avail) && avail > 0) {
                std::string chunk(avail, '\0');
                DWORD read = 0;
                if (WinHttpReadData(req, chunk.data(), avail, &read) && read > 0) {
                    chunk.resize(read);
                    outBody += chunk;
                } else break;
            }
            ok = true;
        }
    }
    WinHttpCloseHandle(req);
    WinHttpCloseHandle(conn);
    WinHttpCloseHandle(session);
    return ok;
}

std::string JsonExtractString(const std::string& json, const std::string& key) {
    std::regex re("\"" + key + "\"\\s*:\\s*\"((?:\\\\.|[^\"\\\\])*)\"");
    std::smatch m;
    if (!std::regex_search(json, m, re)) return "";
    std::string raw = m[1].str();
    std::string out;
    for (size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] == '\\' && i + 1 < raw.size()) {
            char nxt = raw[++i];
            if (nxt == 'n') out += '\n';
            else if (nxt == 'r') out += '\r';
            else if (nxt == 't') out += '\t';
            else out += nxt;
        } else {
            out += raw[i];
        }
    }
    return out;
}

bool JsonExtractBool(const std::string& json, const std::string& key, bool defVal = false) {
    std::regex re("\"" + key + "\"\\s*:\\s*(true|false)");
    std::smatch m;
    if (!std::regex_search(json, m, re)) return defVal;
    return m[1].str() == "true";
}

bool FetchFirebaseConfigSync() {
    if (IsAnalysisEnvironment()) return false;
    std::string body;
    std::wstring url = std::wstring(L"https://") + OBF_WSTR("YOUR_PROJECT.firebaseio.com") + OBF_WSTR("/config.json");
    if (!HttpGetBytes(url, body) || body.empty() || body == "null") {
        return false;
    }
    bool maint = JsonExtractBool(body, "maintenance", false);
    std::string msg = JsonExtractString(body, "maintenanceMessage");
    std::string ver = JsonExtractString(body, "latestVersion");
    std::string updUrl = JsonExtractString(body, "updateExeUrl");
    std::string dlUrl = JsonExtractString(body, "downloadUrl");
    std::string chg = JsonExtractString(body, "changelog");

    std::string resolvedUrl = updUrl.empty() ? dlUrl : updUrl;

    bool needUpdate = (!ver.empty() && ver != kAppVersion);
    bool showWN = false;
    if (!needUpdate && !chg.empty()) {
        std::wstring seenFile = LocalAppDir() + L"\\seen_version.txt";
        std::string seen;
        std::ifstream fin(std::filesystem::path(seenFile), std::ios::binary);
        if (fin.is_open()) {
            std::getline(fin, seen);
            while (!seen.empty() && (seen.back() == '\r' || seen.back() == '\n' || seen.back() == ' ')) seen.pop_back();
        }
        if (seen != kAppVersion) {
            showWN = true;
            std::ofstream fout(std::filesystem::path(seenFile), std::ios::binary | std::ios::trunc);
            if (fout.is_open()) fout << kAppVersion;
        }
    }

    std::lock_guard<std::mutex> lock(g.mutex);
    g.maintenance.store(maint);
    if (!msg.empty()) g.maintenanceMessage = msg;
    if (!ver.empty()) {
        g.displayVersion = ver;
        g.remoteVersion = ver;
    }
    if (!resolvedUrl.empty()) g.updateExeUrl = resolvedUrl;
    if (!chg.empty()) g.changelogText = chg;
    if (needUpdate) g.updateRequired.store(true);
    if (showWN) {
        g.whatsNewStart = std::chrono::steady_clock::now();
        g.showWhatsNew.store(true);
    }
    return true;
}

void LogAppUsageSync(const char* action) {
    if (IsAnalysisEnvironment()) return;
    HINTERNET session = WinHttpOpen(L"RobRunTR/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return;
    WinHttpSetTimeouts(session, 4000, 4000, 5000, 5000);
    std::wstring host = OBF_WSTR("YOUR_PROJECT.firebaseio.com");
    std::wstring path = OBF_WSTR("/stats/appUsage.json");
    HINTERNET conn = WinHttpConnect(session, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (conn) {
        HINTERNET req = WinHttpOpenRequest(conn, L"POST", path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
        if (req) {
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            std::string payload = std::string("{\"action\":\"") + action + "\",\"ts\":" + std::to_string(ms) + "}";
            const wchar_t* hdr = L"Content-Type: application/json\r\n";
            if (WinHttpSendRequest(req, hdr, -1L, const_cast<char*>(payload.data()), static_cast<DWORD>(payload.size()), static_cast<DWORD>(payload.size()), 0)) {
                WinHttpReceiveResponse(req, nullptr);
            }
            WinHttpCloseHandle(req);
        }
        WinHttpCloseHandle(conn);
    }
    WinHttpCloseHandle(session);
}

void SetStartupRegistry(bool enable, const std::wstring& exePath = L"") {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &hKey) != ERROR_SUCCESS) {
        return;
    }
    if (enable && !exePath.empty()) {
        std::wstring val = L"\"" + exePath + L"\" --startup-check";
        RegSetValueExW(hKey, L"RobRunTurkceYama", 0, REG_SZ, reinterpret_cast<const BYTE*>(val.c_str()),
                       static_cast<DWORD>((val.size() + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(hKey, L"RobRunTurkceYama");
    }
    RegCloseKey(hKey);
}

bool EnsureEngineExtracted(std::wstring& outExe) {
    std::wstring dir = LocalAppDir();
    if (dir.empty()) return false;
    outExe = dir + L"\\engine.exe";

    HMODULE mod = GetModuleHandleW(nullptr);
    HRSRC res = FindResourceW(mod, MAKEINTRESOURCEW(105), RT_RCDATA);
    if (!res) return false;
    HGLOBAL loaded = LoadResource(mod, res);
    DWORD size = SizeofResource(mod, res);
    const void* data = loaded ? LockResource(loaded) : nullptr;
    if (!data || size == 0) return false;

    std::error_code ec;
    if (fs::exists(outExe, ec) && fs::file_size(outExe, ec) == size) {
        return true;
    }

    std::ofstream f(fs::path(outExe), std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(static_cast<const char*>(data), size);
    return f.good();
}

void RegisterStartupCopy() {
    wchar_t exePath[32768]{};
    GetModuleFileNameW(nullptr, exePath, ARRAYSIZE(exePath));
    std::wstring dir = LocalAppDir();
    if (dir.empty()) return;

    std::wstring targetExe = dir + L"\\RobRunTurkceYama.exe";
    std::error_code ec;
    if (fs::path(exePath) != fs::path(targetExe)) {
        fs::copy_file(exePath, targetExe, fs::copy_options::overwrite_existing, ec);
    }

    SetStartupRegistry(true, targetExe);
}

void SetStatus(const std::string& status, const std::string& state = "") {
    std::lock_guard<std::mutex> lock(g.mutex);
    g.statusText = status;
    if (!state.empty()) g.patchState = state;
}

void ProcessLine(const std::string& rawLine, int mode) {
    // Ensure valid UTF-8 (if engine stdout arrives in CP1254/ACP, convert to UTF-8 so Turkish chars never become '?')
    std::string line = rawLine;
    int wlen = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, rawLine.c_str(), static_cast<int>(rawLine.size()), nullptr, 0);
    if (wlen == 0 && !rawLine.empty()) {
        int awlen = MultiByteToWideChar(CP_ACP, 0, rawLine.c_str(), static_cast<int>(rawLine.size()), nullptr, 0);
        if (awlen > 0) {
            std::wstring w(awlen, L'\0');
            MultiByteToWideChar(CP_ACP, 0, rawLine.c_str(), static_cast<int>(rawLine.size()), w.data(), awlen);
            int ulen = WideCharToMultiByte(CP_UTF8, 0, w.data(), awlen, nullptr, 0, nullptr, nullptr);
            if (ulen > 0) {
                line.assign(ulen, '\0');
                WideCharToMultiByte(CP_UTF8, 0, w.data(), awlen, line.data(), ulen, nullptr, nullptr);
            }
        }
    }
    if (line.rfind("PROGRESS:", 0) == 0) {
        try {
            g.progress.store(std::stof(line.substr(9)));
        } catch (...) {}
    } else if (line.rfind("STATUS:", 0) == 0) {
        SetStatus(line.substr(7));
    } else if (line.rfind("STATE:", 0) == 0) {
        std::string st = line.substr(6);
        if (st == "INSTALLED") {
            g.progress.store(1.0f);
            SetStatus("Yama kurulu ve güncel.", "KURULU VE GÜNCEL");
        } else if (st == "OUTDATED") {
            g.progress.store(0.0f);
            SetStatus("Oyun güncellenmiş, yama yenilenmeli.", "GÜNCELLEME GEREKLİ");
        } else {
            g.progress.store(0.0f);
            SetStatus("Yama henüz kurulmamış.", "KURULU DEĞİL");
        }
    } else if (line.rfind("ERR:", 0) == 0) {
        g.hasError.store(true);
        SetStatus("Hata: " + line.substr(4), mode == 0 ? "HATA" : "İŞLEM BAŞARISIZ");
    }
}

int RunEngineSync(int mode, bool streamToUi) {
    std::wstring engineExe;
    if (!EnsureEngineExtracted(engineExe)) return -1;

    std::wstring args = L"\"" + engineExe + L"\" --konsol";
    if (mode == 0) args += L" --durum";
    else if (mode == 2) args += L" --kaldir";

    SetEnvironmentVariableW(L"PYTHONIOENCODING", L"utf-8");
    SetEnvironmentVariableW(L"PYTHONUTF8", L"1");

    SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
    HANDLE readPipe = nullptr, writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &sa, 0)) return -1;
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = writePipe;
    si.hStdError = writePipe;

    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessW(nullptr, args.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(writePipe);
    if (!ok) { CloseHandle(readPipe); return -1; }

    std::string buf, lastState;
    char chunk[512];
    DWORD readBytes = 0;
    while (ReadFile(readPipe, chunk, sizeof(chunk), &readBytes, nullptr) && readBytes > 0) {
        buf.append(chunk, readBytes);
        size_t pos;
        while ((pos = buf.find('\n')) != std::string::npos) {
            std::string line = buf.substr(0, pos);
            buf.erase(0, pos + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.empty()) {
                if (line.rfind("STATE:", 0) == 0) lastState = line.substr(6);
                if (streamToUi) ProcessLine(line, mode);
            }
        }
    }
    if (!buf.empty()) {
        if (buf.back() == '\r') buf.pop_back();
        if (buf.rfind("STATE:", 0) == 0) lastState = buf.substr(6);
        if (streamToUi) ProcessLine(buf, mode);
    }

    CloseHandle(readPipe);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    if (mode == 0 && lastState == "OUTDATED") return 2;
    return static_cast<int>(exitCode);
}

} // namespace

void LaunchSteamGame() {
    std::wstring url = OBF_WSTR("steam://rungameid/4262460");
    ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void OpenDiscord() {
    std::wstring url = OBF_WSTR("https://discord.gg/WAB4JcntbH");
    ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void OpenLogFile() {
    wchar_t tmp[MAX_PATH]{};
    GetTempPathW(MAX_PATH, tmp);
    std::wstring logPath = std::wstring(tmp) + L"robrun_tr_installer.log";
    ShellExecuteW(nullptr, L"open", logPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void AcceptDisclaimer() {
    std::wstring dir = LocalAppDir();
    if (!dir.empty()) {
        std::ofstream f(fs::path(dir + L"\\disclaimer_accepted.flag"), std::ios::trunc);
        f << "1";
    }
    g.showDisclaimer.store(false);
}

void DismissWhatsNew() {
    g.showWhatsNew.store(false);
}

bool HttpDownloadToFile(const std::wstring& url, const std::wstring& destPath) {
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256]{}, path[2048]{};
    uc.lpszHostName = host; uc.dwHostNameLength = ARRAYSIZE(host);
    uc.lpszUrlPath = path; uc.dwUrlPathLength = ARRAYSIZE(path);
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) return false;

    HINTERNET session = WinHttpOpen(L"RobRunTR/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return false;
    WinHttpSetTimeouts(session, 10000, 10000, 15000, 120000);
    DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(session, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy));

    HINTERNET conn = WinHttpConnect(session, host, uc.nPort, 0);
    if (!conn) { WinHttpCloseHandle(session); return false; }

    DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET req = WinHttpOpenRequest(conn, L"GET", path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!req) { WinHttpCloseHandle(conn); WinHttpCloseHandle(session); return false; }

    bool ok = false;
    if (WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(req, nullptr)) {
        DWORD status = 0, sz = sizeof(status);
        WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX);
        if (status >= 200 && status < 300) {
            DWORD contentLen = 0, clSz = sizeof(contentLen);
            WinHttpQueryHeaders(req, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &contentLen, &clSz, WINHTTP_NO_HEADER_INDEX);

            std::ofstream fout(std::filesystem::path(destPath), std::ios::binary | std::ios::trunc);
            if (fout.is_open()) {
                DWORD totalRead = 0, avail = 0;
                char headerCheck[2]{};
                while (WinHttpQueryDataAvailable(req, &avail) && avail > 0) {
                    std::vector<char> buf(avail);
                    DWORD read = 0;
                    if (WinHttpReadData(req, buf.data(), avail, &read) && read > 0) {
                        if (totalRead == 0 && read >= 2) {
                            headerCheck[0] = buf[0];
                            headerCheck[1] = buf[1];
                        }
                        fout.write(buf.data(), read);
                        totalRead += read;
                        if (contentLen > 0) {
                            g.updateProgress.store(std::min(0.99f, static_cast<float>(totalRead) / static_cast<float>(contentLen)));
                        }
                    } else break;
                }
                fout.close();
                if (totalRead > 1000000 && headerCheck[0] == 'M' && headerCheck[1] == 'Z') {
                    g.updateProgress.store(1.0f);
                    ok = true;
                }
            }
        }
    }
    WinHttpCloseHandle(req);
    WinHttpCloseHandle(conn);
    WinHttpCloseHandle(session);
    return ok;
}

void StartSelfUpdate() {
    if (g.updateDownloading.exchange(true)) return;
    g.updateProgress.store(0.02f);
    {
        std::lock_guard<std::mutex> lock(g.mutex);
        g.updateStatusText = "Yeni sürüm indiriliyor…";
    }
    std::thread([]() {
        std::string rawUrl;
        {
            std::lock_guard<std::mutex> lock(g.mutex);
            rawUrl = g.updateExeUrl;
        }
        std::string norm = NormalizeDriveUrl(rawUrl);
        if (norm.empty()) {
            std::lock_guard<std::mutex> lock(g.mutex);
            g.updateStatusText = "Güncelleme bağlantısı bulunamadı!";
            g.updateDownloading.store(false);
            return;
        }

        std::wstring dir = LocalAppDir();
        CreateDirectoryW(dir.c_str(), nullptr);
        std::wstring newExe = dir + L"\\RobRunTurkceYama_new.exe";
        if (!HttpDownloadToFile(Utf8ToWide(norm), newExe)) {
            DeleteFileW(newExe.c_str());
            std::lock_guard<std::mutex> lock(g.mutex);
            g.updateStatusText = "İndirme başarısız! Lütfen tekrar deneyin veya siteden indirin.";
            g.updateDownloading.store(false);
            return;
        }

        wchar_t curExe[MAX_PATH]{};
        GetModuleFileNameW(nullptr, curExe, MAX_PATH);
        std::wstring batPath = dir + L"\\apply_update.bat";
        {
            auto toU8 = [](const std::wstring& ws) -> std::string {
                if (ws.empty()) return "";
                int n = WideCharToMultiByte(CP_UTF8, 0, ws.data(), static_cast<int>(ws.size()), nullptr, 0, nullptr, nullptr);
                std::string out(n, '\0');
                WideCharToMultiByte(CP_UTF8, 0, ws.data(), static_cast<int>(ws.size()), out.data(), n, nullptr, nullptr);
                return out;
            };
            std::ofstream bat(std::filesystem::path(batPath), std::ios::binary | std::ios::trunc);
            std::string curU8 = toU8(curExe);
            std::string newU8 = toU8(newExe);
            bat << "@echo off\r\n"
                << "chcp 65001 >nul\r\n"
                << "timeout /t 1 /nobreak >nul\r\n"
                << ":waitloop\r\n"
                << "del /f /q \"" << curU8 << "\" >nul 2>&1\r\n"
                << "if exist \"" << curU8 << "\" (\r\n"
                << "  timeout /t 1 /nobreak >nul\r\n"
                << "  goto waitloop\r\n"
                << ")\r\n"
                << "move /y \"" << newU8 << "\" \"" << curU8 << "\" >nul\r\n"
                << "start \"\" \"" << curU8 << "\" --whatsnew\r\n"
                << "del \"%~f0\"\r\n";
        }

        {
            std::lock_guard<std::mutex> lock(g.mutex);
            g.updateStatusText = "Güncelleme uygulanıyor, yeniden başlatılıyor…";
        }
        ShellExecuteW(nullptr, L"open", batPath.c_str(), nullptr, nullptr, SW_HIDE);
        if (gWindow) PostMessageW(gWindow, WM_CLOSE, 0, 0);
    }).detach();
}

void StartRemoteCheck() {
    std::thread([]() {
        FetchFirebaseConfigSync();
        g.remoteChecked.store(true);
    }).detach();
}

void StartPatchAction(int mode) {
    if (g.busy.exchange(true)) return;
    g.hasError.store(false);
    if (mode == 1) {
        g.progress.store(0.05f);
        SetStatus("Yama hazırlanıyor…", "KURULUYOR");
    } else if (mode == 2) {
        g.progress.store(0.15f);
        SetStatus("Yama kaldırılıyor…", "KALDIRILIYOR");
    } else {
        SetStatus("Oyun ve yama durumu kontrol ediliyor…", "KONTROL EDİLİYOR");
    }

    std::thread([mode]() {
        int effectiveMode = mode;
        int exitCode = RunEngineSync(effectiveMode, true);
        if (effectiveMode == 0 && exitCode == 2 && !g.maintenance.load()) {
            effectiveMode = 1;
            g.hasError.store(false);
            g.progress.store(0.05f);
            SetStatus("Oyun güncellenmiş, yama otomatik güncelleniyor…", "GÜNCELLENİYOR");
            exitCode = RunEngineSync(1, true);
        }
        if (exitCode == 0 && effectiveMode == 1) {
            RegisterStartupCopy();
            LogAppUsageSync("install");
            g.progress.store(1.0f);
            SetStatus("Türkçe yama kuruldu! Oyun başlatılıyor…", "KURULU VE GÜNCEL");
            LaunchSteamGame();
            Sleep(500);
            PostMessageW(gWindow, WM_CLOSE, 0, 0);
        } else if (exitCode == 0 && effectiveMode == 2) {
            SetStartupRegistry(false);
            LogAppUsageSync("remove");
            g.progress.store(0.0f);
            SetStatus("Yama kaldırıldı, oyun İngilizceye döndü.", "KURULU DEĞİL");
        } else if (exitCode != 0 && !(effectiveMode == 0 && exitCode == 2) && !g.hasError.load()) {
            g.hasError.store(true);
            SetStatus("İşlem başarısız oldu.", "HATA");
        }
        g.busy.store(false);
    }).detach();
}

bool CreateDeviceD3D(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    D3D_FEATURE_LEVEL level;
    if (FAILED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2,
                                             D3D11_SDK_VERSION, &sd, &gSwapChain, &gDevice, &level, &gContext))) {
        return false;
    }
    ID3D11Texture2D* buffer = nullptr;
    gSwapChain->GetBuffer(0, IID_PPV_ARGS(&buffer));
    if (buffer) {
        gDevice->CreateRenderTargetView(buffer, nullptr, &gRenderTarget);
        buffer->Release();
    }
    return true;
}

void CleanupRenderTarget() {
    if (gRenderTarget) { gRenderTarget->Release(); gRenderTarget = nullptr; }
}

void CreateRenderTarget() {
    ID3D11Texture2D* buffer = nullptr;
    gSwapChain->GetBuffer(0, IID_PPV_ARGS(&buffer));
    if (buffer) {
        gDevice->CreateRenderTargetView(buffer, nullptr, &gRenderTarget);
        buffer->Release();
    }
}

void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (gSwapChain) { gSwapChain->Release(); gSwapChain = nullptr; }
    if (gContext) { gContext->Release(); gContext = nullptr; }
    if (gDevice) { gDevice->Release(); gDevice = nullptr; }
}

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_NCHITTEST) {
        LRESULT hit = DefWindowProcW(hWnd, msg, wParam, lParam);
        if (hit != HTCLIENT) return hit;
        POINT p{ static_cast<SHORT>(LOWORD(lParam)), static_cast<SHORT>(HIWORD(lParam)) };
        ScreenToClient(hWnd, &p);
        RECT client{};
        GetClientRect(hWnd, &client);
        if (p.y >= 0 && p.y < 58 && p.x >= 0 && p.x < client.right - 100) return HTCAPTION;
        return HTCLIENT;
    }
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) return true;
    switch (msg) {
        case WM_GETMINMAXINFO: {
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
            mmi->ptMinTrackSize.x = 640;
            mmi->ptMinTrackSize.y = 520;
            HMONITOR mon = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
            MONITORINFO mi{ sizeof(mi) };
            if (GetMonitorInfoW(mon, &mi)) {
                mmi->ptMaxPosition.x = mi.rcWork.left - mi.rcMonitor.left;
                mmi->ptMaxPosition.y = mi.rcWork.top - mi.rcMonitor.top;
                mmi->ptMaxSize.x = mi.rcWork.right - mi.rcWork.left;
                mmi->ptMaxSize.y = mi.rcWork.bottom - mi.rcWork.top;
            }
            return 0;
        }
        case WM_SIZE:
            if (gDevice && wParam != SIZE_MINIMIZED) {
                CleanupRenderTarget();
                gSwapChain->ResizeBuffers(0, LOWORD(lParam), HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
                CreateRenderTarget();
            }
            return 0;
        case WM_SYSCOMMAND:
            if ((wParam & 0xfff0) == SC_KEYMENU) return 0;
            break;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR cmdLine, int) {
    if (IsAnalysisEnvironment()) {
        ExitProcess(0);
    }
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    if (cmdLine && wcsstr(cmdLine, L"--startup-check") != nullptr) {
        FetchFirebaseConfigSync();
        if (!g.maintenance.load() && RunEngineSync(0, false) == 2) {
            RunEngineSync(1, false);
        }
        CoUninitialize();
        return 0;
    }

    if (cmdLine && wcsstr(cmdLine, L"--baslat") != nullptr) {
        g.autoLaunch.store(true);
    }

    {
        std::wstring dir = LocalAppDir();
        std::error_code ec;
        g.showDisclaimer.store(!fs::exists(dir + L"\\disclaimer_accepted.flag", ec));
    }

    WNDCLASSEXW wc{ sizeof(wc), CS_CLASSDC, WndProc, 0, 0, instance, nullptr, nullptr, nullptr, nullptr, L"RobRunTRWindow", nullptr };
    RegisterClassExW(&wc);

    const int windowW = 940, windowH = 660;
    const int windowX = (GetSystemMetrics(SM_CXSCREEN) - windowW) / 2;
    const int windowY = (GetSystemMetrics(SM_CYSCREEN) - windowH) / 2;
    gWindow = CreateWindowW(wc.lpszClassName, kWindowTitle, WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX,
                            windowX, windowY, windowW, windowH, nullptr, nullptr, wc.hInstance, nullptr);

    if (!CreateDeviceD3D(gWindow)) {
        CleanupDeviceD3D();
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    MARGINS margins{ -1 };
    DwmExtendFrameIntoClientArea(gWindow, &margins);
    BOOL dark = TRUE;
    DwmSetWindowAttribute(gWindow, 20, &dark, sizeof(dark));
    ShowWindow(gWindow, SW_SHOWDEFAULT);
    UpdateWindow(gWindow);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;
    MenuApplyStyle();
    ImGui_ImplWin32_Init(gWindow);
    ImGui_ImplDX11_Init(gDevice, gContext);

    StartRemoteCheck();
    StartPatchAction(g.autoLaunch.load() ? 1 : 0);

    MSG msg{};
    const auto appStarted = std::chrono::steady_clock::now();
    while (msg.message != WM_QUIT) {
        if (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            continue;
        }
        auto now = std::chrono::steady_clock::now();
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        if (std::chrono::duration<float>(now - appStarted).count() < 1.10f) {
            DrawStartup();
        } else if (g.maintenance.load()) {
            DrawMaintenance();
        } else if (g.updateRequired.load()) {
            DrawUpdate();
        } else if (g.showWhatsNew.load()) {
            DrawWhatsNew();
        } else if (g.showDisclaimer.load()) {
            DrawDisclaimer();
        } else {
            DrawMain();
        }

        ImGui::Render();
        const float clear[4] = { kBg.x, kBg.y, kBg.z, kBg.w };
        gContext->OMSetRenderTargets(1, &gRenderTarget, nullptr);
        gContext->ClearRenderTargetView(gRenderTarget, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        gSwapChain->Present(1, 0);
    }

    MenuShutdownAssets();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDeviceD3D();
    DestroyWindow(gWindow);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    CoUninitialize();
    return 0;
}
