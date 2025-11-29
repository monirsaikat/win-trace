# my-activewin-addon

Native Node.js addon that returns details about the current foreground window on Windows. It reports the process name, executable path, and window title, and tries to grab the browser URL via Windows UI Automation (best effort for Chromium browsers).

## Build

```sh
npm install
npm run build
```

## Usage

```js
const { getActiveWindow } = require('my-activewin-addon');

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

- Fields provided: `processName`, `exePath`, `title`, `url`, `website`, `appName`, numeric `id` (HWND), `bounds`, `owner` (name/processId/path), and `memoryUsage` (working set bytes).
- Run `node test.js` to stream the active window info every second from Node.
- Windows-only; relies on Win32 APIs and UI Automation.
- URL extraction mainly tested with Chrome in English. Other browsers may return `null`.
- Intended for Electron main process polling (for example every second) to watch the active window.
