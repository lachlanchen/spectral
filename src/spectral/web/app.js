const $ = (id) => document.getElementById(id);
const canvas = $("spectrumCanvas");
const wrap = $("canvasWrap");
const ctx = canvas.getContext("2d", { alpha: false });
let instrument = null;
let spectrum = null;
let toastTimer = null;

const colors = [
  [0.00, "#744ac2"], [0.08, "#754ed0"], [0.20, "#356ddd"],
  [0.30, "#20b5d0"], [0.36, "#2dbb73"], [0.48, "#ebcb37"],
  [0.55, "#ef8732"], [0.65, "#df493f"], [0.86, "#8b2738"], [1.00, "#55202f"],
];

function toast(message, bad = false) {
  const node = $("toast");
  node.textContent = message;
  node.style.background = bad ? "#d94f3b" : "#102a35";
  node.classList.add("show");
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => node.classList.remove("show"), 2200);
}

async function jsonRequest(path, payload) {
  const options = payload === undefined ? {} : {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(payload),
  };
  const response = await fetch(path, { ...options, cache: "no-store" });
  const data = await response.json();
  if (!response.ok || !data.ok) throw new Error(data.error || `HTTP ${response.status}`);
  return data;
}

async function command(path, payload, message) {
  try {
    const result = await jsonRequest(path, payload);
    if (result.status) updateStatus(result.status);
    toast(message);
  } catch (error) {
    toast(error.message, true);
  }
}

function setUnlessFocused(node, value) {
  if (document.activeElement !== node) node.value = value;
}

function updateStatus(state) {
  instrument = state;
  const live = state.state === "live";
  $("livePill").className = `live-pill ${live ? "" : "waiting"}`;
  $("livePill").innerHTML = `<i></i>${live ? "Live" : "Waiting"}`;
  $("portLabel").textContent = state.frame?.source?.replace("hardware:", "") || "C12880MA / scanning";
  $("autoExposure").checked = Boolean(state.exposure?.auto);
  $("autoY").checked = Boolean(state.y_scale?.auto);
  const unit = $("exposureUnit").value;
  setUnlessFocused($("exposureValue"), unit === "us" ? state.exposure?.us?.toFixed(3) : state.exposure?.ms?.toFixed(3));
  setUnlessFocused($("yMin"), state.y_scale?.minimum?.toFixed(1));
  setUnlessFocused($("yMax"), state.y_scale?.maximum?.toFixed(1));
  $("smoothing").value = (state.smoothing || "fast").toLowerCase().startsWith("raw") ? "raw" : (state.smoothing || "").toLowerCase().startsWith("smooth") ? "smooth" : "fast";
  $("averaging").value = state.acquisition?.averaging ?? 1;
  $("trigger").value = state.acquisition?.trigger ?? "internal";
  $("outputMask").value = state.acquisition?.output_mask ?? 1;
  $("recordButton").classList.toggle("recording", Boolean(state.recording));
  $("recordButton").textContent = state.recording ? "Stop recording" : "Record raw CSV";
  const summary = state.spectrum;
  $("peakMetric").textContent = summary ? `${summary.peak_nm.toFixed(1)} nm` : "-- nm";
  $("signalMetric").textContent = summary ? Math.round(summary.peak_counts).toLocaleString() : "--";
  $("integralMetric").textContent = summary ? `${(summary.integrated_counts_nm / 1e6).toFixed(2)} M` : "--";
  $("fpsMetric").textContent = `${(state.acquisition?.fps || 0).toFixed(1)} fps`;
  $("frameLabel").textContent = state.frame ? `Frame ${state.frame.sequence} / ${state.frame.source}` : "No validated frame";
  $("saturationLabel").textContent = `Saturation ${summary?.saturated_pixels ?? "--"}/288`;
  $("statusLine").textContent = `API connected / raw acquisition ${Math.round(state.acquisition?.fps || 0)} fps / web render 20 fps`;
}

function wavelengthGradient(context, left, right) {
  const gradient = context.createLinearGradient(left, 0, right, 0);
  colors.forEach(([position, color]) => gradient.addColorStop(position, color));
  return gradient;
}

function resizeCanvas() {
  const rect = wrap.getBoundingClientRect();
  const dpr = Math.min(window.devicePixelRatio || 1, 2);
  const width = Math.max(320, Math.round(rect.width * dpr));
  const height = Math.max(260, Math.round(rect.height * dpr));
  if (canvas.width !== width || canvas.height !== height) {
    canvas.width = width;
    canvas.height = height;
  }
  drawSpectrum();
}

function drawSpectrum() {
  const width = canvas.width;
  const height = canvas.height;
  const dpr = Math.min(window.devicePixelRatio || 1, 2);
  const margin = { left: 68 * dpr, right: 22 * dpr, top: 24 * dpr, bottom: 50 * dpr };
  const plotW = width - margin.left - margin.right;
  const plotH = height - margin.top - margin.bottom;
  ctx.fillStyle = "#fffdf8";
  ctx.fillRect(0, 0, width, height);
  const xMin = 340, xMax = 850;
  let yMin = instrument?.y_scale?.minimum ?? 0;
  let yMax = instrument?.y_scale?.maximum ?? 65535;
  if (!(yMax > yMin)) yMax = yMin + 1;
  const xFor = (nm) => margin.left + ((nm - xMin) / (xMax - xMin)) * plotW;
  const yFor = (value) => margin.top + (1 - (value - yMin) / (yMax - yMin)) * plotH;

  const bands = [[340,380,"#7957d5"],[380,450,"#4f5ed8"],[450,495,"#218ed2"],[495,570,"#43ad74"],[570,590,"#d9b727"],[590,620,"#eb7d32"],[620,700,"#d94747"],[700,850,"#8e5151"]];
  ctx.save();
  ctx.globalAlpha = .055;
  bands.forEach(([a,b,color]) => { ctx.fillStyle = color; ctx.fillRect(xFor(a), margin.top, xFor(b)-xFor(a), plotH); });
  ctx.restore();

  ctx.strokeStyle = "rgba(91,112,118,.18)";
  ctx.lineWidth = dpr;
  ctx.font = `${11*dpr}px "Cascadia Mono", monospace`;
  ctx.fillStyle = "#53676d";
  ctx.textAlign = "center";
  ctx.textBaseline = "top";
  for (let nm = 350; nm <= 850; nm += 50) {
    const x = xFor(nm);
    ctx.beginPath(); ctx.moveTo(x, margin.top); ctx.lineTo(x, margin.top + plotH); ctx.stroke();
    ctx.fillText(String(nm), x, margin.top + plotH + 12*dpr);
  }
  ctx.textAlign = "right";
  ctx.textBaseline = "middle";
  for (let i = 0; i <= 5; i++) {
    const value = yMin + (yMax-yMin) * i / 5;
    const y = yFor(value);
    ctx.beginPath(); ctx.moveTo(margin.left, y); ctx.lineTo(margin.left+plotW, y); ctx.stroke();
    ctx.fillText(Math.round(value).toLocaleString(), margin.left-10*dpr, y);
  }
  ctx.strokeStyle = "#72868b";
  ctx.lineWidth = 1.2*dpr;
  ctx.beginPath(); ctx.moveTo(margin.left, margin.top); ctx.lineTo(margin.left, margin.top+plotH); ctx.lineTo(margin.left+plotW, margin.top+plotH); ctx.stroke();

  const wavelengths = spectrum?.wavelengths_nm || [];
  const counts = spectrum?.counts || [];
  if (wavelengths.length && wavelengths.length === counts.length) {
    ctx.beginPath();
    ctx.moveTo(xFor(wavelengths[0]), yFor(yMin));
    wavelengths.forEach((nm, i) => ctx.lineTo(xFor(nm), yFor(counts[i])));
    ctx.lineTo(xFor(wavelengths[wavelengths.length-1]), yFor(yMin));
    ctx.closePath();
    ctx.globalAlpha = .42;
    ctx.fillStyle = wavelengthGradient(ctx, margin.left, margin.left+plotW);
    ctx.fill();
    ctx.globalAlpha = 1;
    ctx.beginPath();
    wavelengths.forEach((nm, i) => i ? ctx.lineTo(xFor(nm), yFor(counts[i])) : ctx.moveTo(xFor(nm), yFor(counts[i])));
    ctx.strokeStyle = "#087f77";
    ctx.lineWidth = 2.3*dpr;
    ctx.lineJoin = "round";
    ctx.lineCap = "round";
    ctx.stroke();
  }

  ctx.fillStyle = "#344f57";
  ctx.font = `${12*dpr}px "Bahnschrift", sans-serif`;
  ctx.textAlign = "center";
  ctx.fillText("Wavelength (nm)", margin.left + plotW/2, height-19*dpr);
  ctx.save();
  ctx.translate(18*dpr, margin.top+plotH/2);
  ctx.rotate(-Math.PI/2);
  ctx.fillText("ADC counts", 0, 0);
  ctx.restore();
}

async function pollStatus() {
  try {
    const data = await jsonRequest("/api/v1/status");
    updateStatus(data.status);
  } catch (error) {
    $("livePill").className = "live-pill fault";
    $("livePill").innerHTML = "<i></i>Offline";
    $("statusLine").textContent = error.message;
  } finally {
    setTimeout(pollStatus, 500);
  }
}

async function pollSpectrum() {
  try {
    const data = await jsonRequest("/api/v1/spectrum");
    spectrum = data.spectrum;
    drawSpectrum();
  } catch (_) { /* status poll owns connection reporting */ }
  finally { setTimeout(pollSpectrum, 50); }
}

$("applyExposure").onclick = () => command("/api/v1/exposure", { value: Number($("exposureValue").value), unit: $("exposureUnit").value }, "Exposure applied");
$("autoExposure").onchange = () => command("/api/v1/exposure/auto", { enabled: $("autoExposure").checked }, $("autoExposure").checked ? "Auto exposure enabled" : "Auto exposure held");
$("meterExposure").onclick = () => command("/api/v1/exposure/meter", {}, "Exposure metered and held");
$("autoY").onchange = () => command("/api/v1/y-scale/auto", { enabled: $("autoY").checked }, $("autoY").checked ? "Auto scale enabled" : "Y range held");
$("fitY").onclick = () => command("/api/v1/y-scale/fit", {}, "Current Y range fitted");
$("applyY").onclick = () => command("/api/v1/y-scale/limits", { minimum: Number($("yMin").value), maximum: Number($("yMax").value) }, "Y limits applied");
$("smoothing").onchange = () => command("/api/v1/smoothing", { mode: $("smoothing").value }, "Display filter changed");
$("applyAcquisition").onclick = () => command("/api/v1/acquisition", { averaging: Number($("averaging").value), trigger: $("trigger").value, output_mask: Number($("outputMask").value) }, "Acquisition settings applied");
$("captureDark").onclick = () => command("/api/v1/dark/capture", {}, "Dark reference captured");
$("clearDark").onclick = () => command("/api/v1/dark/clear", {}, "Dark reference cleared");
$("recordButton").onclick = () => command("/api/v1/recording", { enabled: !instrument?.recording }, instrument?.recording ? "Recording stopped" : "Raw recording started");
$("exportButton").onclick = () => { const link = document.createElement("a"); link.download = `spectrum_${new Date().toISOString().replaceAll(":", "-")}.png`; link.href = canvas.toDataURL("image/png"); link.click(); };
$("exposureUnit").onchange = () => { if (!instrument) return; setUnlessFocused($("exposureValue"), $("exposureUnit").value === "us" ? instrument.exposure.us.toFixed(3) : instrument.exposure.ms.toFixed(3)); };

canvas.addEventListener("mousemove", (event) => {
  if (!spectrum?.wavelengths_nm?.length) return;
  const rect = canvas.getBoundingClientRect();
  const nm = 340 + ((event.clientX-rect.left)/rect.width) * 510;
  let index = 0, distance = Infinity;
  spectrum.wavelengths_nm.forEach((value, i) => { const d = Math.abs(value-nm); if (d < distance) { distance=d; index=i; } });
  const tip = $("tooltip");
  tip.hidden = false;
  tip.style.left = `${event.clientX-rect.left}px`;
  tip.style.top = `${event.clientY-rect.top}px`;
  tip.textContent = `${spectrum.wavelengths_nm[index].toFixed(1)} nm  /  ${Math.round(spectrum.counts[index]).toLocaleString()}`;
});
canvas.addEventListener("mouseleave", () => $("tooltip").hidden = true);
new ResizeObserver(resizeCanvas).observe(wrap);
resizeCanvas();
pollStatus();
pollSpectrum();
