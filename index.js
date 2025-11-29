const native = require('node-gyp-build')(__dirname);

function normalizeWebsite(url) {
  try {
    const parsed = new URL(url);
    return `${parsed.protocol}//${parsed.hostname}`;
  } catch {
    return null;
  }
}

function getActiveWindow() {
  const info = native.getActiveWindow();
  if (!info) {
    return null;
  }
  info.website = info.url ? normalizeWebsite(info.url) : null;
  return info;
}

module.exports = { getActiveWindow };
