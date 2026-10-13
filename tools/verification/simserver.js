// Long-running Wokwi simulation controller (headless Chrome + CDP + MQTT) with a tiny HTTP API.
//   node simserver.js [--keep]      (--keep = do not overwrite project files; run the project as saved)
//
// It opens the public Wokwi project in an ANONYMOUS headless Chrome, types main.ino / diagram.json /
// libraries.txt from this repository into that page's editors (nothing is saved to the Wokwi account),
// presses Play and then exposes the running simulation over http://127.0.0.1:9444 (see README.md):
// serial text, simulation clock, OLED framebuffer, LED / buzzer / servo sampler, switch / pot control,
// and an independent MQTT client on the live broker.
'use strict';
const http = require('http');
const fs = require('fs');
const path = require('path');
const { Rig, sleep } = require('./cdp_rig.js');
const mqttlib = require('mqtt');                       // npm install  (see package.json)

const PORT = 9444;
const OUT = path.join(__dirname, 'out');
const PROJECT_URL = process.env.WOKWI_URL || 'https://wokwi.com/projects/473060533558119425';
const REPO = process.env.RPRMS_REPO || path.resolve(__dirname, '..', '..');
const PREFIX = process.env.MQTT_PREFIX || 'rprms-hl-8842';
const FILES = {
  'sketch.ino': process.env.SKETCH || REPO + '/main.ino',
  'diagram.json': process.env.DIAGRAM || REPO + '/diagram.json',
  'libraries.txt': process.env.LIBS || REPO + '/libraries.txt',
};
const KEEP = process.argv.includes('--keep');

fs.mkdirSync(OUT, { recursive: true });
let rig = null, mq = null;
const mqttLog = [];
const log = (...a) => { const s = new Date().toISOString().slice(11, 23) + ' ' + a.join(' '); console.log(s); fs.appendFileSync(OUT + '/simserver.log', s + '\n'); };

// ---------------------------------------------------------------- page helpers
const waitFor = async (expr, tries = 120, ms = 1000) => {
  for (let i = 0; i < tries; i++) { try { if (await rig.eval(expr)) return true; } catch (e) {} await sleep(ms); }
  return false;
};

async function clickTab(name) {
  return rig.eval(`(() => { const b = [...document.querySelectorAll('button')].find(x => x.className.toString().includes('tabs_tab') && x.textContent.trim() === ${JSON.stringify(name)}); if (!b) return false; b.click(); return true; })()`);
}

async function setModel(text) {
  return rig.eval(`(() => { const ed = monaco.editor.getEditors()[0]; const m = ed.getModel(); m.setValue(${JSON.stringify(text)}); return m.getValue().length; })()`);
}

async function loadFiles() {
  for (const [tab, file] of Object.entries(FILES)) {
    let text = fs.readFileSync(file, 'utf8').replace(/\r\n/g, '\n');
    if (tab === 'libraries.txt') text = text.split('\n').filter((l) => l.trim() && !l.trim().startsWith('#')).join('\n') + '\n';
    if (!(await clickTab(tab))) throw new Error('tab not found: ' + tab);
    await sleep(1500);
    const n = await setModel(text);
    log('loaded', tab, 'chars', n, 'expected', text.length);
    if (n !== text.length) throw new Error('length mismatch for ' + tab);
  }
  await clickTab('sketch.ino');
  await sleep(800);
}

async function installHooks() {
  await rig.eval(`(() => {
    if (window.__hooked) return true; window.__hooked = true;
    window.__builds = [];
    const of = window.fetch;
    window.fetch = async function (...a) {
      const r = await of.apply(this, a);
      try {
        const u = String((a[0] && a[0].url) || a[0]);
        if (u.includes('/build')) { r.clone().arrayBuffer().then((buf) => { let head = ''; try { head = new TextDecoder().decode(buf.slice(0, 2500)); } catch (e) {} window.__builds.push({ t: Date.now(), status: r.status, size: buf.byteLength, head: head.slice(0, 2000) }); }); }
      } catch (e) {}
      return r;
    };
    // Sampler: record every change of LEDs / buzzers / servo with wall + simulation clock.
    window.__ev = []; window.__last = '';
    const clockOf = () => { const e = [...document.querySelectorAll('*')].find(x => x.children.length === 0 && /^\\d\\d:\\d\\d\\.\\d{3}$/.test(x.textContent.trim())); return e ? e.textContent.trim() : ''; };
    const g = (id) => document.getElementById(id);
    setInterval(() => {
      const s = {
        dose: g('ledDose') && g('ledDose').value ? 1 : 0, ok: g('ledOk') && g('ledOk').value ? 1 : 0, net: g('ledNet') && g('ledNet').value ? 1 : 0,
        bz: ['bzTemp', 'bzHr', 'bzSpo2', 'bzSystem'].map((i) => (g(i) && g(i).hasSignal ? 1 : 0)).join(''),
        servo: g('bedServo') ? Math.round(g('bedServo').angle) : -1,
      };
      const key = JSON.stringify(s);
      if (key !== window.__last) { window.__last = key; s.w = Date.now(); s.c = clockOf(); window.__ev.push(s); if (window.__ev.length > 20000) window.__ev.shift(); }
    }, 30);
    return true;
  })()`);
}

async function play() {
  await rig.eval(`document.querySelector('[aria-label="Start the simulation"]') ? (document.querySelector('[aria-label="Start the simulation"]').click(), true) : false`);
}

const statusJS = `(() => {
  const leaf = [...document.querySelectorAll('*')].filter(x => x.children.length === 0);
  const clock = (leaf.find(x => /^\\d\\d:\\d\\d\\.\\d{3}$/.test(x.textContent.trim())) || {}).textContent;
  const speed = (leaf.find(x => /^\\d+%$/.test(x.textContent.trim())) || {}).textContent;
  const pre = document.querySelector('[class*=monitorContainer] pre');
  const compiling = leaf.some(x => /Compiling/.test(x.textContent));
  const builds = (window.__builds || []).map(b => ({status: b.status, size: b.size, head: b.status === 200 && b.size > 100000 ? '(firmware)' : b.head}));
  const running = !!document.querySelector('[aria-label="Stop the simulation"], [aria-label="Pause the simulation"]');
  return { clock, speed, serialLen: pre ? pre.textContent.length : -1, compiling, builds, running, evN: (window.__ev || []).length };
})()`;

// ---------------------------------------------------------------- MQTT
function startMqtt() {
  mq = mqttlib.connect('mqtt://broker.hivemq.com:1883', { clientId: 'rprms-test-' + Math.random().toString(16).slice(2, 8), keepalive: 30, reconnectPeriod: 3000 });
  mq.on('connect', () => { log('mqtt connected'); mq.subscribe(PREFIX + '/feeds/+'); });
  mq.on('message', (topic, payload) => { mqttLog.push({ t: Date.now(), topic: topic.split('/feeds/')[1], v: payload.toString().slice(0, 300) }); if (mqttLog.length > 5000) mqttLog.shift(); });
  mq.on('error', (e) => log('mqtt error', e.message));
}

// ---------------------------------------------------------------- HTTP API
const J = (res, obj, code = 200) => { res.writeHead(code, { 'Content-Type': 'application/json' }); res.end(JSON.stringify(obj, null, 1)); };
const T = (res, text, code = 200) => { res.writeHead(code, { 'Content-Type': 'text/plain; charset=utf-8' }); res.end(text); };

async function readBody(req) { return new Promise((r) => { let b = ''; req.on('data', (d) => (b += d)); req.on('end', () => r(b)); }); }

async function partRect(id) {
  return rig.eval(`(() => { const e = document.getElementById(${JSON.stringify(id)}); if (!e) return null; const b = e.getBoundingClientRect(); return {x: b.x, y: b.y, w: b.width, h: b.height, v: (e.value !== undefined ? e.value : null)}; })()`);
}

const handlers = {
  '/status': async (q, res) => J(res, await rig.eval(statusJS)),
  '/serial': async (q, res) => {
    const t = await rig.eval(`(document.querySelector('[class*=monitorContainer] pre') || {textContent: ''}).textContent.replace(/\\r/g, '')`);
    const from = parseInt(q.from || '0', 10);
    if (q.tail) return T(res, t.split('\n').slice(-parseInt(q.tail, 10)).join('\n'));
    T(res, t.slice(from) + (q.from ? '' : ''));
  },
  '/events': async (q, res) => {
    const since = parseInt(q.since || '0', 10);
    const ev = await rig.eval(`window.__ev.slice(${since})`);
    J(res, { next: since + ev.length, ev });
  },
  '/mqtt': async (q, res) => {
    const since = parseInt(q.since || '0', 10);
    const m = mqttLog.filter((x) => x.t > since && (!q.topic || x.topic.includes(q.topic)));
    J(res, { now: Date.now(), n: m.length, msgs: m.slice(-parseInt(q.last || '60', 10)) });
  },
  '/mqtt/last': async (q, res) => {   // most recent value of every topic seen
    const last = {}; for (const m of mqttLog) last[m.topic] = { v: m.v, age_s: Math.round((Date.now() - m.t) / 1000) };
    J(res, last);
  },
  '/pub': async (q, res) => { mq.publish(PREFIX + '/feeds/' + q.feed, String(q.value), { qos: 0 }); log('PUB', q.feed, q.value); J(res, { ok: true, feed: q.feed, value: q.value }); },
  '/switch': async (q, res) => {
    const want = q.state === 'toggle' ? null : parseInt(q.state, 10);
    const r = await partRect(q.id);
    if (!r) return J(res, { error: 'no such part ' + q.id }, 404);
    if (want !== null && r.v === want) return J(res, { id: q.id, value: r.v, note: 'already' });
    // A mouse click is occasionally lost (page busy / re-layout between press and release; seen once as
    // "SWITCH swTemp -> 0" after a request for 1). Verify the part's value and click again if it did not
    // move, so a lost click can never be mistaken for a firmware that ignored the switch.
    let r2 = r, clicks = 0;
    do {
      const cur = (await partRect(q.id)) || r;                 // re-read the geometry before every click
      await rig.click(cur.x + cur.w / 2, cur.y + cur.h / 2);
      clicks++;
      await sleep(400);
      r2 = await partRect(q.id);
    } while (want !== null && r2.v !== want && clicks < 4);
    const ok = want === null || r2.v === want;
    log('SWITCH', q.id, '->', r2.v, clicks > 1 ? '(' + clicks + ' clicks)' : '', ok ? '' : 'NOT REACHED');
    J(res, { id: q.id, before: r.v, value: r2.v, clicks, ok });
  },
  '/pot': async (q, res) => {
    const v = parseFloat(q.value);
    const out = await rig.eval(`(() => { const e = document.getElementById(${JSON.stringify(q.id)}); if (!e) return null; e.value = ${v}; e.dispatchEvent(new CustomEvent('input', { detail: ${v}, bubbles: true })); return e.value; })()`);
    log('POT', q.id, '->', out);
    J(res, { id: q.id, value: out });
  },
  '/set': async (q, res) => {   // set a part attribute/property generically: /set?id=bodyTemp&prop=temperature&value=39
    const out = await rig.eval(`(() => { const e = document.getElementById(${JSON.stringify(q.id)}); if (!e) return null; e[${JSON.stringify(q.prop)}] = ${JSON.stringify(isNaN(q.value) ? q.value : parseFloat(q.value))}; e.dispatchEvent(new CustomEvent('input', { bubbles: true })); return String(e[${JSON.stringify(q.prop)}]); })()`);
    log('SET', q.id, q.prop, '->', out);
    J(res, { id: q.id, prop: q.prop, value: out });
  },
  '/part': async (q, res) => J(res, await rig.eval(`(() => { const e = document.getElementById(${JSON.stringify(q.id)}); if (!e) return null; const o = {}; for (const k of ['value','angle','hasSignal','temperature','humidity','min','max']) if (e[k] !== undefined) o[k] = e[k]; return o; })()`)),
  '/mouse': async (q, res) => {
    if (q.action === 'click') await rig.click(+q.x, +q.y);
    else if (q.action === 'drag') await rig.drag(+q.x1, +q.y1, +q.x2, +q.y2, 14);
    else return J(res, { error: 'action must be click|drag' }, 400);
    J(res, { ok: true });
  },
  '/rect': async (q, res) => J(res, await rig.eval(`(() => { const e = document.querySelector(${JSON.stringify(q.sel)}); if (!e) return null; const b = e.getBoundingClientRect(); return {x: b.x, y: b.y, w: b.width, h: b.height, min: e.min, max: e.max, v: e.value}; })()`)),
  '/oled': async (q, res) => {
    const name = (q.name || 'oled_' + Date.now()).replace(/[^\w.-]/g, '_');
    // Wait for a stable (untorn) frame: the SSD1306 model repaints progressively while the I2C
    // transfer is in flight, so grab only when two reads 90 ms apart are identical.
    await rig.eval(`(async () => { const e = document.querySelector('wokwi-ssd1306'); const c = e.shadowRoot.querySelector('canvas'); const h = () => { const d = c.getContext('2d').getImageData(0, 0, c.width, c.height).data; let x = 0; for (let i = 0; i < d.length; i += 4) x = (x * 31 + d[i]) | 0; return x; }; let prev = h(); for (let i = 0; i < 14; i++) { await new Promise(r => setTimeout(r, 90)); const cur = h(); if (cur === prev) return true; prev = cur; } return false; })()`);
    const data = await rig.eval(`(() => { const e = document.querySelector('wokwi-ssd1306'); const c = e.shadowRoot.querySelector('canvas'); const k = ${parseInt(q.scale || '8', 10)}; const t = document.createElement('canvas'); t.width = c.width * k; t.height = c.height * k; const x = t.getContext('2d'); x.imageSmoothingEnabled = false; x.fillStyle = '#000'; x.fillRect(0, 0, t.width, t.height); x.drawImage(c, 0, 0, t.width, t.height); return t.toDataURL('image/png'); })()`);
    const file = path.join(OUT, name + '.png');
    fs.writeFileSync(file, Buffer.from(data.split(',')[1], 'base64'));
    J(res, { file });
  },
  '/oledtext': async (q, res) => {   // crude check: returns count of lit pixels per 8-px text row (to confirm the screen is drawing)
    const rows = await rig.eval(`(() => { const e = document.querySelector('wokwi-ssd1306'); const c = e.shadowRoot.querySelector('canvas'); const d = c.getContext('2d').getImageData(0, 0, c.width, c.height).data; const out = []; for (let y = 0; y < c.height; y++) { let n = 0; for (let x = 0; x < c.width; x++) { const i = (y * c.width + x) * 4; if (d[i] + d[i + 1] + d[i + 2] > 300) n++; } out.push(n); } return out; })()`);
    J(res, { litPerRow: rows });
  },
  '/shot': async (q, res) => {
    const name = (q.name || 'shot_' + Date.now()).replace(/[^\w.-]/g, '_') + '.png';
    let clip = null, scale = parseFloat(q.scale || '1');
    if (q.region === 'circuit' || q.region === 'sim') clip = await rig.eval(`(() => { const ids = ['oled','roomEnv','pir','potEcg','potAqi','bedServo','bzSystem','swTemp','swBrokerCut','bodyTemp']; let x0 = 1e9, y0 = 1e9, x1 = 0, y1 = 0; for (const i of ids) { const e = document.getElementById(i); if (!e) continue; const b = e.getBoundingClientRect(); x0 = Math.min(x0, b.x); y0 = Math.min(y0, b.y); x1 = Math.max(x1, b.x + b.width); y1 = Math.max(y1, b.y + b.height); } return {x: Math.max(0, x0 - 30), y: Math.max(0, y0 - 30), width: x1 - x0 + 60, height: y1 - y0 + 60}; })()`);
    else if (q.region === 'serial') clip = await rig.eval(`(() => { const b = document.querySelector('[class*=monitorContainer]').getBoundingClientRect(); return {x: b.x, y: b.y, width: b.width, height: b.height}; })()`);
    else if (q.x) clip = { x: +q.x, y: +q.y, width: +q.w, height: +q.h };
    const file = await rig.screenshot(path.join(OUT, name), clip, scale);
    J(res, { file });
  },
  '/eval': async (q, res, body) => { try { J(res, { result: await rig.eval(body || q.js) }); } catch (e) { J(res, { error: String(e) }, 500); } },
  '/restart': async (q, res) => {
    await rig.eval(`(() => { const b = document.querySelector('[aria-label="Restart the simulation"]'); if (b) { b.click(); return true; } return false; })()`);
    J(res, { ok: true });
  },
  '/quit': async (q, res) => { J(res, { bye: true }); setTimeout(async () => { try { mq && mq.end(true); } catch (e) {} await rig.close(); process.exit(0); }, 300); },
};

function startHttp() {
  http.createServer(async (req, res) => {
    const u = new URL(req.url, 'http://x');
    const q = Object.fromEntries(u.searchParams.entries());
    const h = handlers[u.pathname];
    if (!h) return J(res, { error: 'unknown', routes: Object.keys(handlers) }, 404);
    try { const body = req.method === 'POST' ? await readBody(req) : ''; await h(q, res, body); } catch (e) { log('API error', u.pathname, e.message); try { J(res, { error: String(e) }, 500); } catch (x) {} }
  }).listen(PORT, '127.0.0.1', () => log('API listening on', PORT));
}

// ---------------------------------------------------------------- main
(async () => {
  startHttp();
  startMqtt();
  rig = new Rig({ port: 9333, w: 1500, h: 1000 });
  await rig.launch();
  log('chrome up; opening project');
  await rig.goto(PROJECT_URL, 3000);
  const ready = await waitFor(`typeof monaco !== 'undefined' && monaco.editor.getEditors().length > 0 && !!document.querySelector('[aria-label="Start the simulation"]') && document.querySelectorAll('wokwi-slide-switch').length > 0`, 180, 1000);
  if (!ready) throw new Error('Wokwi did not become ready');
  await sleep(2500);
  await installHooks();
  if (!KEEP) { await loadFiles(); await sleep(3000); }
  await rig.screenshot(OUT + '/00_loaded.png');
  log('pressing Play');
  await play();
  log('READY: controller up, simulation requested');
})().catch((e) => { log('FATAL', e && e.stack || e); });
