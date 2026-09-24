#pragma once

// Open a console on the host process via AllocConsole for writing debug prints
// (set by CMake: -DCAT_BRIDGE_CONSOLE=ON)
// #define ENABLE_CONSOLE_LOGGING
// Write debug prints to Mewjector's shared log file, if Mewjector is present
#define ENABLE_MEWJECTOR_LOGGING

#ifdef ENABLE_CONSOLE_LOGGING
#include "amoeboid.hpp"
#include "utilities/strings.hpp"
#endif
#ifdef ENABLE_MEWJECTOR_LOGGING
#include "utilities/mewjector_support.h"
#endif

#include <cstdint>
#include <string>
#include <format>
#include <chrono>

#include <windows.h>

// Debug printing facilities.
//
// polymeric 2026

#ifdef ENABLE_CONSOLE_LOGGING
#define ALLOC_CONSOLE() AllocConsole()
#define FREE_CONSOLE() FreeConsole()
#else
#define ALLOC_CONSOLE()
#define FREE_CONSOLE()
#endif

enum class DebugConsoleLevel : uint8_t {
    Debug = 0,
    Info = 1,
    Warn = 2,
    Error = 3
};

struct DebugConsoleMessage {
    std::string message;
    std::chrono::time_point<std::chrono::system_clock> timestamp;
    DebugConsoleLevel level;
    bool truncated;
};

// DebugConsole, name shortened for easy retrieval from the global scope
class D {
public:
    static D &get() {
        static D d;
        return d;
    }

    template<class... Args>
    void log(DebugConsoleLevel level, std::format_string<Args...> fmt, Args&&... args) {
        (void)level; // TODO write severity
        std::string multibyte = std::format(fmt, std::forward<Args>(args)...);
        #ifdef ENABLE_CONSOLE_LOGGING
        auto now = std::chrono::system_clock::now();
        std::wstring wide;
        wide = convert_utf8_string_to_utf16_wstring(std::format("{} - {:%F %T} - {}\n", MOD_IDENTIFIER, now, multibyte));
        WriteConsoleW(GetStdHandle(STD_OUTPUT_HANDLE), wide.data(), static_cast<DWORD>(wide.length()), NULL, NULL);
        #endif
        #ifdef ENABLE_MEWJECTOR_LOGGING
        if(const MewjectorAPI *mj = MJ_SUPPORT_GetAPI(); mj != NULL) {
            mj->Log(MJ_SUPPORT_GetOwner(), "%s", multibyte.c_str());
        }
        #endif
    }

    template<class... Args>
    static void debug(std::format_string<Args...> fmt, Args&&... args) {
        D::get().log(DebugConsoleLevel::Debug, fmt, std::forward<Args>(args)...);
    }

    template<class... Args>
    static void info(std::format_string<Args...> fmt, Args&&... args) {
        D::get().log(DebugConsoleLevel::Info, fmt, std::forward<Args>(args)...);
    }

    template<class... Args>
    static void warn(std::format_string<Args...> fmt, Args&&... args) {
        D::get().log(DebugConsoleLevel::Warn, fmt, std::forward<Args>(args)...);
    }

    template<class... Args>
    static void error(std::format_string<Args...> fmt, Args&&... args) {
        D::get().log(DebugConsoleLevel::Error, fmt, std::forward<Args>(args)...);
    }

private:
    D() = default;
    ~D() = default;
    D(const D&) = delete;
    D& operator=(const D&) = delete;
    D(D&&) = delete;
    D& operator=(D&&) = delete;
};

#undef ENABLE_CONSOLE_LOGGING
#undef ENABLE_MEWJECTOR_LOGGING
