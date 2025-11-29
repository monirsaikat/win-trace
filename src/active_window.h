#pragma once

#include <cstdint>
#include <string>
#ifdef _WIN32
#include <windows.h>
#endif

struct WindowBounds {
    long x = 0;
    long y = 0;
    long width = 0;
    long height = 0;
};

struct OwnerInfo {
    std::string name;
    std::string bundleId;
    std::string path;
    unsigned long processId = 0;
};

struct ActiveWindowInfo {
    std::string processName;
    std::string exePath;
    std::string title;
    std::string browserUrl;  // empty when URL is unavailable
    WindowBounds bounds;
    OwnerInfo owner;
    unsigned long processId = 0;
    uint64_t windowId = 0;
    uint64_t memoryUsage = 0;
};

bool GetActiveWindowInfo(ActiveWindowInfo& info);
