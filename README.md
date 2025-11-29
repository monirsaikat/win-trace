# Win Trace

Native Node.js addon that returns details about the current foreground window on Windows and Linux (X11). It reports the process name, executable path, and window title, and on Windows tries to grab the browser URL via UI Automation (best effort for Chromium browsers).

## Build

```sh
npm install
npm run build
```

On Linux make sure the X11 headers/libraries are installed (for example `sudo apt install libx11-dev`). Wayland-only sessions are not supported; run under X11 or an XWayland compatibility layer.

## Usage

```js
const { getActiveWindow } = require('win-trace');

const info = getActiveWindow();
console.log({
  appName: info?.appName,
  title: info?.title,
  url: info?.url,
  id: info?.id,
  bounds: info?.bounds,
  owner: info?.owner,
  memoryUsage: info?.memoryUsage,
  website: info?.website,
});
```

- Fields provided: `processName`, `exePath`, `title`, `url`, `website`, `appName`, numeric `id` (HWND or X11 window id), `bounds`, `owner` (name/processId/path), and `memoryUsage` (working set bytes).
- Run `node test.js` to stream the active window info every second from Node.
- Windows and Linux (X11); Linux builds provide the same window/process metadata but URLs are currently Windows-only.
- URL extraction mainly tested with Chrome in English. Other browsers may return `null`.
- Intended for Electron main process polling (for example every second) to watch the active window.
