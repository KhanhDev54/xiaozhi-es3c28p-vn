// Xiaozhi 2.8" web flasher — esptool-js over Web Serial
import { ESPLoader, Transport } from "https://unpkg.com/esptool-js@0.5.7/bundle.js";

const $ = (id) => document.getElementById(id);
const els = {
  status: $("status"), statusText: $("statusText"),
  connect: $("btnConnect"), disconnect: $("btnDisconnect"),
  flash: $("btnFlash"), erase: $("btnErase"), clear: $("btnClear"),
  fwSelect: $("fwSelect"), fwInfo: $("fwInfo"),
  drop: $("drop"), dropText: $("dropText"), fileInput: $("fileInput"),
  address: $("address"), baud: $("baud"), eraseAll: $("eraseAll"),
  bar: $("bar"), pct: $("pct"), speed: $("speed"), log: $("log"),
  unsupported: $("unsupported"),
};

let transport = null;
let loader = null;
let chipName = "";
let busy = false;
let firmwares = [];
// Selected image: { name, bytes: Uint8Array, address }
let image = null;

// ---------- logging ----------
function ts() {
  return new Date().toLocaleTimeString("vi-VN", { hour12: false });
}
function log(msg) {
  els.log.textContent += `[${ts()}] ${msg}\n`;
  els.log.scrollTop = els.log.scrollHeight;
}
const terminal = {
  clean() {},
  writeLine(data) { log(data); },
  write(data) {
    els.log.textContent += data;
    els.log.scrollTop = els.log.scrollHeight;
  },
};

// ---------- helpers ----------
function fmtSize(n) {
  if (n >= 1024 * 1024) return (n / 1024 / 1024).toFixed(2) + " MB";
  if (n >= 1024) return (n / 1024).toFixed(1) + " KB";
  return n + " B";
}
function parseAddress(text) {
  const t = text.trim().toLowerCase();
  const v = t.startsWith("0x") ? parseInt(t, 16) : parseInt(t, 10);
  if (Number.isNaN(v) || v < 0) throw new Error(`Địa chỉ không hợp lệ: ${text}`);
  return v;
}
// esptool-js 0.5.x expects a "binary string" (one char per byte)
function toBinaryString(bytes) {
  let out = "";
  const CHUNK = 0x8000;
  for (let i = 0; i < bytes.length; i += CHUNK) {
    out += String.fromCharCode.apply(null, bytes.subarray(i, i + CHUNK));
  }
  return out;
}
function setStatus(kind, text) {
  els.status.className = "pill " + (kind === "on" ? "pill-on" : kind === "busy" ? "pill-busy" : "pill-off");
  els.statusText.textContent = text;
}
function setProgress(ratio, bytesPerSec) {
  const p = Math.max(0, Math.min(1, ratio));
  els.bar.style.width = (p * 100).toFixed(1) + "%";
  els.pct.textContent = Math.floor(p * 100) + "%";
  els.speed.textContent = bytesPerSec ? fmtSize(bytesPerSec) + "/s" : "--";
}
function refreshButtons() {
  const connected = !!loader;
  els.connect.disabled = busy || connected || (typeof mon !== "undefined" && mon.running);
  els.disconnect.disabled = busy || !connected;
  els.flash.disabled = busy || !connected || !image;
  els.erase.disabled = busy || !connected;
  els.fwSelect.disabled = busy;
  els.baud.disabled = connected;
  if (typeof monButtons === "function" && typeof mon !== "undefined") monButtons();
}

// ---------- firmware list ----------
// Both repos publish to https://<user>.github.io/<repo>/ (same origin), so this page can
// list the firmware of every board. Own repo first, then the others.
const SOURCES = [
  { repo: "xiaozhi-es3c28p-vn", board: "ESP32-S3 2.8\" cảm ứng (ES3C28P, 16MB)" },
  { repo: "xiaozhi-s3zero-oled-vn", board: "ESP32-S3 Zero + OLED 0.96\" (4MB)" },
];

function sourceBase(repo) {
  const parts = location.pathname.split("/").filter(Boolean);
  const own = parts.length ? parts[0] : "";
  return own === repo ? "" : `../${repo}/`;
}

async function fetchSource(src) {
  const base = sourceBase(src.repo);
  const res = await fetch(base + "firmware.json", { cache: "no-store" });
  if (!res.ok) throw new Error("HTTP " + res.status);
  const data = await res.json();
  const list = Array.isArray(data.firmwares) ? data.firmwares : [];
  return list.map((fw) => ({ ...fw, file: base + fw.file, board: fw.board || src.board, own: base === "" }));
}

async function loadFirmwareList() {
  const ordered = [...SOURCES].sort((a, b) => (sourceBase(a.repo) === "" ? -1 : 0) - (sourceBase(b.repo) === "" ? -1 : 0));
  const results = await Promise.allSettled(ordered.map(fetchSource));
  firmwares = [];
  results.forEach((r, i) => {
    if (r.status === "fulfilled") firmwares.push(...r.value);
    else log(`Chưa có firmware cho ${ordered[i].board} (${r.reason && r.reason.message}).`);
  });

  els.fwSelect.innerHTML = "";
  els.fwSelect.add(new Option(firmwares.length ? "-- Chọn firmware --" : "Chưa có firmware trong danh sách", ""));
  const groups = new Map();
  firmwares.forEach((fw, i) => {
    if (!groups.has(fw.board)) {
      const g = document.createElement("optgroup");
      g.label = fw.board;
      groups.set(fw.board, g);
      els.fwSelect.appendChild(g);
    }
    groups.get(fw.board).appendChild(new Option(`${fw.name} — v${fw.version}`, String(i)));
  });
  if (firmwares.length) log(`Đã tải ${firmwares.length} firmware từ danh sách.`);
}

async function selectFirmware(index) {
  const fw = firmwares[index];
  if (!fw) { image = null; els.fwInfo.hidden = true; refreshButtons(); return; }
  els.fwInfo.hidden = false;
  els.fwInfo.innerHTML =
    `<b>${fw.name}</b><br>Mạch: ${fw.board}<br>Phiên bản: <b>${fw.version}</b> · Build: ${fw.date || "?"} · Commit: ${(fw.commit || "").slice(0, 7)}` +
    (fw.notes ? `<br>${fw.notes}` : "");
  els.address.value = "0x" + Number(fw.address || 0).toString(16);
  busy = true; refreshButtons();
  try {
    log(`Đang tải ${fw.file}…`);
    const res = await fetch(fw.file, { cache: "no-store" });
    if (!res.ok) throw new Error("HTTP " + res.status);
    const bytes = new Uint8Array(await res.arrayBuffer());
    image = { name: fw.file, bytes, flashMb: fw.flash_mb || 0 };
    els.drop.classList.remove("has-file");
    els.dropText.innerHTML = "Kéo thả file <b>.bin</b> vào đây hoặc <u>chọn file</u>";
    log(`✅ Đã tải ${fw.file} (${fmtSize(bytes.length)}).`);
  } catch (e) {
    image = null;
    log(`❌ Lỗi tải firmware: ${e.message}`);
  } finally {
    busy = false; refreshButtons();
  }
}

async function useLocalFile(file) {
  if (!file) return;
  if (!file.name.toLowerCase().endsWith(".bin")) { log("❌ Chỉ nhận file .bin"); return; }
  const bytes = new Uint8Array(await file.arrayBuffer());
  image = { name: file.name, bytes };
  els.fwSelect.value = "";
  els.fwInfo.hidden = true;
  els.drop.classList.add("has-file");
  els.dropText.innerHTML = `<b>${file.name}</b> · ${fmtSize(bytes.length)}`;
  log(`Đã chọn file ${file.name} (${fmtSize(bytes.length)}).`);
  refreshButtons();
}

// ---------- serial actions ----------
async function connect() {
  busy = true; refreshButtons();
  setStatus("busy", "Đang kết nối…");
  try {
    const port = await navigator.serial.requestPort();
    transport = new Transport(port, true);
    loader = new ESPLoader({
      transport,
      baudrate: parseInt(els.baud.value, 10),
      romBaudrate: 115200,
      terminal,
    });
    chipName = await loader.main();
    setStatus("on", `Đã kết nối · ${chipName}`);
    log(`✅ Đã kết nối: ${chipName}`);
    if (!/ESP32-S3/i.test(chipName)) {
      log("⚠️ Firmware này dành cho ESP32-S3. Kiểm tra lại mạch trước khi nạp.");
    }
  } catch (e) {
    log(`❌ Kết nối thất bại: ${e.message || e}`);
    log("Gợi ý: giữ BOOT, bấm RESET, thả BOOT rồi bấm Kết nối lại.");
    await safeDisconnect();
    setStatus("off", "Chưa kết nối");
  } finally {
    busy = false; refreshButtons();
  }
}

async function safeDisconnect() {
  try { if (transport) await transport.disconnect(); } catch (_) {}
  transport = null; loader = null; chipName = "";
}

async function disconnect() {
  busy = true; refreshButtons();
  await safeDisconnect();
  setStatus("off", "Chưa kết nối");
  log("Đã ngắt kết nối.");
  busy = false; refreshButtons();
}

async function flash() {
  if (!loader || !image) return;
  let address;
  try { address = parseAddress(els.address.value); } catch (e) { log("❌ " + e.message); return; }

  busy = true; refreshButtons();
  setStatus("busy", "Đang nạp…");
  setProgress(0);
  const started = performance.now();
  try {
    if (image.flashMb) {
      try {
        const kb = await loader.getFlashSize();
        if (kb && kb < image.flashMb * 1024) {
          const ok = confirm(`Mạch này có ${kb / 1024}MB flash nhưng firmware cần ${image.flashMb}MB.\n` +
                             "Nạp nhầm firmware sẽ không chạy. Vẫn tiếp tục?");
          if (!ok) { log("Đã huỷ: firmware không hợp với dung lượng flash của mạch."); return; }
        } else if (kb) {
          log(`Flash của mạch: ${kb / 1024}MB ✓`);
        }
      } catch (_) { /* ignore: older chips may not report size */ }
    }
    log(`Bắt đầu nạp ${image.name} (${fmtSize(image.bytes.length)}) tại 0x${address.toString(16)}…`);
    await loader.writeFlash({
      fileArray: [{ data: toBinaryString(image.bytes), address }],
      flashSize: "keep",
      flashMode: "keep",
      flashFreq: "keep",
      eraseAll: els.eraseAll.checked,
      compress: true,
      reportProgress: (_i, written, total) => {
        const secs = (performance.now() - started) / 1000;
        setProgress(written / total, secs > 0 ? written / secs : 0);
      },
    });
    setProgress(1);
    const secs = ((performance.now() - started) / 1000).toFixed(1);
    log(`✅ Nạp xong sau ${secs} giây. Đang khởi động lại mạch…`);
    await loader.after("hard_reset");
    log("🎉 Hoàn tất! Nếu màn hình chưa sáng, bấm nút RESET trên mạch.");
    await safeDisconnect();
    setStatus("off", "Chưa kết nối");
  } catch (e) {
    log(`❌ Nạp thất bại: ${e.message || e}`);
    setStatus(loader ? "on" : "off", loader ? `Đã kết nối · ${chipName}` : "Chưa kết nối");
  } finally {
    busy = false; refreshButtons();
  }
}

async function erase() {
  if (!loader) return;
  if (!confirm("Xoá toàn bộ flash? Mọi dữ liệu (Wi-Fi, cài đặt) trên mạch sẽ mất.")) return;
  busy = true; refreshButtons();
  setStatus("busy", "Đang xoá flash…");
  try {
    log("Đang xoá flash (có thể mất 20–60 giây)…");
    const t0 = performance.now();
    await loader.eraseFlash();
    log(`✅ Đã xoá flash sau ${((performance.now() - t0) / 1000).toFixed(1)} giây.`);
  } catch (e) {
    log(`❌ Xoá flash thất bại: ${e.message || e}`);
  } finally {
    setStatus(loader ? "on" : "off", loader ? `Đã kết nối · ${chipName}` : "Chưa kết nối");
    busy = false; refreshButtons();
  }
}


// ---------- serial monitor (read the device log) ----------
const mon = { port: null, reader: null, running: false, text: "" };
const monEls = {
  open: $("btnMonOpen"), reset: $("btnMonReset"), close: $("btnMonClose"),
  copy: $("btnMonCopy"), save: $("btnMonSave"), log: $("monLog"),
};
const ANSI = /\x1b\[[0-9;]*m/g;
function monWrite(t) {
  mon.text += t;
  if (mon.text.length > 400000) mon.text = mon.text.slice(-300000);
  monEls.log.textContent = mon.text;
  monEls.log.scrollTop = monEls.log.scrollHeight;
}
function monButtons() {
  monEls.open.disabled = mon.running || !!loader;
  monEls.reset.disabled = !mon.running;
  monEls.close.disabled = !mon.running;
}
async function monOpen() {
  if (loader) { log("Hãy bấm Ngắt ở trên trước khi mở log."); return; }
  try {
    mon.port = await navigator.serial.requestPort();
    await mon.port.open({ baudRate: 115200 });
  } catch (e) {
    monWrite(`[Không mở được cổng: ${e.message || e}]\n`);
    mon.port = null; return;
  }
  mon.running = true; refreshButtons();
  monWrite(`[Đã mở log lúc ${ts()} — thao tác trên mạch để xem]\n`);
  const dec = new TextDecoder();
  while (mon.running && mon.port && mon.port.readable) {
    mon.reader = mon.port.readable.getReader();
    try {
      for (;;) {
        const { value, done } = await mon.reader.read();
        if (done) break;
        if (value) monWrite(dec.decode(value, { stream: true }).replace(ANSI, ""));
      }
    } catch (e) {
      if (mon.running) monWrite(`\n[Lỗi đọc: ${e.message || e}]\n`);
    } finally {
      try { mon.reader.releaseLock(); } catch (_) {}
      mon.reader = null;
    }
  }
}
async function monClose() {
  mon.running = false;
  try { if (mon.reader) await mon.reader.cancel(); } catch (_) {}
  try { if (mon.port) await mon.port.close(); } catch (_) {}
  mon.port = null; refreshButtons();
  monWrite(`\n[Đã dừng lúc ${ts()}]\n`);
}
async function monReset() {
  if (!mon.port) return;
  try {
    await mon.port.setSignals({ dataTerminalReady: false, requestToSend: true });
    await new Promise((r) => setTimeout(r, 120));
    await mon.port.setSignals({ dataTerminalReady: false, requestToSend: false });
    monWrite(`\n[Đã gửi lệnh khởi động lại ${ts()}]\n`);
  } catch (e) {
    monWrite(`\n[Không khởi động lại được, hãy bấm nút RESET trên mạch]\n`);
  }
}
function monInit() {
  monEls.open.addEventListener("click", monOpen);
  monEls.close.addEventListener("click", monClose);
  monEls.reset.addEventListener("click", monReset);
  monEls.copy.addEventListener("click", async () => {
    try { await navigator.clipboard.writeText(mon.text); monWrite("\n[Đã sao chép log]\n"); }
    catch (_) { monWrite("\n[Không sao chép được — dùng nút Tải file .txt]\n"); }
  });
  monEls.save.addEventListener("click", () => {
    const a = document.createElement("a");
    a.href = URL.createObjectURL(new Blob([mon.text], { type: "text/plain" }));
    a.download = `xiaozhi-log-${Date.now()}.txt`;
    a.click();
    setTimeout(() => URL.revokeObjectURL(a.href), 2000);
  });
  if (!("serial" in navigator)) monEls.open.disabled = true;
  monButtons();
}


// ---------- unbind helper (MAC + Device ID -> support email) ----------
const UNBIND_TO = "xiaozhi.ai@tenclass.com";
function normMac(v) {
  const hex = (v || "").replace(/[^0-9a-fA-F]/g, "").toUpperCase();
  if (hex.length !== 12) return null;
  return hex.match(/../g).join(":");
}
function unbindMail() {
  const err = $("unbindErr");
  err.textContent = "";
  const mac = normMac($("macInput").value);
  const id = ($("devIdInput").value || "").trim();
  if (!mac) { err.textContent = "Địa chỉ MAC chưa đúng (dạng AA:BB:CC:DD:EE:FF)."; return null; }
  if (id && !/^\d+$/.test(id)) { err.textContent = "Device ID chỉ gồm chữ số (có thể để trống)."; return null; }
  $("macInput").value = mac;
  const subject = `【解绑设备】${id ? "设备ID " + id + "，" : ""}MAC地址 ${mac}`;
  const body =
    "尊敬的 Xiaozhi 支持团队：\n" +
    "我无法登录之前绑定该设备的账户，请协助将以下设备从旧账户中解除绑定。\n" +
    `+ MAC Address: ${mac}\n` +
    (id ? `+ Device ID: ${id}\n` : "") +
    "处理完成后，请通过此邮箱回复确认。谢谢！\n\n" +
    "Hello Xiaozhi support team, I can no longer log in to the account this device is bound to. " +
    "Please unbind the device above from the old account. Thank you!";
  return { subject, body };
}
async function readMac() {
  const err = $("unbindErr");
  err.textContent = "";
  try {
    if (!loader) await connect();
    if (!loader) return;
    busy = true; refreshButtons();
    const mac = await loader.chip.readMac(loader);
    $("macInput").value = (mac || "").toUpperCase();
    log(`MAC của mạch: ${$("macInput").value}`);
    try { await loader.after("hard_reset"); } catch (_) {}
    await safeDisconnect();
    setStatus("off", "Chưa kết nối");
  } catch (e) {
    err.textContent = "Không đọc được MAC: " + (e.message || e);
  } finally {
    busy = false; refreshButtons();
  }
}
function unbindInit() {
  $("btnReadMac").addEventListener("click", readMac);
  $("btnGmail").addEventListener("click", () => {
    const m = unbindMail(); if (!m) return;
    const url = "https://mail.google.com/mail/?view=cm&fs=1&to=" + encodeURIComponent(UNBIND_TO) +
      "&su=" + encodeURIComponent(m.subject) + "&body=" + encodeURIComponent(m.body);
    window.open(url, "_blank", "noopener");
  });
  $("btnMailto").addEventListener("click", () => {
    const m = unbindMail(); if (!m) return;
    location.href = "mailto:" + UNBIND_TO + "?subject=" + encodeURIComponent(m.subject) +
      "&body=" + encodeURIComponent(m.body);
  });
  if (!("serial" in navigator)) $("btnReadMac").disabled = true;
}

// ---------- wiring ----------
function init() {
  log("Sẵn sàng.");
  if (!("serial" in navigator)) {
    els.unsupported.hidden = false;
    els.connect.disabled = true;
    log("❌ Trình duyệt không hỗ trợ Web Serial — dùng Chrome/Edge trên máy tính.");
  } else {
    log("✅ Trình duyệt tương thích.");
    navigator.serial.addEventListener("disconnect", async () => {
      if (loader && !busy) {
        log("⚠️ Mạch đã bị rút ra.");
        await safeDisconnect();
        setStatus("off", "Chưa kết nối");
        refreshButtons();
      }
    });
  }

  monInit();
  unbindInit();
  els.connect.addEventListener("click", connect);
  els.disconnect.addEventListener("click", disconnect);
  els.flash.addEventListener("click", flash);
  els.erase.addEventListener("click", erase);
  els.clear.addEventListener("click", () => { els.log.textContent = ""; });
  els.fwSelect.addEventListener("change", (e) => {
    const v = e.target.value;
    selectFirmware(v === "" ? -1 : parseInt(v, 10));
  });
  els.fileInput.addEventListener("change", (e) => useLocalFile(e.target.files[0]));

  ["dragenter", "dragover"].forEach((t) => els.drop.addEventListener(t, (e) => {
    e.preventDefault(); els.drop.classList.add("over");
  }));
  ["dragleave", "drop"].forEach((t) => els.drop.addEventListener(t, (e) => {
    e.preventDefault(); els.drop.classList.remove("over");
  }));
  els.drop.addEventListener("drop", (e) => useLocalFile(e.dataTransfer.files[0]));

  refreshButtons();
  loadFirmwareList().then(() => {
    // Auto-select the newest firmware so users can flash in two clicks
    if (firmwares.length) {
      els.fwSelect.value = "0";
      selectFirmware(0);
    }
  });
}

init();
