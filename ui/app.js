// Tosc UI - SPDX-License-Identifier: AGPL-3.0-or-later
"use strict";

const N = 2048;
const $ = (s) => document.querySelector(s);
const $$ = (s) => [...document.querySelectorAll(s)];
const deg = Math.PI / 180;
const clamp = (x, a, b) => (x < a ? a : x > b ? b : x);
const logMap = (v, lo, hi) => lo * Math.pow(hi / lo, v / 1000);
const logInv = (x, lo, hi) => Math.round(1000 * Math.log(x / lo) / Math.log(hi / lo));
const fmtKm = (m) => (m >= 1000 ? (m / 1000).toFixed(m >= 10000 ? 0 : 1) + " km" : Math.round(m) + " m");
const fmtLL = (lat, lon) => `${Math.abs(lat).toFixed(3)}°${lat >= 0 ? "N" : "S"} ${Math.abs(lon).toFixed(3)}°${lon >= 0 ? "E" : "W"}`;

// ------------------------------------------------------------------ bridge to the C++ engine
const Native = (() => {
  const be = window.__JUCE__ && window.__JUCE__.backend;
  let next = 0;
  const pending = new Map();
  if (be) be.addEventListener("__juce__complete", ({ promiseId, result }) => {
    const r = pending.get(promiseId);
    if (r) { pending.delete(promiseId); r(result); }
  });
  return {
    ok: !!be,
    call: (name, ...params) => be
      ? new Promise((res) => { const id = next++; pending.set(id, res); be.emitEvent("__juce__invoke", { name, params, resultId: id }); })
      : Promise.resolve(null),
    on: (ev, fn) => { if (be) be.addEventListener(ev, fn); },
  };
})();

// ------------------------------------------------------------------ state
const PLACES = [
  { name: "Mount Fuji", note: "Volcano, Japan", lat: 35.36, lon: 138.73, w: 36000, h: 36000, angle: 0 },
  { name: "Grand Canyon", note: "Arizona, USA", lat: 36.10, lon: -112.11, w: 30000, h: 20000, angle: 90 },
  { name: "Everest", note: "Himalaya, Nepal", lat: 27.988, lon: 86.925, w: 24000, h: 24000, angle: 0 },
  { name: "Matterhorn", note: "Alps, Switzerland", lat: 45.976, lon: 7.658, w: 10000, h: 10000, angle: 0 },
  { name: "Kilimanjaro", note: "Tanzania", lat: -3.067, lon: 37.355, w: 40000, h: 40000, angle: 0 },
  { name: "Table Mountain", note: "Cape Town", lat: -33.957, lon: 18.403, w: 14000, h: 12000, angle: 0 },
  { name: "Yosemite Valley", note: "California, USA", lat: 37.735, lon: -119.58, w: 14000, h: 9000, angle: 90 },
  { name: "Uluru", note: "Australia", lat: -25.345, lon: 131.036, w: 7000, h: 5000, angle: 0 },
  { name: "Geirangerfjord", note: "Norway", lat: 62.11, lon: 7.03, w: 24000, h: 16000, angle: 90 },
  { name: "Crater Lake", note: "Oregon, USA", lat: 42.94, lon: -122.105, w: 14000, h: 14000, angle: 0 },
  { name: "Ngorongoro", note: "Crater, Tanzania", lat: -3.18, lon: 35.56, w: 32000, h: 32000, angle: 0 },
  { name: "Torres del Paine", note: "Patagonia, Chile", lat: -50.98, lon: -73.02, w: 26000, h: 20000, angle: 0 },
  { name: "Santorini", note: "Caldera, Greece", lat: 36.405, lon: 25.40, w: 17000, h: 17000, angle: 0 },
  { name: "Etna", note: "Volcano, Sicily", lat: 37.75, lon: 15.00, w: 40000, h: 40000, angle: 0 },
  { name: "Mauna Kea", note: "Hawaiʻi", lat: 19.70, lon: -155.50, w: 90000, h: 90000, angle: 0 },
  { name: "Monument Valley", note: "Utah, USA", lat: 36.99, lon: -110.10, w: 18000, h: 12000, angle: 0 },
];

const S = {
  region: { ...PLACES[0] },     // the box on the map (follows the map centre)
  gen: null,                    // region the current table came from
  name: "Mount Fuji",
  frames: 64, sea: "flat", gamma: 0.5, step: 0.2, smooth: 1,
  raw: null, rawKey: "", F: 0, R: null, seaTop: 0, hmin: 0, hmax: 0, mpp: 0,
  livePos: 0.5, voices: 0,
  params: {}, octave: 3, hold: false, held: new Set(), exp: "abs", fmt: "wt",
};

function setStatus(t, err) { const el = $("#status"); el.textContent = t || ""; el.classList.toggle("err", !!err); }

// ------------------------------------------------------------------ geo + elevation tiles
function worldPx(lon, lat, z) {
  const s = 256 * Math.pow(2, z), sl = Math.sin(lat * deg);
  return [(lon + 180) / 360 * s, (0.5 - Math.log((1 + sl) / (1 - sl)) / (4 * Math.PI)) * s];
}
const pxPerMeter = (lat, z) => 256 * Math.pow(2, z) / (2 * Math.PI * 6378137 * Math.cos(lat * deg));
function axes(r) { const th = r.angle * deg, ux = Math.cos(th), uy = Math.sin(th); return { ux, uy, vx: -uy, vy: ux }; }
function ll(r, s, t) {
  const a = axes(r), e = s * a.ux + t * a.vx, n = s * a.uy + t * a.vy;
  return [r.lat + n / 111320, r.lon + e / (111320 * Math.cos(r.lat * deg))];
}
function tileRange(r, z) {
  const [cx, cy] = worldPx(r.lon, r.lat, z), k = pxPerMeter(r.lat, z), a = axes(r);
  let x0 = Infinity, x1 = -Infinity, y0 = Infinity, y1 = -Infinity;
  for (const sx of [-1, 1]) for (const sy of [-1, 1]) {
    const s = sx * r.w / 2, t = sy * r.h / 2;
    const px = cx + (s * a.ux + t * a.vx) * k, py = cy - (s * a.uy + t * a.vy) * k;
    x0 = Math.min(x0, px); x1 = Math.max(x1, px); y0 = Math.min(y0, py); y1 = Math.max(y1, py);
  }
  const tx0 = Math.floor(x0 / 256), tx1 = Math.floor(x1 / 256), ty0 = Math.floor(y0 / 256), ty1 = Math.floor(y1 / 256);
  return { tx0, tx1, ty0, ty1, count: (tx1 - tx0 + 1) * (ty1 - ty0 + 1) };
}
function chooseZoom(r) {
  let z = Math.ceil(Math.log2(2 * Math.PI * 6378137 * Math.cos(r.lat * deg) / (256 * (r.w / N))));
  z = clamp(z, 1, 15);
  while (z > 1 && tileRange(r, z).count > 64) z--;
  return z;
}
const tileCache = new Map();
function getTile(z, x, y) {
  const n = Math.pow(2, z); if (y < 0 || y >= n) return Promise.resolve(null);
  const key = `${z}/${((x % n) + n) % n}/${y}`;
  if (!tileCache.has(key)) {
    const p = fetch(`https://s3.amazonaws.com/elevation-tiles-prod/terrarium/${key}.png`)
      .then((r) => { if (!r.ok) throw new Error(`HTTP ${r.status}`); return r.blob(); })
      .then((b) => createImageBitmap(b, { colorSpaceConversion: "none", premultiplyAlpha: "none" }));
    p.catch(() => tileCache.delete(key));
    tileCache.set(key, p);
  }
  return tileCache.get(key);
}
async function sampleRegion(r, F) {
  const z = chooseZoom(r), tr = tileRange(r, z);
  const W = (tr.tx1 - tr.tx0 + 1) * 256, H = (tr.ty1 - tr.ty0 + 1) * 256;
  const cv = document.createElement("canvas"); cv.width = W; cv.height = H;
  const g = cv.getContext("2d", { willReadFrequently: true });
  let done = 0;
  const jobs = [];
  for (let ty = tr.ty0; ty <= tr.ty1; ty++) for (let tx = tr.tx0; tx <= tr.tx1; tx++)
    jobs.push(getTile(z, tx, ty).then((bmp) => { if (bmp) g.drawImage(bmp, (tx - tr.tx0) * 256, (ty - tr.ty0) * 256); setStatus(`Loading elevation ${++done}/${tr.count}`); }));
  await Promise.all(jobs);
  const px = g.getImageData(0, 0, W, H).data;
  const E = new Float32Array(W * H);
  for (let i = 0, j = 0; i < E.length; i++, j += 4) E[i] = Math.max(-11000, px[j] * 256 + px[j + 1] + px[j + 2] / 256 - 32768);
  const [cx, cy] = worldPx(r.lon, r.lat, z), k = pxPerMeter(r.lat, z), a = axes(r);
  const ox = cx - tr.tx0 * 256 - 0.5, oy = cy - tr.ty0 * 256 - 0.5;
  const raw = new Float32Array(F * N);
  for (let f = 0; f < F; f++) {
    const t = F > 1 ? -r.h / 2 + (f / (F - 1)) * r.h : 0;
    for (let j = 0; j < N; j++) {
      const s = -r.w / 2 + (j / (N - 1)) * r.w;
      const x = clamp(ox + (s * a.ux + t * a.vx) * k, 0, W - 1.001), y = clamp(oy - (s * a.uy + t * a.vy) * k, 0, H - 1.001);
      const x0 = x | 0, y0 = y | 0, fx = x - x0, fy = y - y0, i0 = y0 * W + x0;
      const top = E[i0] + (E[i0 + 1] - E[i0]) * fx, bot = E[i0 + W] + (E[i0 + W + 1] - E[i0 + W]) * fx;
      raw[f * N + j] = top + (bot - top) * fy;
    }
  }
  return { raw, mpp: 1 / k };
}

// ------------------------------------------------------------------ terrain -> relief (0..1)
function smoothRows(src, F, rad) {
  if (!rad) return src;
  const out = new Float32Array(src.length);
  for (let f = 0; f < F; f++) {
    const o = f * N; let acc = 0;
    for (let j = -rad; j <= rad; j++) acc += src[o + clamp(j, 0, N - 1)];
    for (let j = 0; j < N; j++) {
      out[o + j] = acc / (2 * rad + 1);
      acc += src[o + clamp(j + rad + 1, 0, N - 1)] - src[o + clamp(j - rad, 0, N - 1)];
    }
  }
  return out;
}
function processRaw() {
  const F = S.F, v = smoothRows(S.raw, F, S.smooth), R = new Float32Array(F * N);
  let mn = Infinity, mx = -Infinity, landMin = Infinity;
  for (const x of v) { if (x < mn) mn = x; if (x > mx) mx = x; if (x > 0 && x < landMin) landMin = x; }
  const g = S.gamma, st = S.step;
  const h = (x, lo, hi) => Math.pow(clamp((x - lo) / Math.max(1e-6, hi - lo), 0, 1), g);
  const lo = isFinite(landMin) ? landMin : 0;
  if (S.sea === "off" || mn > 0) {
    for (let i = 0; i < v.length; i++) R[i] = h(v[i], mn, mx);
    S.seaTop = -1;
  } else if (S.sea === "flat") {
    for (let i = 0; i < v.length; i++) R[i] = v[i] <= 0 ? 0 : st + (1 - st) * h(v[i], lo, mx);
    S.seaTop = st > 0 ? st * 0.5 : 1e-6;
  } else {
    const share = 0.3;
    for (let i = 0; i < v.length; i++) {
      const x = v[i];
      R[i] = x <= 0 ? share * Math.pow(clamp((x - mn) / Math.max(1e-6, -mn), 0, 1), 1.5) : share + (1 - share) * (st + (1 - st) * h(x, lo, mx));
    }
    S.seaTop = share;
  }
  S.R = R; S.hmin = mn; S.hmax = mx;
}
function reliefToB64(R) {
  const i16 = new Int16Array(R.length);
  for (let i = 0; i < R.length; i++) i16[i] = Math.round(clamp(R[i], 0, 1) * 32767);
  const u8 = new Uint8Array(i16.buffer);
  let s = "";
  for (let i = 0; i < u8.length; i += 0x8000) s += String.fromCharCode.apply(null, u8.subarray(i, i + 0x8000));
  return btoa(s);
}
function b64ToRelief(b64) {
  const bin = atob(b64), u8 = new Uint8Array(bin.length);
  for (let i = 0; i < bin.length; i++) u8[i] = bin.charCodeAt(i);
  const i16 = new Int16Array(u8.buffer), R = new Float32Array(i16.length);
  for (let i = 0; i < i16.length; i++) R[i] = i16[i] / 32767;
  return R;
}
function regionPayload() {
  const r = S.gen || {};
  return { name: S.name, lat: r.lat, lon: r.lon, width: r.w, height: r.h, angle: r.angle,
           frames: S.F, sea: S.sea, gamma: S.gamma, step: S.step, smooth: S.smooth, mpp: S.mpp, hmin: S.hmin, hmax: S.hmax };
}
async function pushRelief() {
  const res = await Native.call("setRelief", S.F, reliefToB64(S.R), S.name, regionPayload());
  if (Native.ok && res !== true) setStatus("The engine did not accept the table: " + res, true);
}

// ------------------------------------------------------------------ generate
let busy = false;
async function generate() {
  if (busy) return; busy = true;
  const btn = $("#genBtn"); btn.disabled = true; btn.classList.remove("stale");
  try {
    const r = { ...S.region }, F = S.frames, t0 = performance.now();
    const { raw, mpp } = await sampleRegion(r, F);
    S.raw = raw; S.F = F; S.gen = r; S.mpp = mpp;
    S.name = S.pendingName === "Custom" || !S.pendingName ? `Terrain ${fmtLL(r.lat, r.lon)}` : S.pendingName;
    processRaw(); afterTable(); await pushRelief();
    setStatus(`Ready in ${((performance.now() - t0) / 1000).toFixed(1)} s`);
  } catch (e) {
    setStatus("Could not load elevation (" + e.message + "). Check the internet connection.", true);
  } finally { busy = false; btn.disabled = false; }
}
// raw elevation is not saved with the project; fetch it again when the shape is edited
async function ensureRaw() {
  if (S.raw) return true;
  if (!S.gen || S.gen.lat == null) { setStatus("This table came from a file. Generate from the map to reshape it.", true); return false; }
  try { const { raw, mpp } = await sampleRegion(S.gen, S.F); S.raw = raw; S.mpp = mpp; return true; }
  catch (e) { setStatus("Could not reload elevation: " + e.message, true); return false; }
}
let reshapeTimer = 0;
function reshape() {
  clearTimeout(reshapeTimer);
  reshapeTimer = setTimeout(async () => {
    if (!(await ensureRaw())) return;
    processRaw(); afterTable(); pushRelief();
  }, 120);
}
function afterTable() {
  lastMapPos = -1;
  renderStatic();
  updateHere();
}
function updateHere() {
  $("#hereName").textContent = S.name;
  const r = S.gen;
  $("#hereMeta").textContent = r && r.lat != null
    ? `${fmtLL(r.lat, r.lon)} · ${fmtKm(r.w)} × ${fmtKm(r.h)} · ${S.F} frames`
    : `${S.F} frames · from file`;
  $("#frameMeta").textContent = "";
  $("#sizeMeta").textContent = r && r.lat != null ? `${fmtKm(r.w)} × ${fmtKm(r.h)}` : "";
  $("#heightMeta").textContent = r && r.lat != null && S.hmax ? `${Math.round(Math.max(0, S.hmin))}–${Math.round(S.hmax)} m` : "";
  $("#findMeta").textContent = S.mpp ? `≈ ${Math.round(S.mpp)} m data` : "";
}

// ------------------------------------------------------------------ drawing
function lut() {
  const land = [[0, [52, 98, 60]], [0.28, [116, 138, 70]], [0.55, [176, 146, 88]], [0.8, [204, 186, 156]], [1, [246, 242, 234]]];
  const sea = [[0, [8, 22, 44]], [1, [70, 136, 184]]];
  const ramp = (st, u) => {
    for (let i = 1; i < st.length; i++) if (u <= st[i][0]) {
      const [a, ca] = st[i - 1], [b, cb] = st[i], k = (u - a) / (b - a || 1);
      return ca.map((c, m) => c + (cb[m] - c) * k);
    }
    return st[st.length - 1][1];
  };
  const out = new Uint8ClampedArray(256 * 3), seaTop = S.seaTop;
  for (let i = 0; i < 256; i++) {
    const r = i / 255;
    const c = seaTop < 0 ? ramp(land, r) : r < seaTop ? ramp(sea, S.sea === "bathy" ? r / seaTop : 0.3) : ramp(land, (r - seaTop) / (1 - seaTop));
    out.set(c, i * 3);
  }
  return out;
}
function fitCanvas(cv) {
  const d = Math.min(2, window.devicePixelRatio || 1);
  const w = Math.max(2, Math.round(cv.clientWidth * d)), h = Math.max(2, Math.round(cv.clientHeight * d));
  if (cv.width !== w || cv.height !== h) { cv.width = w; cv.height = h; return true; }
  return false;
}
let topImg = null, img3d = null, img3dKey = "";
function geom3d(W, H, F) {
  return (f) => { const t = F > 1 ? f / (F - 1) : 0; return { x0: W * (0.035 + 0.2 * t), y0: H * (0.95 - 0.5 * t), ww: W * 0.74, amp: H * 0.38 }; };
}
function renderStatic() {
  if (!S.R) return;
  const F = S.F, R = S.R, L = lut();
  const c = document.createElement("canvas"); c.width = N; c.height = F;
  const g = c.getContext("2d"), id = g.createImageData(N, F);
  for (let f = 0; f < F; f++) for (let j = 0; j < N; j++) {
    const k = Math.round(clamp(R[f * N + j], 0, 1) * 255) * 3, o = ((F - 1 - f) * N + j) * 4;
    id.data[o] = L[k]; id.data[o + 1] = L[k + 1]; id.data[o + 2] = L[k + 2]; id.data[o + 3] = 255;
  }
  g.putImageData(id, 0, 0); topImg = c;
  img3dKey = "";
}
function render3d(cv) {
  const W = cv.width, H = cv.height, key = `${W}x${H}`;
  if (img3d && img3dKey === key) return;
  const off = document.createElement("canvas"); off.width = W; off.height = H;
  const q = off.getContext("2d"), F = S.F, R = S.R, P3 = geom3d(W, H, F), pts = Math.min(420, Math.round(W / 2.5));
  q.fillStyle = "#071016"; q.fillRect(0, 0, W, H);
  const stepF = F > 160 ? 2 : 1;
  for (let f = F - 1; f >= 0; f -= stepF) {
    const { x0, y0, ww, amp } = P3(f);
    q.beginPath(); q.moveTo(x0, y0);
    for (let p = 0; p <= pts; p++) { const j = Math.round(p / pts * (N - 1)); q.lineTo(x0 + ww * p / pts, y0 - R[f * N + j] * amp); }
    q.lineTo(x0 + ww, y0); q.closePath();
    q.fillStyle = "#071016"; q.fill();
    const t = f / Math.max(1, F - 1);
    q.strokeStyle = `rgba(${Math.round(226 - 140 * t)}, ${Math.round(165 - 25 * t)}, ${Math.round(87 + 97 * t)}, ${0.95 - 0.5 * t})`;
    q.lineWidth = Math.max(1, W / 1000); q.stroke();
  }
  img3d = off; img3dKey = key;
}
function frameAt(pos) {
  const F = S.F, fp = pos * (F - 1), f0 = Math.floor(fp), f1 = Math.min(F - 1, f0 + 1), fr = fp - f0, out = new Float32Array(N);
  for (let j = 0; j < N; j++) out[j] = S.R[f0 * N + j] * (1 - fr) + S.R[f1 * N + j] * fr;
  return out;
}
function drawTop(cv, pos) {
  fitCanvas(cv);
  const g = cv.getContext("2d");
  g.imageSmoothingEnabled = S.F < cv.height;
  if (topImg) g.drawImage(topImg, 0, 0, cv.width, cv.height);
  const y = (1 - pos) * cv.height;
  g.strokeStyle = "#fff"; g.lineWidth = Math.max(1.5, cv.width / 380); g.setLineDash([7, 6]);
  g.beginPath(); g.moveTo(0, y); g.lineTo(cv.width, y); g.stroke(); g.setLineDash([]);
}
let lastMapPos = -1;
function loop() {
  requestAnimationFrame(loop);
  if (!S.R) return;
  const pos = S.livePos, F = S.F, mode = document.getElementById("app").dataset.mode;
  if (mode === "play") {
    const c3 = $("#view3d"); fitCanvas(c3); render3d(c3);
    const g3 = c3.getContext("2d"); g3.drawImage(img3d, 0, 0);
    const fr = frameAt(pos), { x0, y0, ww, amp } = geom3d(c3.width, c3.height, F)(pos * (F - 1));
    g3.beginPath();
    for (let p = 0; p <= 500; p++) { const j = Math.round(p / 500 * (N - 1)), x = x0 + ww * p / 500, y = y0 - fr[j] * amp; p ? g3.lineTo(x, y) : g3.moveTo(x, y); }
    g3.strokeStyle = "#fff"; g3.lineWidth = Math.max(1.5, c3.width / 520); g3.stroke();
    drawTop($("#viewTop"), pos);
    const cs = $("#viewScope"); fitCanvas(cs);
    const gs = cs.getContext("2d"), W = cs.width, H = cs.height;
    gs.fillStyle = "#071016"; gs.fillRect(0, 0, W, H);
    gs.strokeStyle = "rgba(141,156,158,0.16)"; gs.lineWidth = 1;
    for (let i = 1; i < 4; i++) { gs.beginPath(); gs.moveTo(0, H * i / 4); gs.lineTo(W, H * i / 4); gs.stroke(); }
    const path = () => { gs.beginPath(); for (let p = 0; p <= 600; p++) { const j = Math.round(p / 600 * (N - 1)), x = W * p / 600, y = H * (0.94 - 0.86 * fr[j]); p ? gs.lineTo(x, y) : gs.moveTo(x, y); } };
    path(); gs.lineTo(W, H); gs.lineTo(0, H); gs.closePath(); gs.fillStyle = "rgba(226,165,87,0.2)"; gs.fill();
    path(); gs.strokeStyle = "#e2a557"; gs.lineWidth = Math.max(1.5, W / 380); gs.stroke();
    $("#frameMeta").textContent = `frame ${Math.round(pos * (F - 1)) + 1} / ${F}${S.voices ? ` · ${S.voices} voice${S.voices > 1 ? "s" : ""}` : ""}`;
  } else {
    drawTop($("#findPreview"), pos);
    if (S.gen && S.gen.lat != null && Math.abs(pos - lastMapPos) > 0.002) {
      lastMapPos = pos;
      const r = S.gen, t = -r.h / 2 + pos * r.h;
      scanLine.setLatLngs([ll(r, -r.w / 2, t), ll(r, r.w / 2, t)]);
    }
  }
}

// ------------------------------------------------------------------ map
const map = L.map("map", { zoomControl: true, worldCopyJump: true, attributionControl: true }).setView([S.region.lat, S.region.lon], 10);
const s2 = L.tileLayer("https://tiles.maps.eox.at/wmts/1.0.0/s2cloudless_3857/default/g/{z}/{y}/{x}.jpg",
  { maxZoom: 18, maxNativeZoom: 15, attribution: "Sentinel-2 cloudless 2016 by EOX (CC BY 4.0)" });
const topo = L.tileLayer("https://{s}.tile.opentopomap.org/{z}/{x}/{y}.png",
  { maxZoom: 17, subdomains: "abc", attribution: "© OpenTopoMap (CC-BY-SA), © OpenStreetMap contributors" });
s2.addTo(map);
L.control.layers({ "Sentinel-2": s2, "Topographic": topo }, null, { position: "topright" }).addTo(map);
const box = L.polygon([], { color: "#e2a557", weight: 2, fillOpacity: 0.07, interactive: false }).addTo(map);
const edge0 = L.polyline([], { color: "#4f8fb8", weight: 4, interactive: false }).addTo(map);
const arrow = L.polyline([], { color: "#e2a557", weight: 3, interactive: false }).addTo(map);
const scanLine = L.polyline([], { color: "#ffffff", weight: 2, dashArray: "6 6", interactive: false }).addTo(map);
function drawRegion() {
  const r = S.region, w = r.w / 2, h = r.h / 2, a = Math.min(r.w, r.h) * 0.12;
  box.setLatLngs([ll(r, -w, -h), ll(r, w, -h), ll(r, w, h), ll(r, -w, h)]);
  edge0.setLatLngs([ll(r, -w, -h), ll(r, w, -h)]);
  arrow.setLatLngs([[ll(r, -w * 0.5, 0), ll(r, w * 0.5, 0)], [ll(r, w * 0.5 - a, a * 0.5), ll(r, w * 0.5, 0), ll(r, w * 0.5 - a, -a * 0.5)]]);
}
let suppressMove = false;
map.on("move", () => {
  if (suppressMove) return;
  const c = map.getCenter(), lon = ((c.lng + 540) % 360) - 180;
  // ignore tiny moves (window resize, re-centring): only a real pan makes it a new place
  const dE = (lon - S.region.lon) * 111320 * Math.cos(S.region.lat * deg), dN = (c.lat - S.region.lat) * 110540;
  if (Math.hypot(dE, dN) < 0.02 * Math.min(S.region.w, S.region.h)) return;
  S.region.lat = c.lat; S.region.lon = lon;
  drawRegion(); markStale("Custom");
});
function markStale(name) { if (name) S.pendingName = name; $("#genBtn").classList.add("stale"); $$(".places button").forEach((b) => b.classList.remove("on")); }
function centreOn(r) {
  suppressMove = true;
  drawRegion();
  map.invalidateSize();
  map.fitBounds(box.getBounds(), { padding: [40, 40], animate: false });
  map.setView([r.lat, r.lon], map.getZoom(), { animate: false });
  suppressMove = false;
}

// ------------------------------------------------------------------ FIND controls
function syncRegionInputs() {
  $("#rw").value = logInv(S.region.w, 200, 120000); $("#rh").value = logInv(S.region.h, 200, 120000); $("#ang").value = S.region.angle;
  regionReadouts();
}
function regionReadouts() {
  $("#rwO").textContent = fmtKm(S.region.w); $("#rhO").textContent = fmtKm(S.region.h); $("#angO").textContent = `${Math.round(S.region.angle)}°`;
  $("#gammaO").textContent = S.gamma.toFixed(2); $("#stepO").textContent = S.step.toFixed(2); $("#smoothO").textContent = S.smooth;
}
function segSet(el, v) { [...el.children].forEach((b) => { const on = b.dataset.v === String(v); b.classList.toggle("on", on); b.setAttribute("aria-checked", on); }); }
function goPlace(p, i) {
  S.region = { lat: p.lat, lon: p.lon, w: p.w, h: p.h, angle: p.angle };
  syncRegionInputs(); centreOn(S.region);
  S.pendingName = p.name;
  $$(".places button").forEach((b, k) => b.classList.toggle("on", k === i));
  generate();
}
PLACES.forEach((p, i) => {
  const li = document.createElement("li"), b = document.createElement("button");
  b.innerHTML = `<b></b><small></small>`; b.querySelector("b").textContent = p.name; b.querySelector("small").textContent = p.note;
  b.addEventListener("click", () => goPlace(p, i));
  li.appendChild(b); $("#places").appendChild(li);
});
["rw", "rh", "ang"].forEach((id) => $("#" + id).addEventListener("input", () => {
  S.region.w = logMap(+$("#rw").value, 200, 120000); S.region.h = logMap(+$("#rh").value, 200, 120000); S.region.angle = +$("#ang").value;
  regionReadouts(); drawRegion(); markStale();
}));
$("#framesSeg").addEventListener("click", (e) => { const b = e.target.closest("button"); if (!b) return; S.frames = +b.dataset.v; segSet($("#framesSeg"), S.frames); generate(); });
$("#seaSeg").addEventListener("click", (e) => { const b = e.target.closest("button"); if (!b) return; S.sea = b.dataset.v; segSet($("#seaSeg"), S.sea); reshape(); });
[["gamma", "gamma"], ["step", "step"], ["smooth", "smooth"]].forEach(([id, key]) => $("#" + id).addEventListener("input", () => {
  S[key] = +$("#" + id).value; regionReadouts(); reshape();
}));
$("#genBtn").addEventListener("click", generate);
$("#goPlay").addEventListener("click", () => setMode("play"));
$("#searchForm").addEventListener("submit", async (e) => {
  e.preventDefault();
  const q = $("#search").value.trim(); if (!q) return;
  setStatus(`Searching “${q}”…`);
  try {
    const r = await fetch(`https://nominatim.openstreetmap.org/search?format=json&limit=1&q=${encodeURIComponent(q)}`);
    const js = await r.json();
    if (!js.length) { setStatus(`No place called “${q}” found.`, true); return; }
    const hit = js[0], [s, n, w, ea] = hit.boundingbox.map(Number), lat = +hit.lat, lon = +hit.lon;
    const width = clamp((ea - w) * 111320 * Math.cos(lat * deg) * 1.15, 1500, 100000), height = clamp((n - s) * 110540 * 1.15, 1500, 100000);
    S.region = { lat, lon, w: width, h: height, angle: 0 };
    syncRegionInputs(); centreOn(S.region);
    S.pendingName = hit.display_name.split(",")[0];
    generate();
  } catch (err) { setStatus("Search failed: " + err.message, true); }
});

// ------------------------------------------------------------------ PLAY: knobs + segmented params
const knobs = {};
const ARC = (v) => {
  const a0 = -225 * deg, a1 = a0 + Math.max(0.0001, v) * 270 * deg, r = 22, c = 26;
  const p = (a) => [c + r * Math.cos(a), c + r * Math.sin(a)];
  const [x0, y0] = p(a0), [x1, y1] = p(a1);
  return `M${x0.toFixed(2)} ${y0.toFixed(2)} A${r} ${r} 0 ${a1 - a0 > Math.PI ? 1 : 0} 1 ${x1.toFixed(2)} ${y1.toFixed(2)}`;
};
const LABELS = { position: "Position", lfoRate: "Rate", lfoDepth: "Depth", flyTime: "Fly time",
  unison: "Unison", detune: "Detune", width: "Width", sub: "Sub", drift: "Drift", glide: "Glide",
  attack: "Attack", decay: "Decay", sustain: "Sustain", release: "Release",
  cutoff: "Cutoff", res: "Reso", drive: "Drive", envAmount: "Env amt", keyTrack: "Key trk",
  fAttack: "Attack", fDecay: "Decay", fSustain: "Sustain", fRelease: "Release",
  chorus: "Chorus", delayMix: "Delay", delayTime: "Time", delayFeedback: "Feedbk", space: "Reverb", size: "Size", volume: "Volume" };
// Sends a knob value and shows the engine's formatted readout ("1.20 s", "2.4 kHz").
function setParamShown(id, v) {
  return Native.call("setParam", id, v).then((text) => {
    if (typeof text === "string" && text && knobs[id]) knobs[id].el.querySelector(".k-val").textContent = text;
    if (S.params[id]) { S.params[id].value = v; if (text) S.params[id].text = text; }
  });
}
function makeKnob(id) {
  const el = document.createElement("div");
  el.className = "knob"; el.tabIndex = 0; el.setAttribute("role", "slider"); el.setAttribute("aria-label", LABELS[id] || id);
  el.innerHTML = `<svg viewBox="0 0 52 52"><path class="track" d="${ARC(1)}"/><path class="arc"/><circle class="cap" cx="26" cy="26" r="14"/><line class="ptr" x1="26" y1="26" x2="26" y2="15"/></svg><div class="k-label"></div><div class="k-val"></div>`;
  el.querySelector(".k-label").textContent = LABELS[id] || id;
  const k = { el, id, value: 0 };
  knobs[id] = k;
  let startY = 0, startV = 0, dragging = false, pending = null, raf = 0;
  const send = (v) => { pending = v; if (!raf) raf = requestAnimationFrame(() => { raf = 0; if (pending != null) setParamShown(id, pending); pending = null; }); };
  el.addEventListener("pointerdown", (e) => {
    el.setPointerCapture(e.pointerId); dragging = true; startY = e.clientY; startV = k.value;
    Native.call("beginEdit", id); e.preventDefault();
  });
  el.addEventListener("pointermove", (e) => {
    if (!dragging) return;
    const v = clamp(startV + (startY - e.clientY) / (e.shiftKey ? 900 : 180), 0, 1);
    setKnob(id, v); send(v);
  });
  const end = () => { if (!dragging) return; dragging = false; Native.call("endEdit", id); };
  el.addEventListener("pointerup", end); el.addEventListener("pointercancel", end);
  el.addEventListener("dblclick", () => { const d = S.params[id]?.default ?? 0.5; Native.call("beginEdit", id); setKnob(id, d); setParamShown(id, d); Native.call("endEdit", id); });
  el.addEventListener("wheel", (e) => { e.preventDefault(); const v = clamp(k.value - Math.sign(e.deltaY) * 0.02, 0, 1); Native.call("beginEdit", id); setKnob(id, v); setParamShown(id, v); Native.call("endEdit", id); }, { passive: false });
  el.addEventListener("keydown", (e) => {
    const d = e.key === "ArrowUp" || e.key === "ArrowRight" ? 0.01 : e.key === "ArrowDown" || e.key === "ArrowLeft" ? -0.01 : 0;
    if (!d) return; e.preventDefault(); e.stopPropagation();
    const v = clamp(k.value + d * (e.shiftKey ? 5 : 1), 0, 1); Native.call("beginEdit", id); setKnob(id, v); setParamShown(id, v); Native.call("endEdit", id);
  });
  return el;
}
function setKnob(id, v, text) {
  const k = knobs[id]; if (!k) return;
  k.value = v;
  k.el.querySelector(".arc").setAttribute("d", ARC(v));
  const a = (-225 + v * 270) * deg;
  const ptr = k.el.querySelector(".ptr"); ptr.setAttribute("x2", (26 + 11 * Math.cos(a)).toFixed(2)); ptr.setAttribute("y2", (26 + 11 * Math.sin(a)).toFixed(2));
  if (text != null) k.el.querySelector(".k-val").textContent = text;
  k.el.setAttribute("aria-valuenow", v.toFixed(3));
}
$$("[data-knobs]").forEach((wrap) => wrap.dataset.knobs.split(" ").forEach((id) => wrap.appendChild(makeKnob(id))));

function buildSegParams() {
  $$(".seg-param").forEach((el) => {
    const id = el.dataset.param, p = S.params[id];
    if (!p || !p.choices || el.childElementCount) return;
    const n = p.choices.length;
    p.choices.forEach((c, i) => {
      const b = document.createElement("button"); b.textContent = c; b.dataset.v = String(i);
      b.addEventListener("click", () => { const v = n > 1 ? i / (n - 1) : 0; Native.call("beginEdit", id); Native.call("setParam", id, v); Native.call("endEdit", id); applyParam({ id, value: v }); });
      el.appendChild(b);
    });
  });
}
function applyParam(p) {
  const prev = S.params[p.id] || {};
  const merged = Object.assign(prev, p); S.params[p.id] = merged;
  if (knobs[p.id]) setKnob(p.id, merged.value, merged.text);
  const seg = document.querySelector(`.seg-param[data-param="${p.id}"]`);
  if (seg && merged.choices) segSet(seg, Math.round(merged.value * (merged.choices.length - 1)));
}

// ------------------------------------------------------------------ keyboard
const KEYMAP = "awsedftgyhujkolp;'";
function noteOn(n, vel = 0.85) { S.held.add(n); keyVisual(n, true); Native.call("note", n, vel); }
function noteOff(n) { S.held.delete(n); keyVisual(n, false); Native.call("note", n, 0); }
function toggleOrOn(n) { if (S.hold && S.held.has(n)) noteOff(n); else noteOn(n); }
function keyVisual(n, on) { const el = document.querySelector(`#kbd [data-note="${n}"]`); if (el) el.classList.toggle("on", on); }
function buildKeyboard() {
  const kb = $("#kbd"); kb.innerHTML = "";
  const base = 12 * (S.octave + 1), whites = [], blacks = [];
  for (let n = 0; n <= 24; n++) ([1, 3, 6, 8, 10].includes(n % 12) ? blacks : whites).push(n);
  const ww = 100 / whites.length;
  whites.forEach((n) => {
    const d = document.createElement("div"); d.className = "w"; d.dataset.note = base + n;
    d.innerHTML = `<span>${n % 12 === 0 ? "C" + (S.octave + n / 12) : (KEYMAP[n] || "").toUpperCase()}</span>`;
    kb.appendChild(d);
  });
  blacks.forEach((n) => {
    const wi = whites.filter((w) => w < n).length;
    const d = document.createElement("div"); d.className = "b"; d.dataset.note = base + n;
    d.style.left = `calc(${wi * ww}% - ${ww * 0.31}%)`; d.style.width = `${ww * 0.62}%`;
    kb.appendChild(d);
  });
  for (const n of S.held) keyVisual(n, true);
  $("#octMeta").textContent = `C${S.octave}`;
}
const ptrNotes = new Map();
$("#kbd").addEventListener("pointerdown", (e) => {
  const k = e.target.closest("[data-note]"); if (!k) return;
  e.preventDefault(); $("#kbd").setPointerCapture(e.pointerId);
  const n = +k.dataset.note; ptrNotes.set(e.pointerId, n);
  const vel = clamp(0.45 + 0.55 * (e.offsetY / k.clientHeight), 0.3, 1);
  if (S.hold) toggleOrOn(n); else noteOn(n, vel);
});
$("#kbd").addEventListener("pointermove", (e) => {
  if (!ptrNotes.has(e.pointerId) || S.hold) return;
  const el = document.elementFromPoint(e.clientX, e.clientY)?.closest("#kbd [data-note]"); if (!el) return;
  const n = +el.dataset.note, prev = ptrNotes.get(e.pointerId);
  if (n !== prev) { noteOff(prev); ptrNotes.set(e.pointerId, n); noteOn(n); }
});
const ptrUp = (e) => { if (ptrNotes.has(e.pointerId)) { if (!S.hold) noteOff(ptrNotes.get(e.pointerId)); ptrNotes.delete(e.pointerId); } };
$("#kbd").addEventListener("pointerup", ptrUp); $("#kbd").addEventListener("pointercancel", ptrUp);
const keyNotes = new Map();
window.addEventListener("keydown", (e) => {
  if (e.repeat || e.ctrlKey || e.metaKey || e.altKey) return;
  if (document.activeElement && document.activeElement.tagName === "INPUT" && document.activeElement.type !== "range") return;
  if (document.getElementById("app").dataset.mode !== "play") return;
  const k = e.key.toLowerCase();
  if (k === "z") { S.octave = Math.max(0, S.octave - 1); buildKeyboard(); return; }
  if (k === "x") { S.octave = Math.min(7, S.octave + 1); buildKeyboard(); return; }
  const i = KEYMAP.indexOf(k); if (i < 0) return;
  e.preventDefault();
  const n = 12 * (S.octave + 1) + i; keyNotes.set(k, n);
  if (S.hold) toggleOrOn(n); else noteOn(n);
});
window.addEventListener("keyup", (e) => { const k = e.key.toLowerCase(); if (keyNotes.has(k)) { if (!S.hold) noteOff(keyNotes.get(k)); keyNotes.delete(k); } });
window.addEventListener("blur", () => { if (!S.hold) for (const n of [...S.held]) noteOff(n); });
$("#octDown").addEventListener("click", () => { S.octave = Math.max(0, S.octave - 1); buildKeyboard(); });
$("#octUp").addEventListener("click", () => { S.octave = Math.min(7, S.octave + 1); buildKeyboard(); });
$("#holdBtn").addEventListener("click", () => {
  S.hold = !S.hold; $("#holdBtn").setAttribute("aria-pressed", S.hold);
  if (!S.hold) for (const n of [...S.held]) noteOff(n);
});

// ------------------------------------------------------------------ files
$("#expSeg").addEventListener("click", (e) => { const b = e.target.closest("button"); if (!b) return; S.exp = b.dataset.v; segSet($("#expSeg"), S.exp); });
$("#bitwigBtn").addEventListener("click", async () => {
  const path = await Native.call("sendToBitwig", S.exp);
  setStatus(path ? `Sent to Bitwig: ${path.split(/[\\/]/).slice(-2).join("/")} (rescan the Library)` : "Could not write to the Bitwig library.", !path);
});
$("#saveBtn").addEventListener("click", async () => { const p = await Native.call("exportWT", S.exp); if (p) setStatus("Saved " + p.split(/[\\/]/).pop()); });
$("#openBtn").addEventListener("click", async () => {
  const res = await Native.call("openWT");
  if (!res) return;
  if (typeof res === "string") { setStatus("Could not open: " + res, true); return; }
  loadFromEngine(res); setStatus("Loaded " + S.name);
});

// ------------------------------------------------------------------ modes
function setMode(m) {
  document.getElementById("app").dataset.mode = m;
  $("#tabFind").setAttribute("aria-selected", m === "find"); $("#tabPlay").setAttribute("aria-selected", m === "play");
  if (m === "find") setTimeout(() => map.invalidateSize(), 0);
  img3dKey = "";
}
$$(".tabs button").forEach((b) => b.addEventListener("click", () => setMode(b.dataset.mode)));

// ------------------------------------------------------------------ engine sync
function loadFromEngine(st) {
  const rel = st.relief, reg = st.region || {};
  if (rel && rel.data) { S.R = b64ToRelief(rel.data); S.F = rel.frames; S.name = rel.name || reg.name || "Terrain"; }
  S.raw = null;
  if (reg.lat != null && reg.width) {
    S.gen = { lat: +reg.lat, lon: +reg.lon, w: +reg.width, h: +reg.height, angle: +reg.angle || 0 };
    S.sea = reg.sea || "flat"; S.gamma = +reg.gamma || 0.5; S.step = reg.step != null ? +reg.step : 0.2; S.smooth = reg.smooth != null ? +reg.smooth : 1;
    S.mpp = +reg.mpp || 0; S.hmin = +reg.hmin || 0; S.hmax = +reg.hmax || 0;
    S.frames = S.F;
    S.region = { ...S.gen };
    syncRegionInputs(); centreOn(S.region);
    $("#gamma").value = S.gamma; $("#step").value = S.step; $("#smooth").value = S.smooth;
    segSet($("#framesSeg"), S.frames); segSet($("#seaSeg"), S.sea); regionReadouts();
    S.seaTop = S.sea === "off" ? -1 : S.sea === "flat" ? (S.step > 0 ? S.step * 0.5 : 1e-6) : 0.3;
  } else {
    S.gen = null; S.seaTop = -1; scanLine.setLatLngs([]);
  }
  $("#genBtn").classList.remove("stale");
  afterTable();
}
Native.on("params", (list) => list.forEach(applyParam));

// ------------------------------------------------------------------ sounds (factory presets)
function buildPresets(names, current) {
  const sel = $("#presetSel"); sel.innerHTML = "";
  (names || []).forEach((n, i) => { const o = document.createElement("option"); o.value = String(i); o.textContent = n; sel.appendChild(o); });
  const i = (names || []).indexOf(current); sel.value = String(i >= 0 ? i : 0);
}
async function loadPreset(i) {
  const sel = $("#presetSel"), n = sel.options.length; if (!n) return;
  i = ((i % n) + n) % n; sel.value = String(i);
  const name = await Native.call("loadPreset", i);
  setStatus(`Sound: ${name}`);
}
$("#presetSel").addEventListener("change", (e) => { loadPreset(+e.target.value); e.target.blur(); });  // blur: letter keys play notes again
$("#presetPrev").addEventListener("click", () => loadPreset(+$("#presetSel").value - 1));
$("#presetNext").addEventListener("click", () => loadPreset(+$("#presetSel").value + 1));
Native.on("live", (d) => { S.livePos = d.pos; S.voices = d.voices; });
Native.on("relief", (d) => loadFromEngine(d));

async function boot() {
  buildKeyboard();
  segSet($("#expSeg"), S.exp); segSet($("#fmtSeg"), S.fmt); segSet($("#framesSeg"), S.frames); segSet($("#seaSeg"), S.sea);
  syncRegionInputs(); centreOn(S.region);
  if (Native.ok) {
    const st = await Native.call("getState");
    (st.params || []).forEach((p) => { S.params[p.id] = p; });
    buildSegParams();
    (st.params || []).forEach(applyParam);
    buildPresets(st.presets, st.preset);
    loadFromEngine(st);
    setStatus(st.standalone ? "Standalone · audio and MIDI settings under Options" : "");
  } else {
    setStatus("Preview mode: no audio engine (open inside the app or plugin)", true);
    goPlace(PLACES[0], 0);
  }
  requestAnimationFrame(loop);
}
window.addEventListener("resize", () => { img3dKey = ""; });
boot();
