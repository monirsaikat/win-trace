#include "browser_url.h"

#ifdef _WIN32

#include <UIAutomation.h>
#include <OleAuto.h>
#include <combaseapi.h>
#include <string>

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

void ReleaseInterface(IUnknown* iface) {
    if (iface) {
        iface->Release();
    }
}

}  // namespace

std::string GetBrowserUrl(HWND hwnd, const std::string& browserProcessName) {
    (void)browserProcessName;
    if (!hwnd) {
        return std::string();
    }

    HRESULT initResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool shouldUninit = false;
    if (SUCCEEDED(initResult) || initResult == RPC_E_CHANGED_MODE) {
        shouldUninit = true;
    } else {
        return std::string();
    }

    IUIAutomation* automation = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&automation));
    if (FAILED(hr) || automation == nullptr) {
        if (shouldUninit) {
            CoUninitialize();
        }
        return std::string();
    }

    IUIAutomationElement* root = nullptr;
    hr = automation->ElementFromHandle(hwnd, &root);
    if (FAILED(hr) || root == nullptr) {
        ReleaseInterface(automation);
        if (shouldUninit) {
            CoUninitialize();
        }
        return std::string();
    }

    VARIANT conditionValue;
    VariantInit(&conditionValue);
    conditionValue.vt = VT_BSTR;
    conditionValue.bstrVal = SysAllocString(L"Address and search bar");

    IUIAutomationCondition* condition = nullptr;
    hr = automation->CreatePropertyCondition(UIA_NamePropertyId, conditionValue, &condition);
    VariantClear(&conditionValue);

    if (FAILED(hr) || condition == nullptr) {
        ReleaseInterface(root);
        ReleaseInterface(automation);
        if (shouldUninit) {
            CoUninitialize();
        }
        return std::string();
    }

    IUIAutomationElement* addressBar = nullptr;
    hr = root->FindFirst(TreeScope_Subtree, condition, &addressBar);

    if (FAILED(hr) || addressBar == nullptr) {
        ReleaseInterface(condition);
        ReleaseInterface(root);
        ReleaseInterface(automation);
        if (shouldUninit) {
            CoUninitialize();
        }
        return std::string();
    }

    IUIAutomationValuePattern* valuePattern = nullptr;
    hr = addressBar->GetCurrentPattern(UIA_ValuePatternId,
                                       reinterpret_cast<IUnknown**>(&valuePattern));
    std::string url;
    if (SUCCEEDED(hr) && valuePattern) {
        BSTR value = nullptr;
        if (SUCCEEDED(valuePattern->get_CurrentValue(&value)) && value != nullptr) {
            url = WideToUtf8(std::wstring(value, SysStringLen(value)));
            SysFreeString(value);
        }
    }

    ReleaseInterface(valuePattern);
    ReleaseInterface(addressBar);
    ReleaseInterface(condition);
    ReleaseInterface(root);
    ReleaseInterface(automation);

    if (shouldUninit) {
        CoUninitialize();
    }

    return url;
}

#else

std::string GetBrowserUrl(HWND, const std::string&) {
    return std::string();
}

#endif  // _WIN32
