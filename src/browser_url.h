#pragma once

#ifdef _WIN32
#include <windows.h>
#endif

#include <string>

std::string GetBrowserUrl(HWND hwnd, const std::string& browserProcessName);
