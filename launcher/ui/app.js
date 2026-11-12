// The launcher's interface. It draws what the engine reports and sends back what the person presses; it decides
// nothing about how anything is set up or started. Opened in a browser without the app, a small simulator stands in for
// the engine (add ?demo=fresh, setup, hold, ready, start, flight, fatal or failed to the address; &tab= picks a page,
// &still=1 holds everything at rest).
"use strict";
(() => {
  const $ = (id) => document.getElementById(id);
  const NS = "http://www.w3.org/2000/svg";
  const native = window.webkit && window.webkit.messageHandlers && window.webkit.messageHandlers.engine;
  const query = new URLSearchParams(location.search);
  const TARGET_FPS = 25;
  // where each stage of a start begins, as a share of the whole (measured on the test machine: 75, 181, 225 and 279 s)
  const STAGES = [["boot", "Boot", 0], ["renderer", "Render", 0.27], ["level", "Level", 0.65], ["seeded", "Entering", 0.81], ["world", "World", 1]];
  const STAGE_WORDS = { boot: "Starting", renderer: "Starting the renderer", level: "Loading the level", seeded: "Entering the world", world: "In flight" };

  let status = null;              // the engine's last full report
  let flight = null;              // the last telemetry while the game runs
  let download = null;            // the publisher's launcher's progress while it is open
  let fatal = null;               // the game's last fatal error, until the next start
  let autoContinue = false;       // after the first Set up, carry on by itself whenever a hold is resolved
  let stopArmed = 0;
  let swapPeak = 0;               // the most swap in use during the current flight; kept afterwards as this Mac's own figure
  const running = {};             // step id -> {detail, progress}
  const drawn = {};               // what each part of the page last showed: nothing is rebuilt without cause
  const stored = (k, v) => { try { if (v === undefined) return localStorage.getItem(k); localStorage.setItem(k, v); } catch (e) { return null; } };
  const changed = (key, value) => { const s = typeof value === "string" ? value : JSON.stringify(value); if (drawn[key] === s) return false; drawn[key] = s; return true; };
  const put = (node, text) => { if (node.textContent !== text) node.textContent = text; };
  const send = (cmd, extra) => { const m = Object.assign({ cmd }, extra || {}); if (native) native.postMessage(m); else demo.handle(m); };
  const icon = (name) => { const s = document.createElementNS(NS, "svg"), u = document.createElementNS(NS, "use"); u.setAttribute("href", "#i-" + name); s.appendChild(u); return s; };

  const still = query.has("still") || (window.matchMedia && matchMedia("(prefers-reduced-motion: reduce)").matches);
  if (still) document.documentElement.classList.add("still");

  // the backdrop's stars: the same sky every time, drawn once
  (() => {
    let seed = 20261112; const rnd = () => (seed = (seed * 1664525 + 1013904223) >>> 0) / 4294967296;
    const g = $("stars");
    for (let i = 0; i < 110; i++) {
      const x = rnd() * 960, y = rnd() * 460, r = 0.45 + rnd() * rnd() * 1.3;
      if (Math.hypot(x - 722, y - 600) < 424) continue;                       // not on the planet
      const c = document.createElementNS(NS, "circle");
      c.setAttribute("cx", x.toFixed(1)); c.setAttribute("cy", y.toFixed(1)); c.setAttribute("r", r.toFixed(2)); c.setAttribute("opacity", (0.25 + rnd() * 0.6).toFixed(2));
      g.appendChild(c);
    }
  })();

  // ---------- words ----------
  const clock = (s) => `${Math.floor(s / 60)}:${String(Math.floor(s % 60)).padStart(2, "0")}`;
  const gb = (v) => (v == null ? "" : v.toFixed(1) + " GB");
  const stateWord = (s) => {
    const r = running[s.id];
    if (s.state === "running" || r) return r && r.progress != null ? Math.round(r.progress * 100) + "%" : "Working";
    if (s.state === "done") return s.id === "rsi" && s.installed === false ? "Not needed" : "";
    if (s.state === "hold") return "Your turn";
    if (s.state === "failed") return "Failed";
    return "";
  };

  function log(text) {
    const list = $("log"), empty = list.querySelector(".empty"); if (empty) empty.remove();
    const li = document.createElement("li"), t = document.createElement("time"), d = new Date();
    t.textContent = d.toTimeString().slice(0, 8); li.appendChild(t); li.appendChild(document.createTextNode(text));
    list.insertBefore(li, list.firstChild);
    while (list.children.length > 300) list.lastChild.remove();
  }

  // ---------- the whole picture, from the current state ----------
  function phase() {
    if (!status) return "checking";
    if (flight && flight.phase && !["ended", "refused", "failed"].includes(flight.phase)) return flight.phase === "world" ? "flight" : "start";
    if (status.gameRunning) return "flight";
    if (status.launcherOpen) return "launcher";
    if (Object.keys(running).length || status.busy) return "working";
    const steps = status.steps;
    if (steps.some((s) => s.state === "failed")) return "failed";
    if (steps.some((s) => s.state === "hold") && !steps.slice(0, steps.findIndex((s) => s.state === "hold")).some((s) => s.state === "todo")) return "hold";
    return status.ready ? "ready" : "setup";
  }

  // the bar under the status line: a share filled, cuts where the parts meet, and in flight a mark where this Mac's memory ends
  function setBar(p, cuts, limit) {
    const bar = $("bar");
    $("bar-fill").style.setProperty("--p", String(Math.max(0, Math.min(1, p || 0))));
    $("bar-fill").style.setProperty("--limit", limit != null ? (limit * 100).toFixed(2) + "%" : "100%");
    bar.classList.toggle("has-limit", limit != null);
    if (changed("bar-marks", [cuts, limit])) {
      const m = $("bar-marks"); m.textContent = "";
      (cuts || []).forEach((f) => { const i = document.createElement("i"); i.style.left = (f * 100).toFixed(2) + "%"; m.appendChild(i); });
      if (limit != null) { const i = document.createElement("i"); i.className = "limit"; i.style.left = (limit * 100).toFixed(2) + "%"; m.appendChild(i); }
    }
  }
  // under the bar: one thing at each end ...
  function setNote(left, right, strongRight) {
    if (!changed("bar-note", ["ends", left, right, strongRight])) return;
    const n = $("bar-note"); n.textContent = "";
    const a = document.createElement("span"); a.textContent = left || ""; n.appendChild(a);
    const b = document.createElement(strongRight ? "b" : "span"); b.textContent = right || ""; n.appendChild(b);
  }
  // ... or the names of a start's stages, each standing where its part of the bar begins
  function setStages(now) {
    if (!changed("bar-note", ["stages", now])) return;
    const n = $("bar-note"); n.textContent = "";
    STAGES.slice(0, -1).forEach(([, name, f], i) => { const x = document.createElement("span"); x.className = "at" + (i === now ? " now" : ""); x.style.left = (f * 100).toFixed(2) + "%"; x.textContent = name; n.appendChild(x); });
  }
  const STAGE_CUTS = STAGES.slice(1, -1).map((s) => s[2]);

  function render() {
    if (!status) return;
    const ph = phase(), steps = status.steps, done = steps.filter((s) => s.state === "done").length, n = steps.length;
    document.body.dataset.phase = ph;
    const hold = steps.find((s) => s.state === "hold"), failed = steps.find((s) => s.state === "failed"), next = steps.find((s) => s.state !== "done");
    const memMax = Math.max(16, Math.ceil(status.machine.memoryGB * 1.5 / 4) * 4), installed = status.machine.memoryGB;
    const ninths = Array.from({ length: n - 1 }, (_, i) => (i + 1) / n);
    let dot = "", text = "", aside = "", label = "", ico = "", disabled = false, stop = false;

    if (ph === "start") {
      const f = flight || {}, st = f.phase || "boot", t = f.t || 0, usual = Number(stored("worldAfter")) || 0;
      const i = Math.max(0, STAGES.findIndex((s) => s[0] === st)), lo = STAGES[i][2], hi = STAGES[Math.min(i + 1, STAGES.length - 1)][2];
      dot = "busy"; text = STAGE_WORDS[st] || "Starting"; aside = usual ? `${clock(t)} of about ${clock(usual)}` : `${clock(t)}, the first start takes several minutes`;
      setBar(Math.max(lo, Math.min(hi - 0.01, usual ? t / usual : t / 300)), STAGE_CUTS);
      setStages(i);
      label = Date.now() < stopArmed ? "Stop the game?" : "Stop"; ico = "stop"; stop = true;
    } else if (ph === "flight") {
      const f = flight || {}, used = f.gameGB || 0, over = used - installed;
      dot = "go"; text = "In flight"; aside = f.t != null ? clock(f.t) : "";
      setBar(used / memMax, [], installed / memMax);
      setNote(used ? `${gb(used)} in use by the game` : "reading the game's memory", over > 0.05 ? `${over.toFixed(1)} GB past this Mac's ${installed} GB` : `this Mac has ${installed} GB`, over > 0.05);
      label = Date.now() < stopArmed ? "Stop the game?" : "Stop"; ico = "stop"; stop = true;
    } else if (ph === "ready") {
      const usual = Number(stored("worldAfter")) || 0;
      dot = fatal ? "bad" : "go"; text = fatal ? "The game stopped" : "Ready"; aside = usual ? `last start: ${clock(usual)} to the world` : "";
      setBar(0, STAGE_CUTS); setStages(-1); label = "Single player"; ico = "play";      // the same bar, empty, with the stages a start goes through
    } else {
      const cur = steps.find((s) => running[s.id]) || (ph === "launcher" ? steps.find((s) => s.id === "game") : null);
      const frac = cur && running[cur.id] && running[cur.id].progress != null ? running[cur.id].progress : (ph === "launcher" && download && download.progress != null ? download.progress : 0);
      setBar((done + frac) / n, ninths); aside = `${done} of ${n}`;
      setNote("", "");
      if (ph === "working") { dot = "busy"; text = cur ? (running[cur.id].detail || cur.title) : "Setting up"; label = "Setting up"; disabled = true; ico = "setup"; }
      else if (ph === "launcher") {
        const loading = download && download.gb > 0.05;
        dot = loading ? "busy" : "you"; text = loading ? `Downloading the game: ${gb(download.gb)}` : "The publisher's launcher is open";
        setNote(download ? download.phase : "", ""); label = "Launcher open"; disabled = true;
      }
      else if (ph === "hold") { dot = "you"; text = holdWords(hold).status; label = holdWords(hold).button; }
      else if (ph === "failed") { dot = "bad"; text = `${failed.title} failed`; label = "Try again"; }
      else if (ph === "checking") { text = "Checking"; label = "Single player"; disabled = true; ico = "play"; }
      else { text = steps.some((s) => s.id !== "mac" && s.state === "done") ? "Setup is not finished" : "Not set up yet"; aside = next ? `next: ${next.title}` : aside;
        label = steps.some((s) => s.id !== "mac" && s.state === "done") ? "Continue setup" : "Set up"; ico = "setup"; }
    }
    const way = ph === "flight" ? 1 : ph === "start" ? 0.08 + 0.92 * Number($("bar-fill").style.getPropertyValue("--p") || 0) : 0.08;
    if (changed("way", way.toFixed(3))) $("craft").style.setProperty("--way", (way * 100).toFixed(1) + "%");
    $("dot").className = "dot" + (dot ? " " + dot : "");
    put($("status-text"), text); put($("status-aside"), aside);
    const primary = $("primary"); put($("primary-label"), label); primary.disabled = disabled;
    primary.classList.toggle("stop", stop); primary.classList.toggle("armed", stop && Date.now() < stopArmed);
    if (changed("primary-icon", ico)) { $("primary-icon").style.display = ico ? "" : "none"; if (ico) $("primary-icon").firstChild.setAttribute("href", "#i-" + ico); }

    const left = n - done; $("setup-count").hidden = !left; put($("setup-count"), String(left));
    renderPanel(ph, steps, hold, failed);
    renderTiles(ph, installed);
    if (changed("measured", [status.tested, native])) $("measured").textContent = (native ? "" : "Simulated readings. ") + `Measured on one Mac so far: ${status.tested.mac}, ${status.tested.memory}.`;
    put($("version"), status.version + (native ? "" : ", simulator"));
    if (!$("page-settings").hidden) renderSettings();
    if (!$("page-setup").hidden) renderSources();
    // last, when the panel has its final height: the row that matters is brought into view
    const active = $("steps").querySelector('li[data-state="running"], li[data-state="hold"], li[data-state="failed"]');
    if (active && changed("active-row", active.dataset.id + active.dataset.state + ph) && active.scrollIntoView) active.scrollIntoView({ block: "nearest" });
  }

  // the panel beside the title: setup's steps while there is setup to do, and what is the person's to do
  function renderPanel(ph, steps, hold, failed) {
    const setup = ["setup", "working", "hold", "launcher", "failed", "checking"].includes(ph);
    let note = null;                                     // [kind, title, text, action, other]
    if (fatal && (ph === "ready" || ph === "setup")) note = ["bad", fatal, "", ["Show the logs", () => send("reveal", { what: "logs" })], ["Dismiss", () => { fatal = null; render(); }]];
    else if (ph === "failed") note = ["bad", "", failed.detail, null, ["Show the logs", () => send("reveal", { what: "logs" })]];
    else if (ph === "hold") note = ["you", "Your turn", holdWords(hold).text, null, hold.action === "rosetta" ? ["Check again", () => send("status")] : null];
    else if (ph === "launcher") note = ["you", "Your turn", "Sign in and press Install there. Close it when it says the game is installed. Never press Launch Game: that is the online game.", ["Close the launcher", () => send("closeLauncher")], null];
    $("panel").hidden = !(setup || note);
    $("panel-title").hidden = !setup; $("steps").hidden = !setup; $("panel").classList.toggle("brief", !setup);
    const box = $("notice"); box.hidden = !note;
    if (note) {
      box.classList.toggle("bad", note[0] === "bad");
      if (changed("notice-icon", note[0])) $("notice-icon").firstChild.setAttribute("href", note[0] === "bad" ? "#i-alert" : "#i-hand");
      put($("notice-title"), note[1]); put($("notice-text"), note[2]);
      $("notice-head").hidden = !note[1]; $("notice-text").hidden = !note[2];
      const a = $("notice-action"), o = $("notice-other");
      a.hidden = !note[3]; if (note[3]) { put(a, note[3][0]); a.onclick = note[3][1]; }
      o.hidden = !note[4]; if (note[4]) { put(o, note[4][0]); o.onclick = note[4][1]; }
      $("notice-actions").hidden = !note[3] && !note[4];
    }
    if (!setup) return;
    const list = $("steps");
    steps.forEach((s, i) => {
      const state = running[s.id] ? "running" : s.state;
      let li = list.children[i];
      if (!li || li.dataset.id !== s.id) {
        li = document.createElement("li"); li.dataset.id = s.id;
        const mark = document.createElement("span"); mark.className = "mark"; li.appendChild(mark);
        ["name", "state", "detail"].forEach((c) => { const x = document.createElement("span"); x.className = c; li.appendChild(x); });
        if (list.children[i]) list.replaceChild(li, list.children[i]); else list.appendChild(li);
      }
      if (li.dataset.state !== state) {
        li.dataset.state = state; const mark = li.firstChild; mark.textContent = "";
        if (state === "done") mark.appendChild(icon("check")); else if (state === "failed") mark.appendChild(icon("alert"));
      }
      put(li.children[1], s.title); put(li.children[2], stateWord(s)); put(li.children[3], (running[s.id] && running[s.id].detail) || s.detail || "");
    });
    while (list.children.length > steps.length) list.lastChild.remove();
  }

  function holdWords(s) {
    if (s.action === "rosetta") return { status: "Rosetta 2 is needed", button: "Open Terminal", text: "Rosetta 2 is not installed. Apple asks you to agree to its licence, so this only opens Terminal with the command typed. Answer there, then check again." };
    if (s.action === "import") {
      const bottle = (s.found || "").split("/Bottles/")[1] || ""; const name = bottle.split("/")[0] || "another";
      return { status: "The game is already on this Mac", button: "Move the game here", text: `It is in the ${name} bottle. Moving it here is a rename: nothing is copied, no space is used, and a link stays behind for the bottle.` };
    }
    if (s.action === "signin") return { status: "Sign in to download the game", button: "Open the publisher's launcher", text: "Sign in to the publisher's launcher and press Install there. Signing in and accepting its terms are yours to do. The download shows here." };
    return { status: "Your turn", button: "Continue", text: s.detail };
  }
  function holdAction(s) {
    if (s.action === "rosetta") send("rosetta");
    else if (s.action === "import") { autoContinue = true; send("import", { path: s.found }); }
    else if (s.action === "signin") { autoContinue = true; send("openLauncher"); }
    else runSetup();
  }
  function runSetup() { autoContinue = true; send("setup"); }

  // four tiles: what this Mac and this install are, and while the game runs, what it is doing
  function renderTiles(ph, installed) {
    const m = status.machine, game = status.steps.find((s) => s.id === "game") || {}, f = flight || {};
    let rows;                                             // [label, value, note, class]
    if (ph === "flight" || ph === "start") {
      const fps = f.fps && ph === "flight" ? Math.round(f.fps) : null;
      rows = [["Frame rate", fps != null ? `${fps} FPS` : "\u2013", `target ${TARGET_FPS}`, fps == null ? "" : fps >= TARGET_FPS ? "go" : "warn"],
        ["Compressed", f.compressedGB != null ? gb(f.compressedGB) : "\u2013", "held in memory", ""],
        ["Swap", f.swapGB != null ? gb(f.swapGB) : "\u2013", "written to disk", ""],
        ["Paging", f.paging != null ? `${f.paging.toLocaleString("en-US")}/s` : "\u2013", "pages decompressed", ""]];
    } else {
      const v = game.state === "done" ? (game.detail || "").replace("Star Citizen ", "") : "";
      rows = [["This Mac", m.chip.replace("Apple ", ""), `${m.memoryGB} GB, macOS ${m.macos}`, ""],
        ["Game", v ? v.split(" ")[0] : "Not installed", v && v.includes("(") ? "build " + v.split("(")[1].replace(")", "") : "the publisher's own copy", ""],
        ["Graphics profile", status.profile || "–", "for the next start", ""],
        ["Free disk", `${Math.round(m.freeGB)} GB`, Number(stored("swapPeak")) ? `last flight's swap: ${Number(stored("swapPeak")).toFixed(1)} GB` : "the game swaps to disk", ""]];
    }
    const dl = $("tiles");
    if (dl.children.length !== rows.length) { dl.textContent = ""; rows.forEach(() => { const d = document.createElement("div"); d.appendChild(document.createElement("dt")); d.appendChild(document.createElement("dd")); const note = document.createElement("dd"); note.className = "note"; d.appendChild(note); dl.appendChild(d); }); }
    rows.forEach(([label, value, note, cls], i) => { const d = dl.children[i]; put(d.children[0], label); put(d.children[1], value); put(d.children[2], note); if (d.children[1].className !== cls) d.children[1].className = cls; });
  }

  function renderSettings() {
    if (changed("profiles", [status.profiles, status.profile])) {
      const box = $("profiles"); box.textContent = "";
      (status.profiles || []).forEach((name) => {
        const b = document.createElement("button"); b.type = "button"; b.setAttribute("role", "radio"); b.setAttribute("aria-checked", String(name === status.profile)); b.textContent = name;
        b.onclick = () => send("profile", { name }); box.appendChild(b);
      });
    }
    $("mute").setAttribute("aria-checked", String(!!status.muted));
    const rsi = status.steps.find((s) => s.id === "rsi"); $("open-launcher").disabled = !(rsi && rsi.installed) || status.gameRunning;
    put($("about"), `sc-offline-mac ${status.version}. An unofficial fan project, published for research and study, under GPL-3.0. Measured on one Mac so far: ${status.tested.mac}, ${status.tested.memory}.`);
  }

  const SOURCES = {
    mac: ["Checks the chip, macOS, Rosetta 2, memory and free disk space.", "Read from this Mac."],
    wine: ["Runs Windows programs. The game runs in it, through Rosetta.", ["WineHQ's official Mac package, 11.0, at github.com/Gcenx/macOS_Wine_builds", "https://github.com/Gcenx/macOS_Wine_builds/releases/tag/11.0_1"]],
    dxvk: ["Direct3D 11 on Vulkan. The game asks Direct3D which graphics adapter to use before it draws with Vulkan.", ["DXVK for macOS, 1.10.3, at github.com/Gcenx/DXVK-macOS", "https://github.com/Gcenx/DXVK-macOS/releases/tag/v1.10.3-20230507-repack"]],
    kit: ["The patched MoltenVK, the shim in front of it, winegrid and the squeezer.", ["this project, inside the app", "https://github.com/GUNNA-at/sc-offline-mac"]],
    prefix: ["A private Windows folder with Windows 11 settings, DXVK and the Vulkan wrapper.", "Made on this Mac."],
    rsi: ["The publisher's own launcher, used to sign in and to download, verify and update the game.", ["the publisher's download site", "https://robertsspaceindustries.com/en/download"]],
    game: ["Star Citizen itself: downloaded by the publisher's launcher after you sign in, or adopted if this Mac already has it.", "From the publisher, with your own account."],
    mod: ["The mod that runs the game offline.", ["sc-offline 0.7.7 by scuba-sc, on GitHub", "https://github.com/scuba-sc/sc-offline/releases/tag/v0.7.7"]],
    tune: ["The mod's Mac plugin (macfix) and a graphics profile.", ["this project, inside the app", "https://github.com/GUNNA-at/sc-offline-mac"]],
  };
  function renderSources() {
    $("setup-run").disabled = status.ready || status.busy || status.gameRunning;
    const list = $("sources");
    status.steps.forEach((s, i) => {
      const state = running[s.id] ? "running" : s.state, src = SOURCES[s.id] || ["", ""];
      let li = list.children[i];
      if (!li) {
        li = document.createElement("li");
        const mark = document.createElement("span"); mark.className = "mark"; li.appendChild(mark);
        const body = document.createElement("div"), h = document.createElement("h3"), p = document.createElement("p"), from = document.createElement("p"); from.className = "from";
        h.textContent = s.title; p.textContent = src[0];
        if (Array.isArray(src[1])) { const a = document.createElement("button"); a.className = "link"; a.textContent = src[1][0]; a.appendChild(icon("out")); a.onclick = () => send("open", { url: src[1][1] }); from.appendChild(a); }
        else from.textContent = src[1];
        body.appendChild(h); body.appendChild(p); body.appendChild(from); li.appendChild(body);
        const st = document.createElement("span"); st.className = "state"; li.appendChild(st);
        list.appendChild(li);
      }
      if (li.dataset.state !== state) {
        li.dataset.state = state; const mark = li.firstChild; mark.textContent = "";
        if (state === "done") mark.appendChild(icon("check")); else if (state === "failed") mark.appendChild(icon("alert")); else mark.textContent = String(i + 1);
      }
      put(li.lastChild, state === "done" ? (s.id === "rsi" && s.installed === false ? "Not needed" : "Done") : state === "todo" ? "Not yet" : stateWord(s));
    });
  }

  // ---------- what the engine says ----------
  function receive(e) {
    if (e.type === "status") {
      const first = !status; status = e;
      if (first && !$("log").children.length) { const li = document.createElement("li"); li.className = "empty"; li.textContent = "Nothing yet."; $("log").appendChild(li); }
      if (!e.busy) for (const k in running) delete running[k];
      if (!e.gameRunning && flight && ["ended", "refused", "failed"].includes(flight.phase)) flight = null;
      render();
      // carry on by itself once a hold has been resolved
      if (autoContinue && !e.busy && !e.ready && !e.gameRunning && !e.launcherOpen) {
        const next = e.steps.find((s) => s.state !== "done");
        if (next && next.state === "todo") send("setup");
      }
    } else if (e.type === "step") {
      if (!status) return;
      const s = status.steps.find((x) => x.id === e.id); if (!s) return;
      if (e.state === "running") running[e.id] = { detail: e.detail, progress: e.progress };
      else { delete running[e.id]; s.state = e.state; s.detail = e.detail; if (e.state === "done") log(`${s.title}: done`); if (e.state === "failed") log(`${s.title} failed: ${e.detail}`); }
      render();
    } else if (e.type === "setup") {
      if (e.state === "started") log("Setup started");
      if (e.state === "finished") log("Setup finished");
    } else if (e.type === "download") { download = e; render(); }
    else if (e.type === "launcher") {
      log(`Publisher's launcher: ${e.state}${e.message ? " (" + e.message + ")" : ""}`);
      if (e.state !== "open") download = null;
      if (status) { status.launcherOpen = e.state === "open"; render(); }
    } else if (e.type === "art") { setArt(e.url || null); }
    else if (e.type === "flight") {
      const before = flight && flight.phase;
      if (e.phase === "start") { fatal = null; log("Single player: starting"); }
      if (e.phase !== before && STAGE_WORDS[e.phase] && e.phase !== "boot") log(`${STAGE_WORDS[e.phase]} at ${clock(e.t || 0)}`);
      if (e.worldAfter && (!flight || !flight.worldAfter)) stored("worldAfter", String(e.worldAfter));
      if (e.phase === "start") swapPeak = 0;
      if (e.swapGB > swapPeak) swapPeak = e.swapGB;
      if (e.phase === "ended" && swapPeak > 0) stored("swapPeak", swapPeak.toFixed(1));
      if (e.fatal && !fatal) { fatal = e.fatal; log(`The game stopped: ${e.fatal}`); }
      if (e.phase === "refused") log(`Not started: ${e.message}`);
      if (e.phase === "ended") log("The game has ended");
      flight = e;
      if (["ended", "refused", "failed"].includes(e.phase)) { flight = null; stopArmed = 0; }
      render();
    }
  }
  window.launcher = { receive };

  // the person's own picture as the backdrop: it replaces the drawn scene and never leaves this Mac
  function setArt(url) {
    const art = document.querySelector(".art"); let img = art.querySelector("img");
    if (!url) { if (img) img.remove(); $("scene").style.display = ""; $("art-reset").hidden = true; return; }
    if (!img) { img = document.createElement("img"); img.alt = ""; art.appendChild(img); }
    img.src = url; $("scene").style.display = "none"; $("art-reset").hidden = false;
  }

  // ---------- controls ----------
  function pressPrimary() {
    const ph = phase();
    if (ph === "start" || ph === "flight") {
      if (Date.now() < stopArmed) { stopArmed = 0; send("stop"); log("Stop pressed"); render(); }
      else { stopArmed = Date.now() + 3500; render(); setTimeout(render, 3600); }
    } else if (ph === "ready") send("play");
    else if (ph === "hold") holdAction(status.steps.find((s) => s.state === "hold"));
    else if (ph === "setup" || ph === "failed") runSetup();
  }
  $("primary").addEventListener("click", pressPrimary);
  $("setup-run").addEventListener("click", () => { runSetup(); selectTab("play"); });
  $("setup-folder").addEventListener("click", () => send("reveal", { what: "root" }));
  $("show-root").addEventListener("click", () => send("reveal", { what: "root" }));
  $("show-game").addEventListener("click", () => send("reveal", { what: "game" }));
  $("show-logs").addEventListener("click", () => send("reveal", { what: "logs" }));
  $("open-launcher").addEventListener("click", () => { send("openLauncher"); selectTab("play"); });
  $("mute").addEventListener("click", () => { const on = $("mute").getAttribute("aria-checked") !== "true"; $("mute").setAttribute("aria-checked", String(on)); if (status) status.muted = on; send("mute", { on }); });
  $("art-pick").addEventListener("click", () => send("pickArt"));
  $("art-reset").addEventListener("click", () => send("resetArt"));
  $("about-repo").addEventListener("click", () => send("open", { url: "https://github.com/GUNNA-at/sc-offline-mac" }));

  const tabs = Array.from(document.querySelectorAll('[role="tab"]'));
  function selectTab(name, focus) {
    tabs.forEach((t) => { const on = t.id === "tab-" + name; t.setAttribute("aria-selected", String(on)); t.tabIndex = on ? 0 : -1; $(t.getAttribute("aria-controls")).hidden = !on; if (on && focus) t.focus(); });
    if (status) { if (name === "settings") renderSettings(); if (name === "setup") { renderSources(); showActive($("sources")); } }
  }
  function showActive(list) { const row = list.querySelector('li[data-state="running"], li[data-state="hold"], li[data-state="failed"]'); if (row && row.scrollIntoView) row.scrollIntoView({ block: "nearest" }); }
  window.addEventListener("resize", () => { showActive($("steps")); });
  tabs.forEach((t, i) => {
    t.addEventListener("click", () => selectTab(t.id.slice(4)));
    t.addEventListener("keydown", (e) => { if (e.key === "ArrowDown" || e.key === "ArrowUp") { const next = tabs[(i + (e.key === "ArrowDown" ? 1 : tabs.length - 1)) % tabs.length]; selectTab(next.id.slice(4), true); e.preventDefault(); } });
  });
  document.addEventListener("keydown", (e) => {
    if (e.metaKey && e.key >= "1" && e.key <= "4") { selectTab(tabs[Number(e.key) - 1].id.slice(4)); e.preventDefault(); }
    if (e.key === "Enter" && (document.activeElement === document.body || document.activeElement === null) && !$("primary").disabled && !$("page-play").hidden) pressPrimary();
  });

  // ---------- without the app: a simulator, for looking at the design in a browser ----------
  const demo = (() => {
    const steps = (states) => [["mac", "This Mac", "Apple M4 Pro, 24 GB, macOS 26.2"], ["wine", "Wine", "WineHQ 11.0, the official Mac build"], ["dxvk", "DXVK", "1.10.3 for macOS"], ["kit", "Kit parts", "MoltenVK, shim, winegrid, squeezer"],
      ["prefix", "Windows folder", "Windows 11 settings, DXVK and the Vulkan wrapper in place"], ["rsi", "Publisher's launcher", "installed"], ["game", "Game", "Star Citizen 4.10.2 (12913470)"], ["mod", "sc-offline mod", "sc-offline 0.7.7"], ["tune", "macfix and profile", "profile: pretty"]]
      .map(([id, title, detail], i) => Object.assign({ id, title, detail, state: states[i] || "todo" }, id === "rsi" ? { installed: true } : {}));
    const base = (states, extra) => Object.assign({ type: "status", version: "0.1.0", ready: states.every((s) => s === "done"), gameRunning: false, launcherOpen: false, busy: false, profile: "pretty", profiles: ["fast", "lean", "low", "pretty"], muted: false,
      machine: { chip: "Apple M4 Pro", memoryGB: 24, freeGB: 15.2, macos: "26.2", appleSilicon: true, rosetta: true }, tested: { mac: "MacBook Pro 16-inch, M4 Pro", memory: "24 GB", macos: "26.2", game: "LIVE 4.10.2", mod: "sc-offline 0.7.7" }, steps: steps(states) }, extra || {});
    const D = "done", T = "todo";
    const scenes = {
      fresh: () => [base([D, T, T, T, T, T, T, T, T])],
      setup: () => [base([D, T, T, T, T, T, T, T, T], { busy: true }), { type: "step", id: "wine", state: "running", detail: "Wine 11.0: 116 of 177 MB", progress: 0.66 }],
      hold: () => { const s = base([D, D, D, D, D, D, "hold", T, T]); s.steps[5].installed = false; s.steps[5].detail = "not needed: the game is already here"; Object.assign(s.steps[6], { detail: "found in the StarCitizen bottle", action: "import", found: "~/Library/Application Support/CrossOver/Bottles/StarCitizen/drive_c/Program Files/Roberts Space Industries/StarCitizen" }); return [s]; },
      ready: () => [base([D, D, D, D, D, D, D, D, D])],
      start: () => [base([D, D, D, D, D, D, D, D, D], { gameRunning: true }), { type: "flight", phase: "level", t: 196, fps: 0 }],
      flight: () => [base([D, D, D, D, D, D, D, D, D], { gameRunning: true }), { type: "flight", phase: "world", t: 412, worldAfter: 279, fps: 35.6, gameGB: 25.7, compressedGB: 3.1, swapGB: 11.5, paging: 871, freeGB: 2.2 }],
      fatal: () => [base([D, D, D, D, D, D, D, D, D]), { type: "flight", phase: "level", t: 120, fps: 0, fatal: "Failed to open render library." }, { type: "flight", phase: "ended" }],
      failed: () => { const s = base([D, "failed", T, T, T, T, T, T, T]); s.steps[1].detail = "Wine 11.0: download failed (the network connection was lost)"; return [s]; },
    };
    let timer = null;
    return {
      start() {
        const name = query.get("demo") || "ready";
        if (name === "flight" || name === "start" || name === "ready") { stored("worldAfter", "279"); stored("swapPeak", "14.5"); }
        (scenes[name] || scenes.ready)().forEach(receive);
        if (query.get("tab")) selectTab(query.get("tab"));
      },
      handle(m) {
        if (m.cmd === "status") return;
        if (m.cmd === "play") {
          clearInterval(timer); let t = 0; const plan = [[0, "boot"], [5, "renderer"], [11, "level"], [16, "seeded"], [21, "world"]];
          status.gameRunning = true; receive({ type: "flight", phase: "start", t: 0 });
          timer = setInterval(() => { t += 1; const st = plan.filter((p) => p[0] <= t).pop()[1]; const world = st === "world";
            receive({ type: "flight", phase: st, t: Math.round(t * 13), fps: world ? 30 + Math.sin(t / 2) * 6 : st === "boot" ? 0 : 8 + t % 5, gameGB: world ? 25.4 + Math.sin(t / 3) : Math.min(24, t * 1.1), compressedGB: 3 + (t % 4) / 3, swapGB: world ? 11.5 : t / 3, paging: world ? 300 + (t * 37) % 700 : 40000, worldAfter: world ? 279 : undefined }); }, 1000);
        }
        if (m.cmd === "stop") { clearInterval(timer); status.gameRunning = false; receive({ type: "flight", phase: "ended" }); }
        if (m.cmd === "profile") { status.profile = m.name; render(); }
        if (m.cmd === "setup" || m.cmd === "import") {
          const todo = status.steps.filter((s) => s.state !== "done"); let i = 0, p = 0; status.busy = true;
          clearInterval(timer); timer = setInterval(() => { const s = todo[i]; if (!s) { clearInterval(timer); status.busy = false; status.ready = true; receive(Object.assign({}, status)); return; }
            p += 0.25; if (p >= 1) { receive({ type: "step", id: s.id, state: "done", detail: s.detail }); i += 1; p = 0; } else receive({ type: "step", id: s.id, state: "running", detail: `${s.title}: working`, progress: p }); }, 350);
        }
      },
    };
  })();

  if (native) send("status"); else demo.start();
  setInterval(() => { if (native && document.visibilityState === "visible" && status && !status.gameRunning && !status.busy) send("status"); }, 15000);
})();
