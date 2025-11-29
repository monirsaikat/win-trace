# Win Trace

Native Node.js addon that returns details about the current foreground window on Windows and Linux (X11). It reports the process name, executable path, and window title, and tries to grab the browser URL via Windows UI Automation or Linux AT-SPI (best effort for Chromium-family browsers).

## Build

```sh
npm install
npm run build
```

On Linux install the development headers for both X11 and AT-SPI before building (for example `sudo apt install libx11-dev libatspi2.0-dev`). At runtime an X11 or XWayland session is required so `_NET_ACTIVE_WINDOW` and AT-SPI can reach the focused browser window. Most desktop environments already run the `at-spi2-core` accessibility service; if yours disables it, enable accessibility support so URL collection works.

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
- Windows and Linux (X11/XWayland). Linux builds use AT-SPI to read Chromium-, Firefox-, and other GTK-based browser address bars (best effort).
- URL extraction mainly tested with Chrome in English. Other browsers may return `null`.
- Intended for Electron main process polling (for example every second) to watch the active window.
