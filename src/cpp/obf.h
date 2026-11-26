#pragma once
#include <array>
#include <cstddef>
#include <string>
#include <windows.h>

// Compile-time XOR string obfuscation so sensitive strings do not appear in plaintext in IDA/strings
template <std::size_t N, unsigned char Key>
struct ObfString {
    std::array<unsigned char, N> data{};
    constexpr ObfString(const char (&str)[N]) {
        for (std::size_t i = 0; i < N; ++i) {
            data[i] = static_cast<unsigned char>(str[i]) ^ static_cast<unsigned char>(Key + i * 7);
        }
    }
    std::string decode() const {
        std::string out(N - 1, '\0');
        for (std::size_t i = 0; i < N - 1; ++i) {
            out[i] = static_cast<char>(data[i] ^ static_cast<unsigned char>(Key + i * 7));
        }
        return out;
    }
    std::wstring decodeW() const {
        std::string s = decode();
        return std::wstring(s.begin(), s.end());
    }
};

#define OBF_STR(s) ([]() { \
    constexpr ObfString<sizeof(s), static_cast<unsigned char>((__LINE__ * 37) ^ 0xA5)> obf(s); \
    return obf.decode(); \
}())

#define OBF_WSTR(s) ([]() { \
    constexpr ObfString<sizeof(s), static_cast<unsigned char>((__LINE__ * 37) ^ 0xA5)> obf(s); \
    return obf.decodeW(); \
}())

// Anti-debug / anti-analysis checks
inline bool IsAnalysisEnvironment() {
    if (IsDebuggerPresent()) return true;
    BOOL remoteDbg = FALSE;
    if (CheckRemoteDebuggerPresent(GetCurrentProcess(), &remoteDbg) && remoteDbg) return true;
    return false;
}
