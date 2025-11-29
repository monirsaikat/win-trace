#include <napi.h>

#include "active_window.h"

Napi::Value GetActiveWindowWrapped(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    ActiveWindowInfo windowInfo;
    if (!GetActiveWindowInfo(windowInfo)) {
        return env.Null();
    }

    Napi::Object result = Napi::Object::New(env);
    result.Set("processName", windowInfo.processName);
    result.Set("exePath", windowInfo.exePath);
    result.Set("title", windowInfo.title);
    result.Set("appName", windowInfo.processName);
    result.Set("processId", Napi::Number::New(env, static_cast<double>(windowInfo.processId)));
    result.Set("id", Napi::Number::New(env, static_cast<double>(windowInfo.windowId)));
    result.Set("memoryUsage",
               Napi::Number::New(env, static_cast<double>(windowInfo.memoryUsage)));

    Napi::Object bounds = Napi::Object::New(env);
    bounds.Set("x", windowInfo.bounds.x);
    bounds.Set("y", windowInfo.bounds.y);
    bounds.Set("width", windowInfo.bounds.width);
    bounds.Set("height", windowInfo.bounds.height);
    result.Set("bounds", bounds);

    Napi::Object owner = Napi::Object::New(env);
    owner.Set("name", windowInfo.owner.name);
    owner.Set("processId",
              Napi::Number::New(env, static_cast<double>(windowInfo.owner.processId)));
    owner.Set("bundleId", windowInfo.owner.bundleId);
    owner.Set("path", windowInfo.owner.path);
    result.Set("owner", owner);

    if (windowInfo.browserUrl.empty()) {
        result.Set("url", env.Null());
    } else {
        result.Set("url", windowInfo.browserUrl);
    }

    return result;
}

Napi::Object Init(Napi::Env env, Napi::Object exports) {
    exports.Set("getActiveWindow", Napi::Function::New(env, GetActiveWindowWrapped));
    return exports;
}

NODE_API_MODULE(activewin, Init)
