const { getActiveWindow } = require('./');

function summarize(info) {
  return {
    appName: info.appName ?? info.processName ?? null,
    title: info.title ?? null,
    url: info.url ?? null,
    website: info.website ?? null,
    id: info.id ?? null,
    bounds: info.bounds ?? null,
    owner: info.owner ?? null,
    memoryUsage: info.memoryUsage ?? null,
  };
}

function printActiveWindow() {
  const info = getActiveWindow();
  if (!info) {
    console.log('No active window detected');
    return;
  }

  console.log(summarize(info));
  console.log('---------------------------');
}

if (require.main === module) {
  setInterval(printActiveWindow, 1000);
}

module.exports = { printActiveWindow };
