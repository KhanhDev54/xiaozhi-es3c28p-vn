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
  els.connect.disabled = busy || connected;
  els.disconnect.disabled = busy || !connected;
  els.flash.disabled = busy || !connected || !image;
  els.erase.disabled = busy || !connected;
  els.fwSelect.disabled = busy;
  els.baud.disabled = connected;
}

// ---------- firmware list ----------
async function loadFirmwareList() {
  try {
    const res = await fetch("firmware.json", { cache: "no-store" });
    if (!res.ok) throw new Error("HTTP " + res.status);
    const data = await res.json();
    firmwares = Array.isArray(data.firmwares) ? data.firmwares : [];
  } catch (e) {
    firmwares = [];
    log(`Không tải được danh sách firmware (${e.message}). Bạn vẫn có thể kéo thả file .bin.`);
  }
  els.fwSelect.innerHTML = "";
  const placeholder = new Option(firmwares.length ? "-- Chọn firmware --" : "Chưa có firmware trong danh sách", "");
  els.fwSelect.add(placeholder);
  firmwares.forEach((fw, i) => els.fwSelect.add(new Option(`${fw.name} — v${fw.version}`, String(i))));
  if (firmwares.length) log(`Đã tải ${firmwares.length} firmware từ danh sách.`);
}

async function selectFirmware(index) {
  const fw = firmwares[index];
  if (!fw) { image = null; els.fwInfo.hidden = true; refreshButtons(); return; }
  els.fwInfo.hidden = false;
  els.fwInfo.innerHTML =
    `<b>${fw.name}</b><br>Phiên bản: <b>${fw.version}</b> · Build: ${fw.date || "?"} · Commit: ${(fw.commit || "").slice(0, 7)}` +
    (fw.notes ? `<br>${fw.notes}` : "");
  els.address.value = "0x" + Number(fw.address || 0).toString(16);
  busy = true; refreshButtons();
  try {
    log(`Đang tải ${fw.file}…`);
    const res = await fetch(fw.file, { cache: "no-store" });
    if (!res.ok) throw new Error("HTTP " + res.status);
    const bytes = new Uint8Array(await res.arrayBuffer());
    image = { name: fw.file, bytes };
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
