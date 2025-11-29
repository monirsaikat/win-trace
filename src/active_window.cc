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

#elif __linux__

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace {

class DisplayHandle {
   public:
    DisplayHandle() : display_(XOpenDisplay(nullptr)) {}
    ~DisplayHandle() {
        if (display_) {
            XCloseDisplay(display_);
        }
    }

    Display* get() const { return display_; }
    bool valid() const { return display_ != nullptr; }

   private:
    Display* display_;
};

std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

Window QueryActiveWindow(Display* display) {
    Atom activeAtom = XInternAtom(display, "_NET_ACTIVE_WINDOW", True);
    if (activeAtom == None) {
        return 0;
    }
    Atom actualType;
    int actualFormat;
    unsigned long itemCount = 0;
    unsigned long bytesLeft = 0;
    unsigned char* data = nullptr;
    if (XGetWindowProperty(display, DefaultRootWindow(display), activeAtom, 0, (~0L), False,
                           AnyPropertyType, &actualType, &actualFormat, &itemCount, &bytesLeft,
                           &data) != Success ||
        !data || itemCount == 0) {
        if (data) {
            XFree(data);
        }
        return 0;
    }
    Window window = 0;
    if (actualFormat == 32) {
        window = reinterpret_cast<unsigned long*>(data)[0];
    }
    XFree(data);
    return window;
}

bool QueryWindowPid(Display* display, Window window, pid_t& pid) {
    Atom pidAtom = XInternAtom(display, "_NET_WM_PID", True);
    if (pidAtom == None) {
        return false;
    }
    Atom actualType;
    int actualFormat;
    unsigned long itemCount = 0;
    unsigned long bytesLeft = 0;
    unsigned char* data = nullptr;
    if (XGetWindowProperty(display, window, pidAtom, 0, 1, False, XA_CARDINAL, &actualType,
                           &actualFormat, &itemCount, &bytesLeft, &data) != Success ||
        !data || itemCount == 0 || actualFormat != 32) {
        if (data) {
            XFree(data);
        }
        return false;
    }
    pid = static_cast<pid_t>(reinterpret_cast<unsigned long*>(data)[0]);
    XFree(data);
    return pid > 0;
}

std::string ReadUtf8Property(Display* display, Window window, const char* name) {
    Atom property = XInternAtom(display, name, True);
    if (property == None) {
        return std::string();
    }
    Atom utf8Type = XInternAtom(display, "UTF8_STRING", False);
    Atom actualType;
    int actualFormat;
    unsigned long itemCount = 0;
    unsigned long bytesLeft = 0;
    unsigned char* data = nullptr;
    if (XGetWindowProperty(display, window, property, 0, (~0L), False,
                           utf8Type != None ? utf8Type : AnyPropertyType, &actualType,
                           &actualFormat, &itemCount, &bytesLeft, &data) != Success ||
        !data || itemCount == 0) {
        if (data) {
            XFree(data);
        }
        return std::string();
    }
    std::string value(reinterpret_cast<char*>(data), itemCount);
    XFree(data);
    return value;
}

std::string QueryWindowTitle(Display* display, Window window) {
    std::string title = ReadUtf8Property(display, window, "_NET_WM_NAME");
    if (!title.empty()) {
        return title;
    }

    XTextProperty textProp;
    if (XGetWMName(display, window, &textProp) != 0 && textProp.value) {
        std::string fallback(reinterpret_cast<char*>(textProp.value),
                             textProp.nitems * textProp.format / 8);
        XFree(textProp.value);
        return fallback;
    }
    return std::string();
}

WindowBounds ReadWindowBounds(Display* display, Window window) {
    WindowBounds bounds;
    XWindowAttributes attributes;
    if (XGetWindowAttributes(display, window, &attributes) == 0) {
        return bounds;
    }
    bounds.width = attributes.width;
    bounds.height = attributes.height;

    Window child;
    int x = 0;
    int y = 0;
    if (XTranslateCoordinates(display, window, DefaultRootWindow(display), 0, 0, &x, &y,
                              &child) != 0) {
        bounds.x = x;
        bounds.y = y;
    } else {
        bounds.x = attributes.x;
        bounds.y = attributes.y;
    }
    return bounds;
}

std::string ReadFirstLine(const std::string& path) {
    std::ifstream file(path);
    std::string line;
    if (file && std::getline(file, line)) {
        return line;
    }
    return std::string();
}

std::string ReadProcessName(pid_t pid) {
    std::string path = "/proc/" + std::to_string(pid) + "/comm";
    std::string name = ReadFirstLine(path);
    if (!name.empty() && name.back() == '\n') {
        name.pop_back();
    }
    return ToLower(name);
}

std::string ReadExePath(pid_t pid) {
    std::string linkPath = "/proc/" + std::to_string(pid) + "/exe";
    std::vector<char> buffer(PATH_MAX, '\0');
    ssize_t copied = readlink(linkPath.c_str(), buffer.data(), buffer.size() - 1);
    if (copied <= 0) {
        return std::string();
    }
    buffer[static_cast<size_t>(copied)] = '\0';
    return std::string(buffer.data());
}

uint64_t ReadMemoryUsage(pid_t pid) {
    std::string path = "/proc/" + std::to_string(pid) + "/statm";
    std::ifstream file(path);
    long totalPages = 0;
    long rssPages = 0;
    if (!(file >> totalPages >> rssPages)) {
        return 0;
    }
    long pageSize = sysconf(_SC_PAGESIZE);
    if (pageSize <= 0) {
        return 0;
    }
    return static_cast<uint64_t>(rssPages) * static_cast<uint64_t>(pageSize);
}

std::string ExtractNameFromPath(const std::string& path) {
    if (path.empty()) {
        return std::string();
    }
    size_t pos = path.find_last_of("/\\");
    if (pos != std::string::npos && pos + 1 < path.size()) {
        return ToLower(path.substr(pos + 1));
    }
    return ToLower(path);
}

}  // namespace

bool GetActiveWindowInfo(ActiveWindowInfo& info) {
    DisplayHandle display;
    if (!display.valid()) {
        return false;
    }

    Window window = QueryActiveWindow(display.get());
    if (window == 0) {
        return false;
    }

    pid_t pid = 0;
    if (!QueryWindowPid(display.get(), window, pid)) {
        return false;
    }

    info.windowId = static_cast<uint64_t>(window);
    info.bounds = ReadWindowBounds(display.get(), window);
    info.title = QueryWindowTitle(display.get(), window);
    info.processId = static_cast<unsigned long>(pid);
    info.memoryUsage = ReadMemoryUsage(pid);

    info.exePath = ReadExePath(pid);
    info.processName = ReadProcessName(pid);
    if (info.processName.empty()) {
        info.processName = ExtractNameFromPath(info.exePath);
    }

    info.owner.name = info.processName;
    info.owner.bundleId = info.processName;
    info.owner.path = info.exePath;
    info.owner.processId = info.processId;
    info.browserUrl.clear();
    return true;
}

#else

bool GetActiveWindowInfo(ActiveWindowInfo&) {
    return false;
}

#endif  // _WIN32
