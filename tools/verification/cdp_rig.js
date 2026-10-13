// Minimal headless-Chrome CDP rig for driving the Wokwi simulator unattended.
// Uses Node's built-in WebSocket (Node >= 22). No third-party browser automation needed.
'use strict';
const { spawn, execSync } = require('child_process');
const fs = require('fs');
const os = require('os');
const path = require('path');
const http = require('http');

// Chrome (or Edge) location: CHROME_PATH wins, then the usual Windows install locations.
function findChrome() {
  const cands = [
    process.env.CHROME_PATH,
    'C:/Program Files/Google/Chrome/Application/chrome.exe',
    'C:/Program Files (x86)/Google/Chrome/Application/chrome.exe',
    (process.env.LOCALAPPDATA || '') + '/Google/Chrome/Application/chrome.exe',
    'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe',
    'C:/Program Files/Microsoft/Edge/Application/msedge.exe',
  ].filter(Boolean);
  const hit = cands.find((p) => fs.existsSync(p));
  if (!hit) throw new Error('Chrome/Edge not found - set CHROME_PATH');
  return hit;
}

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

function getJSON(url) {
  return new Promise((resolve, reject) => {
    http.get(url, (res) => {
      let b = '';
      res.on('data', (d) => (b += d));
      res.on('end', () => { try { resolve(JSON.parse(b)); } catch (e) { reject(e); } });
    }).on('error', reject);
  });
}

class Rig {
  constructor(opts = {}) {
    this.port = opts.port || 9333;
    this.profile = opts.profile || path.join(os.tmpdir(), 'rig-profile-' + this.port);
    this.w = opts.w || 1500;
    this.h = opts.h || 1000;
    this.id = 0;
    this.pending = new Map();
    this.events = [];
  }

  async launch() {
    fs.mkdirSync(this.profile, { recursive: true });
    this.proc = spawn(findChrome(), [
      '--headless=new',
      `--remote-debugging-port=${this.port}`,
      `--user-data-dir=${this.profile}`,
      `--window-size=${this.w},${this.h}`,
      '--no-first-run', '--no-default-browser-check', '--mute-audio',
      '--disable-background-timer-throttling',
      '--disable-renderer-backgrounding',
      '--disable-backgrounding-occluded-windows',
      '--disable-features=CalculateNativeWinOcclusion',
      'about:blank',
    ], { stdio: 'ignore' });
    let targets = null;
    for (let i = 0; i < 60; i++) {
      try { targets = await getJSON(`http://127.0.0.1:${this.port}/json/list`); if (targets.length) break; } catch (e) {}
      await sleep(500);
    }
    if (!targets) throw new Error('Chrome did not start');
    const page = targets.find((t) => t.type === 'page');
    this.ws = new WebSocket(page.webSocketDebuggerUrl);
    await new Promise((res, rej) => { this.ws.onopen = res; this.ws.onerror = rej; });
    this.ws.onmessage = (m) => {
      const msg = JSON.parse(m.data);
      if (msg.id && this.pending.has(msg.id)) {
        const { res, rej } = this.pending.get(msg.id);
        this.pending.delete(msg.id);
        msg.error ? rej(new Error(msg.error.message)) : res(msg.result);
      } else if (msg.method) {
        this.events.push(msg);
        if (this.events.length > 2000) this.events.shift();
      }
    };
    await this.send('Page.enable');
    await this.send('Runtime.enable');
    await this.send('Emulation.setFocusEmulationEnabled', { enabled: true });
    await this.send('Emulation.setDeviceMetricsOverride', { width: this.w, height: this.h, deviceScaleFactor: 1, mobile: false });
    return this;
  }

  send(method, params = {}) {
    const id = ++this.id;
    return new Promise((res, rej) => {
      this.pending.set(id, { res, rej });
      this.ws.send(JSON.stringify({ id, method, params }));
    });
  }

  async goto(url, settleMs = 4000) {
    await this.send('Page.navigate', { url });
    await sleep(settleMs);
  }

  // Evaluate an expression in the page; returns the JSON-serialisable value.
  async eval(expr) {
    const r = await this.send('Runtime.evaluate', { expression: expr, returnByValue: true, awaitPromise: true });
    if (r.exceptionDetails) throw new Error('eval: ' + (r.exceptionDetails.exception && r.exceptionDetails.exception.description || r.exceptionDetails.text));
    return r.result.value;
  }

  async screenshot(file, clip, scale) {
    const params = { format: 'png', captureBeyondViewport: false };
    if (clip) params.clip = { x: clip.x, y: clip.y, width: clip.width, height: clip.height, scale: scale || 1 };
    const r = await this.send('Page.captureScreenshot', params);
    fs.mkdirSync(path.dirname(file), { recursive: true });
    fs.writeFileSync(file, Buffer.from(r.data, 'base64'));
    return file;
  }

  async mouse(type, x, y, extra = {}) {
    await this.send('Input.dispatchMouseEvent', Object.assign({ type, x, y, button: 'left', buttons: type === 'mouseReleased' ? 0 : 1, clickCount: 1 }, extra));
  }
  async click(x, y) {
    await this.mouse('mouseMoved', x, y, { buttons: 0 });
    await this.mouse('mousePressed', x, y);
    await sleep(60);
    await this.mouse('mouseReleased', x, y);
  }
  async drag(x1, y1, x2, y2, steps = 12) {
    await this.mouse('mouseMoved', x1, y1, { buttons: 0 });
    await this.mouse('mousePressed', x1, y1);
    for (let i = 1; i <= steps; i++) {
      await this.mouse('mouseMoved', x1 + ((x2 - x1) * i) / steps, y1 + ((y2 - y1) * i) / steps);
      await sleep(25);
    }
    await this.mouse('mouseReleased', x2, y2);
  }

  async close() {
    try { this.ws && this.ws.close(); } catch (e) {}
    if (this.proc && this.proc.pid) {
      // Kill the whole Chrome process tree, otherwise renderer/GPU children linger on Windows.
      try { execSync('taskkill /PID ' + this.proc.pid + ' /T /F', { stdio: 'ignore' }); } catch (e) {}
    }
  }
}

module.exports = { Rig, sleep };
