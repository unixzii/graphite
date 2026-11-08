// Runs an Emscripten program (built with -sSINGLE_FILE) in headless Chromium and exits with its status.
// Usage: node run.mjs <program.js> [arguments...]
//
// The page gets a #host element and three functions for the conformance harness (conformance/web.js):
// graphiteSetScale(scale) sets devicePixelRatio, graphiteCapture(x, y, width, height) captures that rectangle of the
// page as premultiplied RGBA8, and graphiteSaveFile(path, base64) writes a file under GRAPHITE_CONFORMANCE_OUT
// (default: <repository>/target/conformance).
import { chromium } from 'playwright';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import zlib from 'node:zlib';
import { fileURLToPath, pathToFileURL } from 'node:url';

const script = path.resolve(process.argv[2] ?? '');
if (!fs.existsSync(script)) {
  console.error('usage: node run.mjs <program.js> [arguments...]');
  process.exit(2);
}
const repository = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const output = process.env.GRAPHITE_CONFORMANCE_OUT ?? path.join(repository, 'target/conformance');
const viewport = { width: 800, height: 600 };

/* Decodes an 8-bit, non-interlaced RGB or RGBA PNG into premultiplied RGBA8. */
function decodePNG(png) {
  let offset = 8, width = 0, height = 0, type = 0;
  const idat = [];
  while (offset < png.length) {
    const length = png.readUInt32BE(offset), name = png.toString('latin1', offset + 4, offset + 8);
    const body = png.subarray(offset + 8, offset + 8 + length);
    if (name === 'IHDR') {
      width = body.readUInt32BE(0);
      height = body.readUInt32BE(4);
      type = body[9];
      if (body[8] !== 8 || (type !== 2 && type !== 6) || body[12] !== 0) throw new Error('unsupported PNG');
    } else if (name === 'IDAT') idat.push(body);
    offset += 12 + length;
  }
  const channels = type === 6 ? 4 : 3, stride = width * channels;
  const raw = zlib.inflateSync(Buffer.concat(idat));
  const rows = Buffer.alloc(stride * height);
  for (let y = 0; y < height; y++) {
    const filter = raw[y * (stride + 1)];
    for (let x = 0; x < stride; x++) {
      const v = raw[y * (stride + 1) + 1 + x];
      const a = x >= channels ? rows[y * stride + x - channels] : 0;
      const b = y ? rows[(y - 1) * stride + x] : 0;
      const c = x >= channels && y ? rows[(y - 1) * stride + x - channels] : 0;
      let p = 0;
      if (filter === 1) p = a;
      else if (filter === 2) p = b;
      else if (filter === 3) p = (a + b) >> 1;
      else if (filter === 4) {
        const q = a + b - c, pa = Math.abs(q - a), pb = Math.abs(q - b), pc = Math.abs(q - c);
        p = pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
      }
      rows[y * stride + x] = (v + p) & 255;
    }
  }
  const rgba = Buffer.alloc(width * height * 4);
  for (let i = 0, j = 0; i < width * height; i++, j += channels) {
    const alpha = channels === 4 ? rows[j + 3] : 255;
    for (let k = 0; k < 3; k++) rgba[i * 4 + k] = Math.round((rows[j + k] * alpha) / 255);
    rgba[i * 4 + 3] = alpha;
  }
  return { width, height, data: rgba };
}

// The page gives the program a host element and reports its exit status. File URLs keep it self-contained.
const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'graphite-web-'));
const page = path.join(dir, 'index.html');
fs.writeFileSync(page, `<!doctype html>
<meta charset="utf-8">
<style>html, body { margin: 0; background: transparent; }</style>
<div id="host" style="width: 400px; height: 300px"></div>
<script>
  var Module = {
    arguments: ${JSON.stringify(process.argv.slice(3))},
    onExit: (status) => { window.graphiteExit = status; },
    onAbort: () => { window.graphiteExit = 134; },
  };
</script>
<script src="${pathToFileURL(script)}"></script>
`);

const browser = await chromium.launch();
let status = 1;
try {
  const context = await browser.newContext({ viewport });
  const tab = await context.newPage();
  const cdp = await context.newCDPSession(tab);
  await cdp.send('Emulation.setDefaultBackgroundColorOverride', { color: { r: 0, g: 0, b: 0, a: 0 } });
  await tab.exposeFunction('graphiteSetScale', (scale) =>
    cdp.send('Emulation.setDeviceMetricsOverride', { ...viewport, deviceScaleFactor: scale, mobile: false }));
  await tab.exposeFunction('graphiteCapture', async (x, y, width, height) => {
    const shot = await cdp.send('Page.captureScreenshot', { format: 'png', clip: { x, y, width, height, scale: 1 } });
    const image = decodePNG(Buffer.from(shot.data, 'base64'));
    return { width: image.width, height: image.height, data: image.data.toString('base64') };
  });
  await tab.exposeFunction('graphiteSaveFile', (relative, base64) => {
    const file = path.join(output, relative);
    fs.mkdirSync(path.dirname(file), { recursive: true });
    fs.writeFileSync(file, Buffer.from(base64, 'base64'));
  });
  tab.on('console', (message) => console.log(message.text()));
  // An uncaught error ends the run at once instead of waiting for an exit that never comes.
  const failed = new Promise((resolve) => tab.on('pageerror', (error) => resolve(error)));
  await tab.goto(pathToFileURL(page).href);
  const exited = tab.waitForFunction(() => window.graphiteExit !== undefined, null, { timeout: 600000, polling: 100 });
  const error = await Promise.race([failed, exited.then(() => null)]);
  if (error) console.error(error.message);
  else status = await tab.evaluate(() => window.graphiteExit);
} catch (error) {
  console.error(error.message);
} finally {
  await browser.close();
  fs.rmSync(dir, { recursive: true, force: true });
}
process.exit(status);
