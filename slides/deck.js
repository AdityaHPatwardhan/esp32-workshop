/*
 * ESP32 Unboxed: deck behaviour on top of reveal.js.
 * Edit names, links and URLs in config.js; slide content lives in index.html.
 */
(() => {
  const $ = id => document.getElementById(id);
  const params = new URLSearchParams(location.search);

  // ---- CONFIG placeholders and hosts
  document.querySelectorAll("[data-cfg]").forEach(el => { el.textContent = CONFIG[el.dataset.cfg] ?? ""; });
  if (!CONFIG.date && $("dateSep")) $("dateSep").hidden = true;
  // clickable links (shown without https://)
  document.querySelectorAll("[data-cfg-link]").forEach(el => {
    const url = CONFIG[el.dataset.cfgLink] ?? "";
    el.href = url; el.target = "_blank"; el.textContent = url.replace(/^https?:\/\//, "");
  });
  document.querySelectorAll("[data-hosts]").forEach(el => {
    el.innerHTML = CONFIG.hosts.map(h => `<div><b>${h.name}</b><span>${h.org}</span></div>`).join("");
  });

  // ---- real 8x8 pixel crop from a board frame ("An image is just numbers")
  const PIX = [[142,145,145,146,146,146,148,150],[146,146,147,148,148,150,151,152],[148,149,150,149,151,152,154,154],[149,149,149,150,152,152,155,80],[150,151,151,153,154,156,161,243],[151,151,152,154,156,159,132,255],[153,154,153,156,159,161,218,255],[154,155,155,157,161,133,254,166]];
  const grid = $("pixgrid");
  if (grid) {
    grid.style.cssText += "display:grid;grid-template-columns:repeat(8,46px);gap:3px";
    PIX.flat().forEach(v => {
      const c = document.createElement("div");
      c.textContent = v;
      c.style.cssText = `width:46px;height:46px;display:grid;place-items:center;font:700 14px var(--mono);border-radius:4px;background:rgb(${v},${v},${v});color:${v > 140 ? "#0b1020" : "#ffffff"}`;
      grid.appendChild(c);
    });
  }

  // ---- interactive neuron
  const X = [148, 80, 243];
  function neuron() {
    if (!$("w1")) return;
    const w = [1, 2, 3].map(i => parseFloat($("w" + i).value));
    const b = parseFloat($("nb").value);
    const sum = X.reduce((a, x, i) => a + x * w[i], 0) + b;
    const out = Math.max(0, sum);
    w.forEach((v, i) => { $(`w${i + 1}v`).textContent = v.toFixed(1); $(`w${i + 1}l`).textContent = "× " + v.toFixed(1).replace("-", "−"); });
    $("nbv").textContent = b;
    $("bl").textContent = (b < 0 ? "− " : "+ ") + Math.abs(b);
    $("nsum").textContent = `Σ = ${X.map((x, i) => `${x}·${w[i].toFixed(1)}`).join(" + ")} ${b < 0 ? "−" : "+"} ${Math.abs(b)} = ${sum.toFixed(0)}`;
    $("nout").textContent = out.toFixed(0);
    $("nfire").innerHTML = out > 0 ? `<span class="green">fires: ${out.toFixed(0)}</span>` : `<span class="dim">silent: 0</span>`;
  }
  ["w1", "w2", "w3", "nb"].forEach(id => $(id) && $(id).addEventListener("input", neuron));
  neuron();

  // ---- interactive quantization
  function quant() {
    if (!$("qs")) return;
    const v = parseFloat($("qs").value), scale = 1 / 127;
    const q = Math.max(-128, Math.min(127, Math.round(v / scale)));
    const back = q * scale;
    $("qf").textContent = `${v.toFixed(8)} (float32, 4 bytes)`;
    $("qi").textContent = `${q} (int8)`;
    $("qe").textContent = `1 byte · back to ${back.toFixed(4)} · error ${Math.abs(v - back).toFixed(4)}`;
    $("qdot").setAttribute("cx", 680 + v * 420);
    $("qdl").setAttribute("x", 680 + v * 420);
    $("qdl").textContent = `${v.toFixed(2)} → ${q}`;
  }
  $("qs") && $("qs").addEventListener("input", quant);
  quant();

  // ---- "you are here" footer on every content slide
  const colors = { amber: "var(--amber)", cyan: "var(--cyan)", violet: "var(--violet)", green: "var(--green)", pink: "var(--pink)" };
  let part = null;
  document.querySelectorAll(".reveal .slides .slide").forEach(s => {
    if (s.closest("section").dataset.title === "Recap") part = null;
    const p = s.classList.contains("divider") && s.querySelector(".part");
    if (p && !/break/i.test(p.textContent)) {
      const col = [...p.classList].find(c => colors[c]);
      part = { label: p.textContent + " · " + s.querySelector("h1").textContent, color: colors[col] };
      return;
    }
    if (part && !s.classList.contains("divider")) {
      const chip = document.createElement("div");
      chip.className = "partchip";
      chip.innerHTML = `<i style="background:${part.color}"></i>${part.label}`;
      s.appendChild(chip);
    }
  });

  // ---- live dashboard embed (slide with data-live)
  let liveLoaded = false;
  function live(slide) {
    const fr = $("liveFrame");
    if (!fr || liveLoaded || !slide || !slide.hasAttribute("data-live")) return;
    const url = CONFIG.dashboardLocal;
    $("liveState").textContent = "connecting…";
    fetch(url + "/state", { mode: "no-cors", cache: "no-store" })
      .then(() => { fr.src = url; fr.hidden = false; liveLoaded = true; $("liveState").textContent = url.replace(/^https?:\/\//, ""); })
      .catch(() => { $("liveState").textContent = "dashboard not running: showing a screenshot"; });
  }

  Reveal.initialize({
    width: 1280,
    height: 720,
    margin: 0,
    minScale: 0.2,
    maxScale: 3,
    center: false,
    hash: true,
    controls: true,
    controlsTutorial: true,
    progress: true,
    slideNumber: "c/t",
    transition: "slide",          // sessions slide in horizontally, their slides vertically
    backgroundTransition: "fade",
    fragments: !params.has("all"), // ?all shows every step at once (printing, screenshots)
    plugins: [RevealNotes],
  });
  Reveal.on("ready", e => live(e.currentSlide));
  Reveal.on("slidechanged", e => live(e.currentSlide));
})();
