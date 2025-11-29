{
  "targets": [
    {
      "target_name": "activewin",
      "sources": [
        "src/addon.cc",
        "src/active_window.cc",
        "src/browser_url.cc"
      ],
      "include_dirs": [
        "<!(node -p \"require('node-addon-api').include_dir\")"
      ],
      "dependencies": [
        "<!(node -p \"require('node-addon-api').gyp\")"
      ],
      "defines": [
        "NAPI_DISABLE_CPP_EXCEPTIONS"
      ],
      "conditions": [
        ["OS=='win'", {
          "libraries": [
            "uiautomationcore.lib"
          ],
          "cflags_cc": ["/std:c++17"],
          "msvs_settings": {
            "VCCLCompilerTool": {
              "AdditionalOptions": ["/std:c++17"]
            }
          }
        }],
        ["OS=='linux'", {
          "libraries": [
            "-lX11"
          ],
          "cflags_cc": ["-std=c++17"]
        }]
      ]
    }
  ]
}
