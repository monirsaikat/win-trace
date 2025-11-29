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
#include <X11/Xutil.h>
#include <atspi/atspi.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdint>
#include <deque>
#include <fstream>
#include <string>
#include <vector>

#include <glib.h>

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

void FreeGError(GError*& error) {
    if (error) {
        g_error_free(error);
        error = nullptr;
    }
}

std::string Trim(const std::string& value) {
    const std::string whitespace = " \t\n\r";
    size_t start = value.find_first_not_of(whitespace);
    if (start == std::string::npos) {
        return std::string();
    }
    size_t end = value.find_last_not_of(whitespace);
    return value.substr(start, end - start + 1);
}

bool StartsWithIgnoreCase(const std::string& value, const std::string& prefix) {
    if (value.size() < prefix.size()) {
        return false;
    }
    for (size_t i = 0; i < prefix.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(value[i])) !=
            std::tolower(static_cast<unsigned char>(prefix[i]))) {
            return false;
        }
    }
    return true;
}

bool LooksLikeUrl(const std::string& rawValue) {
    std::string value = Trim(rawValue);
    if (value.empty()) {
        return false;
    }
    std::string lower = ToLower(value);
    static const std::vector<std::string> kKnownSchemes = {
        "http:",   "https:", "file:",  "about:", "chrome:", "googlechrome:",
        "edge:",   "brave:", "opera:", "vivaldi:", "moz-extension:", "gopher:"
    };
    for (const auto& scheme : kKnownSchemes) {
        if (StartsWithIgnoreCase(lower, scheme)) {
            return true;
        }
    }
    if (lower.rfind("www.", 0) == 0) {
        return true;
    }
    if (lower.find("://") != std::string::npos) {
        return true;
    }
    size_t dot = lower.find('.');
    if (dot != std::string::npos && dot + 1 < lower.size() &&
        lower.find(' ') == std::string::npos) {
        return true;
    }
    return false;
}

struct BrowserLocator {
    const char* processName;
    std::vector<std::string> keywords;
};

const BrowserLocator& GetBrowserLocator(const std::string& processName) {
    static const BrowserLocator kLocators[] = {
        {"firefox", {"address", "search with", "url", "awesome bar", "url bar"}},
        {"chrome", {"address and search", "omnibox", "url"}},
        {"chromium", {"address and search", "omnibox", "url"}},
        {"google-chrome", {"address and search", "omnibox", "url"}},
        {"msedge", {"search or enter web address", "address and search", "url"}},
        {"microsoft-edge", {"search or enter web address", "address and search", "url"}},
        {"brave", {"address and search", "url"}},
        {"opera", {"address field", "search", "url"}},
        {"vivaldi", {"address", "search", "url"}},
    };
    for (const auto& locator : kLocators) {
        if (processName == locator.processName) {
            return locator;
        }
    }
    static const BrowserLocator kDefault = {"", {"address", "search", "url", "omnibox"}};
    return kDefault;
}

bool EnsureAtspiInitialized() {
    static bool attempted = false;
    static bool initialized = false;
    if (attempted) {
        return initialized;
    }
    attempted = true;
    initialized = atspi_init();
    return initialized;
}

int ScoreEntryNode(AtspiAccessible* node, const BrowserLocator& locator) {
    if (!node) {
        return 0;
    }
    GError* error = nullptr;
    AtspiRole role = atspi_accessible_get_role(node, &error);
    FreeGError(error);
    if (role != ATSPI_ROLE_ENTRY && role != ATSPI_ROLE_TEXT) {
        return 0;
    }

    AtspiStateSet* states = atspi_accessible_get_state_set(node);
    if (!states) {
        return 0;
    }
    bool editable = atspi_state_set_contains(states, ATSPI_STATE_EDITABLE);
    bool focusable = atspi_state_set_contains(states, ATSPI_STATE_FOCUSABLE);
    bool enabled = atspi_state_set_contains(states, ATSPI_STATE_ENABLED);
    bool focused = atspi_state_set_contains(states, ATSPI_STATE_FOCUSED);
    g_object_unref(states);
    if (!editable || !focusable || !enabled) {
        return 0;
    }

    int score = 1;
    if (focused) {
        score += 2;
    }

    GError* nameError = nullptr;
    gchar* nameChars = atspi_accessible_get_name(node, &nameError);
    FreeGError(nameError);
    std::string lowerName = nameChars ? ToLower(nameChars) : std::string();
    if (nameChars) {
        g_free(nameChars);
    }

    if (!lowerName.empty()) {
        for (const auto& keyword : locator.keywords) {
            if (!keyword.empty() && lowerName.find(keyword) != std::string::npos) {
                score += 4;
                break;
            }
        }
        static const std::vector<std::string> kGenericKeywords = {"address", "search", "url",
                                                                  "location", "omnibox"};
        for (const auto& keyword : kGenericKeywords) {
            if (!keyword.empty() && lowerName.find(keyword) != std::string::npos) {
                score += 2;
                break;
            }
        }
    }

    GError* parentError = nullptr;
    AtspiAccessible* parent = atspi_accessible_get_parent(node, &parentError);
    FreeGError(parentError);
    if (parent) {
        GError* roleError = nullptr;
        AtspiRole parentRole = atspi_accessible_get_role(parent, &roleError);
        FreeGError(roleError);
        if (parentRole == ATSPI_ROLE_TOOL_BAR || parentRole == ATSPI_ROLE_PANEL) {
            score += 1;
        }
        g_object_unref(parent);
    }

    return score;
}

std::string ExtractUrlFromNode(AtspiAccessible* node) {
    if (!node) {
        return std::string();
    }
    AtspiText* textIface = atspi_accessible_get_text_iface(node);
    if (!textIface) {
        return std::string();
    }
    GError* error = nullptr;
    gchar* rawValue = atspi_text_get_text(textIface, 0, -1, &error);
    FreeGError(error);
    if (!rawValue) {
        return std::string();
    }
    std::string value(rawValue);
    g_free(rawValue);
    value = Trim(value);
    if (value.size() > 4096) {
        value.resize(4096);
    }
    if (LooksLikeUrl(value)) {
        return value;
    }
    return std::string();
}

void ReleaseQueue(std::deque<AtspiAccessible*>& queue) {
    while (!queue.empty()) {
        g_object_unref(queue.front());
        queue.pop_front();
    }
}

AtspiAccessible* FindAccessibleForPid(pid_t pid) {
    if (!EnsureAtspiInitialized()) {
        return nullptr;
    }

    const size_t kMaxVisitedPerDesktop = 2048;
    gint desktopCount = atspi_get_desktop_count();

    for (gint desktopIndex = 0; desktopIndex < desktopCount; ++desktopIndex) {
        AtspiAccessible* desktop = atspi_get_desktop(desktopIndex);
        if (!desktop) {
            continue;
        }

        std::deque<AtspiAccessible*> queue;
        queue.push_back(g_object_ref(desktop));
        size_t visited = 0;
        AtspiAccessible* match = nullptr;

        while (!queue.empty() && visited < kMaxVisitedPerDesktop) {
            AtspiAccessible* node = queue.front();
            queue.pop_front();
            ++visited;

            GError* pidError = nullptr;
            gint nodePid = atspi_accessible_get_process_id(node, &pidError);
            FreeGError(pidError);
            if (nodePid == pid) {
                match = node;
                ReleaseQueue(queue);
                break;
            }

            GError* countError = nullptr;
            gint childCount = atspi_accessible_get_child_count(node, &countError);
            FreeGError(countError);
            for (gint i = 0; i < childCount; ++i) {
                GError* childError = nullptr;
                AtspiAccessible* child = atspi_accessible_get_child_at_index(node, i, &childError);
                FreeGError(childError);
                if (child) {
                    queue.push_back(child);
                }
            }
            g_object_unref(node);
        }

        if (match) {
            ReleaseQueue(queue);
            if (desktop != match) {
                g_object_unref(desktop);
            }
            return match;
        }

        ReleaseQueue(queue);
        g_object_unref(desktop);
    }
    return nullptr;
}

std::string SearchAddressBar(AtspiAccessible* root, const BrowserLocator& locator) {
    if (!root) {
        return std::string();
    }

    const size_t kMaxNodes = 6000;
    std::deque<AtspiAccessible*> queue;
    queue.push_back(g_object_ref(root));
    size_t visited = 0;
    int bestScore = 0;
    std::string bestUrl;

    while (!queue.empty() && visited < kMaxNodes) {
        AtspiAccessible* node = queue.front();
        queue.pop_front();
        ++visited;

        int score = ScoreEntryNode(node, locator);
        if (score > 0) {
            std::string value = ExtractUrlFromNode(node);
            if (!value.empty()) {
                if (score > bestScore) {
                    bestScore = score;
                    bestUrl = value;
                    if (score >= 6 && value.find("://") != std::string::npos) {
                        g_object_unref(node);
                        break;
                    }
                }
            }
        }

        GError* countError = nullptr;
        gint childCount = atspi_accessible_get_child_count(node, &countError);
        FreeGError(countError);
        for (gint i = 0; i < childCount; ++i) {
            GError* childError = nullptr;
            AtspiAccessible* child = atspi_accessible_get_child_at_index(node, i, &childError);
            FreeGError(childError);
            if (child) {
                queue.push_back(child);
            }
        }
        g_object_unref(node);
    }

    ReleaseQueue(queue);
    return bestUrl;
}

std::string QueryBrowserUrl(pid_t pid, const std::string& processName) {
    AtspiAccessible* root = FindAccessibleForPid(pid);
    if (!root) {
        return std::string();
    }

    const BrowserLocator& locator = GetBrowserLocator(processName);
    std::string url = SearchAddressBar(root, locator);
    g_object_unref(root);
    return url;
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

    static const std::vector<std::string> kBrowserNames = {
        "firefox", "chrome",  "chromium", "google-chrome", "msedge",
        "brave",   "opera",   "vivaldi",  "microsoft-edge"};
    bool isBrowser =
        std::find(kBrowserNames.begin(), kBrowserNames.end(), info.processName) !=
        kBrowserNames.end();
    if (isBrowser) {
        info.browserUrl = QueryBrowserUrl(static_cast<pid_t>(info.processId), info.processName);
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
