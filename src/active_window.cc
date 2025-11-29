#include "active_window.h"

#ifdef _WIN32

#include <Psapi.h>
#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

#include "browser_url.h"

namespace {

std::string WideToUtf8(const std::wstring& input) {
    if (input.empty()) {
        return std::string();
    }
    int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, input.c_str(), static_cast<int>(input.size()),
                                         nullptr, 0, nullptr, nullptr);
    if (sizeNeeded <= 0) {
        return std::string();
    }
    std::string result(sizeNeeded, '\0');
    WideCharToMultiByte(CP_UTF8, 0, input.c_str(), static_cast<int>(input.size()), result.data(),
                        sizeNeeded, nullptr, nullptr);
    return result;
}

std::wstring ReadWindowTitle(HWND hwnd) {
    int length = GetWindowTextLengthW(hwnd);
    if (length <= 0) {
        return std::wstring();
    }

    std::wstring buffer(static_cast<size_t>(length) + 1, L'\0');
    int copied = GetWindowTextW(hwnd, buffer.data(), length + 1);
    if (copied <= 0) {
        return std::wstring();
    }
    buffer.resize(static_cast<size_t>(copied));
    return buffer;
}

bool QueryExePath(HANDLE processHandle, std::wstring& exePath) {
    DWORD size = MAX_PATH;
    exePath.resize(size);
    while (true) {
        DWORD copied = size;
        if (QueryFullProcessImageNameW(processHandle, 0, exePath.data(), &copied)) {
            exePath.resize(copied);
            return true;
        }
        DWORD error = GetLastError();
        if (error == ERROR_INSUFFICIENT_BUFFER) {
            size *= 2;
            exePath.resize(size);
            continue;
        }
        return false;
    }
}

std::string NormalizeProcessName(const std::wstring& source) {
    std::string name = WideToUtf8(source);
    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const std::string exeSuffix = ".exe";
    if (name.size() > exeSuffix.size()) {
        if (name.compare(name.size() - exeSuffix.size(), exeSuffix.size(), exeSuffix) == 0) {
            name.erase(name.size() - exeSuffix.size(), exeSuffix.size());
        }
    }
    return name;
}

WindowBounds ReadWindowBounds(HWND hwnd) {
    WindowBounds bounds;
    RECT rect;
    if (GetWindowRect(hwnd, &rect)) {
        bounds.x = rect.left;
        bounds.y = rect.top;
        bounds.width = rect.right - rect.left;
        bounds.height = rect.bottom - rect.top;
    }
    return bounds;
}

uint64_t ReadMemoryUsage(HANDLE processHandle) {
    PROCESS_MEMORY_COUNTERS_EX counters;
    if (GetProcessMemoryInfo(processHandle,
                             reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                             sizeof(counters))) {
        return static_cast<uint64_t>(counters.WorkingSetSize);
    }
    return 0;
}

}  // namespace

bool GetActiveWindowInfo(ActiveWindowInfo& info) {
    HWND hwnd = GetForegroundWindow();
    if (!hwnd) {
        return false;
    }
    info.windowId = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(hwnd));
    info.bounds = ReadWindowBounds(hwnd);

    std::wstring titleW = ReadWindowTitle(hwnd);
    DWORD processId = 0;
    if (GetWindowThreadProcessId(hwnd, &processId) == 0 || processId == 0) {
        return false;
    }

    HANDLE processHandle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE,
                                       processId);
    if (!processHandle) {
        return false;
    }

    std::wstring exePathW;
    if (!QueryExePath(processHandle, exePathW)) {
        CloseHandle(processHandle);
        return false;
    }

    std::wstring processNameW(MAX_PATH, L'\0');
    DWORD nameLength =
        GetModuleBaseNameW(processHandle, nullptr, processNameW.data(), processNameW.size());
    if (nameLength == 0) {
        CloseHandle(processHandle);
        return false;
    }
    processNameW.resize(nameLength);

    info.exePath = WideToUtf8(exePathW);
    info.title = WideToUtf8(titleW);
    info.processName = NormalizeProcessName(processNameW);
    info.processId = processId;
    info.memoryUsage = ReadMemoryUsage(processHandle);

    CloseHandle(processHandle);

    info.owner.name = info.processName;
    info.owner.bundleId = info.processName;
    info.owner.path = info.exePath;
    info.owner.processId = processId;

    static const std::vector<std::string> kBrowserNames = {"chrome", "msedge", "brave",
                                                           "opera",  "firefox"};
    bool isBrowser = std::find(kBrowserNames.begin(), kBrowserNames.end(), info.processName) !=
                     kBrowserNames.end();

    if (isBrowser) {
        info.browserUrl = GetBrowserUrl(hwnd, info.processName);
    } else {
        info.browserUrl.clear();
    }

    return true;
}

#else

bool GetActiveWindowInfo(ActiveWindowInfo&) {
    return false;
}

#endif  // _WIN32
