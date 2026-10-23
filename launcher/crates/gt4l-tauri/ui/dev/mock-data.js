/*
 * Development mock of the Rust command layer. Loaded only by ui/dev/index.html.
 * It answers the same command names and payloads as the Tauri build, so the
 * pages render in a plain browser. Values are sample values, not game data.
 *
 * Query options (ui/dev/index.html?...):
 *   setup=done|needed|wrong|copying|checking|preparing|renderer|failed|missing|offline   (default done)
 *   pick=spec                                       (setup: the picker returns an unsupported image, refused)
 *   pick=other                                      (setup: the picker returns the other game's disc, a mismatch)
 *   pick=spec2                                      (setup: the picker returns a Spec II disc)
 *   games=gt4|spec2|both|none                       (which games are installed; default gt4: Spec II shows Install)
 *   selected=spec2                                  (the game the home screen starts on)
 *   running=1                                       (game running at start)
 *   focus=play                                      (home: focus the Play button, for screenshots)
 *   Controls: pad=A,LB,DpadRight lt=0.6 rt=0.2 lx=0.7 ly=-0.45 rx= ry= pads=2 nopad=1 (a still dev controller),
 *             select=circle mode=key capture=hold|conflict remap=1 locked=1
 */
(function (root) {
  "use strict";

  var query = new URLSearchParams(root.location.search);
  var setupStart = query.get("setup") || "done";
  var IMAGE_BYTES = 5314478080;
  var SPEC_MESSAGE = "This disc is not supported yet. GT4Recomp supports Gran Turismo 4 (USA, SCUS-97328) and " +
    "Gran Turismo 4 Spec II 1.10 (SCUS-97436). This is the unmodified GT4 Online Public Beta (SCUS-97436), not Gran Turismo 4 Spec II.";

  // Two games (TASK-101). The mock keeps the same shapes as commands::game_row and app_state.
  var gamesParam = query.get("games") || "gt4";
  var GAME_TABLE = [
    { id: "gt4", name: "Gran Turismo 4", installed: gamesParam === "gt4" || gamesParam === "both",
      disc_name: "Gran Turismo 4 \u00b7 USA \u00b7 v2.00", size_bytes: 5314478080 + 18000000 },
    { id: "spec2", name: "Gran Turismo 4 Spec II", installed: gamesParam === "spec2" || gamesParam === "both",
      disc_name: "Gran Turismo 4 Spec II \u00b7 Spec II 1.10", size_bytes: 3683418112 + 6520944 }
  ];
  var selectedGame = query.get("selected") ||
    (GAME_TABLE.filter(function (game) { return game.installed; })[0] || GAME_TABLE[0]).id;
  var setupGame = null; // the game of the setup that runs (set by setup_choose_disc)
  function gameById(id) {
    return GAME_TABLE.filter(function (game) { return game.id === id; })[0] || null;
  }
  function gamesView() {
    return {
      selected: selectedGame,
      any_installed: GAME_TABLE.some(function (game) { return game.installed; }),
      games: GAME_TABLE.map(function (game) {
        return { id: game.id, name: game.name, installed: game.installed, damaged: false, reason: "",
          selected: game.id === selectedGame, disc_name: game.installed ? game.disc_name : "",
          folder: "C:\\Program Files\\GT4Recomp\\data\\games\\" + game.id,
          size_bytes: game.installed ? game.size_bytes : 0 };
      })
    };
  }

  var setup = { state: "done", step: "copying", progress: 0, bytes_done: 0, bytes_total: 0, renderer_step: "",
    disc_name: "Gran Turismo 4 \u00b7 USA \u00b7 v2.00", problem: "", note: "" };
  function running(step, progress) {
    setup.state = "checking";
    setup.step = step;
    setup.progress = progress;
    setup.bytes_total = step === "copying" ? IMAGE_BYTES : (step === "renderer" ? 48000000 : 0);
    setup.bytes_done = Math.round(setup.bytes_total * progress);
    setup.renderer_step = step === "renderer" ? "downloading" : "";
  }
  if (setupStart === "wrong") {
    setup.state = "wrong_disc";
    setup.disc_name = "Gran Turismo 4 Spec II v1.10.iso";
    setup.problem = SPEC_MESSAGE;
  } else if (setupStart === "needed") {
    setup.state = "needed";
    setup.disc_name = "";
  } else if (setupStart === "copying" || setupStart === "checking") {
    running("copying", 0.62);
  } else if (setupStart === "preparing") {
    running("preparing", 0.5);
  } else if (setupStart === "renderer") {
    running("renderer", 0.4);
  } else if (setupStart === "failed") {
    setup.state = "failed";
    setup.problem = "Not enough free space to copy the game files: 5.8 GB needed, 2.1 GB free. Free up space and try again.";
  } else if (setupStart === "missing") {
    setup.state = "files_missing";
    setup.disc_name = "";
    setup.problem = "Game files missing (SCUS-97328.iso is missing). Choose your disc image again.";
  } else if (setupStart === "offline") {
    setup.note = "GSdx could not be downloaded, so the game cannot start yet. GT4Recomp tries again at the next start.";
  }

  // setup_start: walk the steps on a timer, like the Rust thread.
  var PLAN = [["copying", 0], ["copying", 0.35], ["copying", 0.7], ["copying", 1], ["checking", 1], ["preparing", 0.5],
    ["renderer", 0.3], ["renderer", 0.8], ["renderer", 1]];
  var setupTimer = null;
  function startSetup() {
    var index = 0;
    clearInterval(setupTimer);
    setupTimer = setInterval(function () {
      if (index >= PLAN.length) {
        clearInterval(setupTimer);
        setup.state = "done";
        setup.renderer_step = "done";
        var installedGame = gameById(setupGame || selectedGame);
        if (installedGame) {
          installedGame.installed = true;
          selectedGame = installedGame.id;
          setup.disc_name = installedGame.disc_name;
        }
        return;
      }
      running(PLAN[index][0], PLAN[index][1]);
      index += 1;
    }, 500);
  }

  var gameRunning = query.get("running") === "1";

  // Read by pages/home.js: focuses Play on mount. Only the dev harness sets it.
  root.GT4 = root.GT4 || {};
  root.GT4.devFocus = query.get("focus") || "";
  var maxUpscale = 4;

  // Settings rows, the same shape as the Rust table.
  var DEFAULTS = {};
  var rows = [
    { id: "renderer", label: "Renderer", help: "GSdx draws the game on your GPU in its own process; it is the renderer GT4Recomp uses. CoreGS 2.0 is in development.",
      page: "basic", group: "graphics", control: "dropdown",
      values: [
        { value: "pcsx2", label: "GSdx" },
        { value: "coregs-2.0", label: "CoreGS 2.0 (coming soon)", disabled: true }
      ],
      default: "pcsx2", current: "pcsx2" },
    // TASK-080 addendum: no Aspect ratio and no Internal resolution row. Render scale (50% to 200%) with the
    // resulting internal resolution as its readout (labels are filled by tableView from the chosen resolution).
    { id: "render_scale", label: "Render scale", help: "How sharp the picture is drawn. 100% draws it at least at your resolution; 50% is faster and softer, 200% is sharper and needs a much faster GPU.",
      page: "basic", group: "graphics", control: "slider",
      values: { min: 50, max: 200, step: 25, unit: "%" },
      default: 100, current: 100 },
    { id: "fxaa", label: "Anti-aliasing (FXAA)", help: "Smooths jagged edges with a fast filter. It softens fine detail a little. Off by default.",
      page: "basic", group: "graphics", control: "toggle", values: null, default: false, current: false },
    { id: "performance_mode", label: "Performance Mode", help: "Faster on weaker PCs. Some effects that read the image back are skipped, such as headlight and sun flares and the car icons next to the gap time. Applies the next time you play.",
      page: "basic", group: "graphics", control: "toggle", values: null, default: false, current: false },
    { id: "cas_mode", label: "Sharpening (CAS)", help: "FidelityFX Contrast Adaptive Sharpening. Off leaves the picture as the game drew it.",
      page: "basic", group: "graphics", control: "dropdown",
      values: [
        { value: "off", label: "Off" },
        { value: "sharpen-only", label: "Sharpen only" },
        { value: "sharpen-and-resize", label: "Sharpen and resize" }
      ],
      default: "off", current: "off" },
    { id: "cas_sharpness", label: "Sharpening amount", help: "How strong the CAS sharpening is. Used when CAS is on.",
      page: "basic", group: "graphics", control: "slider",
      values: { min: 0, max: 100, step: 1, unit: "%" },
      default: 50, current: 50 },
    { id: "monitor", label: "Monitor", help: "The screen the game opens on. The primary monitor is used until you pick another one.",
      page: "basic", group: "display", control: "dropdown", values: [], default: "primary", current: "primary" },
    { id: "resolution", label: "Resolution", help: "The size of the picture, from the sizes your monitor supports. The shape of the screen follows it.",
      page: "basic", group: "display", control: "dropdown", values: [], default: "desktop", current: "desktop" },
    { id: "fullscreen", label: "Fullscreen", help: "Start in fullscreen. Alt+Enter or F11 switches while playing.",
      page: "basic", group: "display", control: "toggle", values: null, default: false, current: true },
    { id: "vsync", label: "VSync", help: "On keeps motion smooth with no tearing. Off lowers latency and may tear.",
      page: "basic", group: "display", control: "dropdown",
      values: [
        { value: "on", label: "On" },
        { value: "off", label: "Off" }
      ],
      default: "on", current: "on" },
    { id: "async_gs", label: "Asynchronous GS", help: "Sends graphics work to the GPU without waiting. Needed for full speed; turn off only to compare.",
      page: "advanced", control: "toggle", values: null, default: true, current: true },
    { id: "unsynced_readbacks", label: "Unsynced readbacks", help: "Faster, but effects that read the screen will be wrong.",
      page: "advanced", control: "toggle", values: null, default: false, current: false },
    { id: "performance_report", label: "Performance report", help: "Records frame timing while you play for bug reports.",
      page: "advanced", control: "toggle", values: null, default: false, current: false },
    { id: "perf_overlay", label: "Performance overlay", help: "Shows GT4Recomp's own overlay while you play (FPS, frame time, CPU and GPU use).",
      page: "advanced", control: "toggle", values: null, default: false, current: false },
    { id: "perf_overlay_fps", label: "Overlay: FPS", help: "Shows the frame rate in the performance overlay.",
      page: "advanced", control: "toggle", values: null, default: true, current: true },
    { id: "debug_internal", label: "Internal debug", help: "Not shown to users.",
      page: "hidden", control: "toggle", values: null, default: false, current: true },
    // TASK-091 (1.2.1): the launcher's own row, appended by the Rust table (commands::get_settings_table).
    { id: "launcher_gpu", label: "Launcher GPU acceleration", help: "Off keeps the launcher window without GPU acceleration, after it failed to draw in WebView2. On takes effect at the next start of the launcher.",
      page: "advanced", control: "toggle", values: null, default: true, current: true }
  ];
  rows.forEach(function (row) {
    DEFAULTS[row.id] = row.default;
  });

  // Two sample monitors (the Rust table lists the connected ones; see model::settings_table_with_monitors).
  var MONITORS = [
    { id: "\\\\.\\DISPLAY1@1920x1080", label: "TV \u2013 1920x1080 (primary)", desktop: "1920x1080",
      modes: ["1920x1080", "1600x900", "1280x720", "1024x768", "800x600"] },
    { id: "\\\\.\\DISPLAY2@3440x1440", label: "DELL U3419W \u2013 3440x1440", desktop: "3440x1440",
      modes: ["3440x1440", "2560x1080", "1920x1080", "1680x1050", "1280x720"] }
  ];
  // The rule of gt4l_core::display::display_plan: k = ceil(max(W / 640, H / 448)), times the scale, ties up, 1..12.
  function renderLabels(output) {
    var size = output.split("x").map(Number);
    var k = Math.max(Math.ceil(size[0] / 640), Math.ceil(size[1] / 448));
    var labels = {};
    for (var step = 50; step <= 200; step += 25) {
      var m = Math.min(12, Math.max(1, Math.floor((2 * k * step + 100) / 200)));
      labels[String(step)] = step + "% \u00B7 " + m + "x \u00B7 " + (640 * m) + "x" + (448 * m);
    }
    return labels;
  }
  function fillDisplayRows() {
    var monitorRow = findRow("monitor");
    var chosen = MONITORS.filter(function (m) { return m.id === monitorRow.current; })[0] || MONITORS[0];
    monitorRow.current = chosen.id;
    monitorRow.values = MONITORS.map(function (m) { return { value: m.id, label: m.label }; });
    var resolutionRow = findRow("resolution");
    if (chosen.modes.indexOf(resolutionRow.current) < 0) {
      resolutionRow.current = chosen.desktop;
    }
    resolutionRow.default = chosen.desktop;
    resolutionRow.values = chosen.modes.map(function (mode) {
      return { value: mode, label: mode === chosen.desktop ? mode + " (desktop)" : mode };
    });
    findRow("render_scale").values.labels = renderLabels(resolutionRow.current);
  }

  function tableView() {
    fillDisplayRows();
    return {
      max_upscale: maxUpscale,
      rows: rows.map(function (row) {
        return JSON.parse(JSON.stringify(row));
      })
    };
  }

  function findRow(id) {
    for (var i = 0; i < rows.length; i++) {
      if (rows[i].id === id) {
        return rows[i];
      }
    }
    return null;
  }

  // Demo offers for the update popup (?update=1). Notes are sample text, not a real release.
  var DEMO_OFFERS = [
    { stream: "renderer", installed: "1.2.1", latest: "1.2.2", update: true,
      package: "gt4-gs-1.1.1-windows-x64.zip",
      notes: "## gt4-gs 1.1.1\n\n- **Faster** present on VSync Off.\n- Fixes a crash on exit when the game ends first.\n- Sample text for the launcher preview."},
    { stream: "launcher", installed: "1.3.0", latest: "1.3.1", update: true,
      package: "GT4Recomp-Setup-1.1.1.exe",
      notes: "GT4Recomp 1.1.1\n\n* Tray: the launcher comes back when the game ends.\n* Sample text for the launcher preview." }
  ];
  // Controls: the built-in mapping of docs/CONTROLS.md, with the same assign/swap rules as gt4controls (Rust).
  var CONTROL_ORDER = ["dpad_up", "dpad_right", "dpad_down", "dpad_left", "cross", "circle", "square", "triangle",
    "l1", "r1", "l2", "r2", "l3", "r3", "select", "start"];
  var CONTROL_NAMES = { dpad_up: "D-pad up", dpad_right: "D-pad right", dpad_down: "D-pad down", dpad_left: "D-pad left",
    cross: "Cross", circle: "Circle", square: "Square", triangle: "Triangle", l1: "L1", r1: "R1", l2: "L2", r2: "R2",
    l3: "L3", r3: "R3", select: "Select", start: "Start" };
  var DEFAULT_SOURCES = {
    dpad_up: ["xinput:DpadUp", "key:Up"], dpad_right: ["xinput:DpadRight", "key:Right"],
    dpad_down: ["xinput:DpadDown", "key:Down"], dpad_left: ["xinput:DpadLeft", "key:Left"],
    cross: ["xinput:A", "key:Z"], circle: ["xinput:B", "key:X"], square: ["xinput:X", "key:A"], triangle: ["xinput:Y", "key:S"],
    l1: ["xinput:LB", "key:Q"], r1: ["xinput:RB", "key:W"], l2: ["xinput:LT", "key:1"], r2: ["xinput:RT", "key:2"],
    l3: ["xinput:LThumb", "key:C"], r3: ["xinput:RThumb", "key:V"], select: ["xinput:Back", "key:Backspace"],
    start: ["xinput:Start", "key:Enter"]
  };
  var RESERVED = { F7: true, F8: true, F9: true, F11: true };
  var bindings = JSON.parse(JSON.stringify(DEFAULT_SOURCES));
  // ?remap=1: a few changed buttons, so the "Changed" state shows in screenshots.
  if (query.get("remap") === "1") {
    bindings.cross = ["xinput:B", "key:Z"];
    bindings.circle = ["xinput:A", "key:X"];
    bindings.select = ["xinput:Back"];
  }

  function analogDefaults() {
    var curve = [{ value: "linear", label: "Linear" }, { value: "quadratic", label: "Quadratic" }, { value: "cubic", label: "Cubic" }];
    function slider(id, label, def) {
      return { id: id, label: label, help: "How far it must travel before it counts, in percent of its travel.",
        page: "basic", group: "controls", control: "slider", values: { min: 0, max: 95, step: 1, unit: "%" },
        default: def, current: def };
    }
    function dropdown(id, label) {
      return { id: id, label: label, help: "How the pressure grows with the trigger travel.", page: "basic",
        group: "controls", control: "dropdown", values: curve, default: "linear", current: "linear" };
    }
    return [slider("l2_deadzone", "L2 deadzone", 11.8), dropdown("l2_curve", "L2 curve"),
      slider("r2_deadzone", "R2 deadzone", 11.8), dropdown("r2_curve", "R2 curve"),
      slider("lstick_deadzone", "Left stick deadzone", 24), slider("rstick_deadzone", "Right stick deadzone", 26.5)];
  }
  var analog = analogDefaults();

  function base(source) { return String(source).split("|")[0].trim(); }
  function familyOf(source) { return base(source).split(":")[0]; }
  function nameOf(source) { return base(source).split(":")[1] || ""; }

  function controlsView() {
    return {
      controls: CONTROL_ORDER.map(function (id) {
        var sources = bindings[id].slice();
        var pad = sources.filter(function (s) { return familyOf(s) === "xinput"; })[0];
        var key = sources.filter(function (s) { return familyOf(s) === "key"; })[0];
        return { id: id, name: CONTROL_NAMES[id], binding: sources.length ? nameOf(sources[0]) : "-", sources: sources,
          xinput: pad ? nameOf(pad) : null, key: key ? nameOf(key) : null,
          is_default: JSON.stringify(sources) === JSON.stringify(DEFAULT_SOURCES[id]) };
      }),
      triggers: JSON.parse(JSON.stringify(analog)),
      locked: query.get("locked") === "1"
    };
  }

  function ordered(list) {
    return list.filter(function (s) { return familyOf(s) === "xinput"; }).concat(list.filter(function (s) { return familyOf(s) !== "xinput"; }));
  }

  // The rules of gt4controls::Controls::assign: same family replaced, swap with the holder, refusal without swap.
  function assignBinding(id, source, swap) {
    var b = base(source);
    if (!bindings[id]) {
      throw new Error("'" + id + "' is not a button input");
    }
    if (familyOf(b) === "key" && RESERVED[nameOf(b)]) {
      throw new Error("key '" + nameOf(b) + "' is reserved by the launcher");
    }
    var current = bindings[id];
    if (current.some(function (s) { return base(s) === b; })) {
      var same = controlsView();
      same.swapped_with = null;
      same.unchanged = true;
      return same;
    }
    var previous = current.filter(function (s) { return familyOf(s) === familyOf(b); })[0] || null;
    var holder = CONTROL_ORDER.filter(function (other) {
      return other !== id && bindings[other].some(function (s) { return base(s) === b; });
    })[0] || null;
    if (holder) {
      if (!swap) {
        throw new Error("'" + b + "' is already bound to " + holder);
      }
      var replaced = [];
      bindings[holder].forEach(function (s) {
        var incoming = base(s) === b ? previous : s;
        if (incoming && !replaced.some(function (kept) { return base(kept) === base(incoming); })) {
          replaced.push(incoming);
        }
      });
      bindings[holder] = ordered(replaced);
    }
    bindings[id] = ordered(current.filter(function (s) { return familyOf(s) !== familyOf(b); }).concat([b]));
    var view = controlsView();
    view.swapped_with = holder;
    view.unchanged = false;
    return view;
  }

  // Dev-only live input for the Controls page: ?pad=A,LB,DpadRight&lt=0.6&rt=0&lx=0.7&ly=-0.45&rx=-0.3&ry=0.6&pads=2
  // builds a still Gamepad-like object (standard mapping). ?nopad=1 shows "No controller".
  var STANDARD = ["A", "B", "X", "Y", "LB", "RB", "LT", "RT", "Back", "Start", "LThumb", "RThumb", "DpadUp", "DpadDown",
    "DpadLeft", "DpadRight"];
  function devPad(index) {
    var pressed = (query.get("pad") || "").split(",").filter(Boolean);
    var buttons = STANDARD.map(function (name) {
      var down = pressed.indexOf(name) >= 0;
      var value = down ? 1 : 0;
      if (name === "LT" && query.get("lt")) { value = Number(query.get("lt")); }
      if (name === "RT" && query.get("rt")) { value = Number(query.get("rt")); }
      return { pressed: value > 0.12, value: value };
    });
    var axes = ["lx", "ly", "rx", "ry"].map(function (key) { return Number(query.get(key) || 0); });
    return { id: "Xbox Wireless Controller (XInput STANDARD GAMEPAD)", index: index, connected: true,
      mapping: "standard", buttons: buttons, axes: axes };
  }
  if (query.get("pad") !== null || query.get("lt") !== null || query.get("lx") !== null || query.get("pads") !== null) {
    var devPads = [devPad(0)];
    if (query.get("pads") === "2") {
      var second = devPad(1);
      second.id = "Controller (Gamepad for Xbox 360)";
      devPads.push(second);
    }
    root.GT4.devGamepads = function () { return devPads; };
  } else if (query.get("nopad") === "1") {
    root.GT4.devGamepads = function () { return []; };
  }
  // ?select=circle&mode=key&capture=hold|conflict (Controls screenshots; capture starts once the page is entered).
  root.GT4.devControls = {
    select: query.get("select") || "",
    mode: query.get("mode") || "",
    capture: query.get("capture") || ""
  };

  var pendingCapture = null;
  function captureBinding() {
    return new Promise(function (resolve, reject) {
      pendingCapture = reject;
      var mode = root.GT4.devControls.capture;
      if (mode === "hold") {
        return; // waits until cancel_capture
      }
      setTimeout(function () {
        if (pendingCapture === reject) {
          pendingCapture = null;
          resolve({ control: "", source: mode === "conflict" ? "xinput:LB" : "xinput:RB" });
        }
      }, mode === "conflict" ? 200 : 2000);
    });
  }

  // One library per game: Spec II starts empty (saves are never shared or converted).
  var libraries = {
    gt4: {
      saves: [
        { id: "01", name: "Save 01", modified: "2026-10-16T06:31:00", active: true },
        { id: "02", name: "Save 02", modified: "2026-10-12T22:14:00", active: false },
        { id: "03", name: "Save 03", modified: "2026-09-03T19:02:00", active: false }
      ],
      replays: [
        { id: "01", name: "Replay 01", modified: "2026-10-16T21:40:00" },
        { id: "02", name: "Replay 02", modified: "2026-10-16T20:55:00" },
        { id: "03", name: "Replay 03", modified: "2026-10-12T22:31:00" }
      ]
    },
    spec2: { saves: [], replays: [] }
  };
  function libraryOf(id) { return libraries[id || selectedGame]; }
  function libraryView(kind) {
    var game = gameById(selectedGame);
    return { items: JSON.parse(JSON.stringify(libraryOf()[kind])), game: game.id, gameName: game.name, status: "",
      folder: "C:\\Users\\Player\\AppData\\Roaming\\GT4Recomp\\games\\" + game.id + (kind === "saves" ? "\\Saves" : "\\Replays") };
  }

  function appState() {
    var view = gamesView();
    var snapshot = JSON.parse(JSON.stringify(setup));
    // Without a ?setup= override the setup section describes the selected game (installed: done; else: needed).
    if (setupStart === "done" && snapshot.state === "done" && !gameById(selectedGame).installed) {
      snapshot.state = "needed";
      snapshot.disc_name = "";
    }
    var target = setupGame || (view.any_installed ? selectedGame : null);
    snapshot.game = target ? { id: target, name: gameById(target).name } : null;
    return { setup: snapshot, game_running: gameRunning, selected_game: view.selected,
      any_installed: view.any_installed, games: view.games,
      game_files: "C:\\Program Files\\GT4Recomp\\data\\games\\" + selectedGame + "\\disc",
      migration: { running: false, item: "", bytes_done: 0, bytes_total: 0, notice: "", problem: false } };
  }

  function invoke(command, args) {
    args = args || {};
    switch (command) {
      case "app_state":
        return appState();
      case "setup_start":
        running("copying", 0);
        startSetup();
        return null;
      case "setup_choose_disc": {
        if (query.get("pick") === "spec") {
          setup.state = "wrong_disc";
          setup.disc_name = "Gran Turismo 4 (USA) (Beta).iso";
          setup.problem = SPEC_MESSAGE;
          throw new Error(SPEC_MESSAGE);
        }
        // The disc decides the game; "pick=other" is always the one that was not asked for, "pick=spec2" is Spec II.
        var asked = args.game || null;
        var pick = query.get("pick");
        var discGame = pick === "spec2" ? "spec2" : (pick === "other" && asked ? (asked === "gt4" ? "spec2" : "gt4") : (asked || "gt4"));
        setupGame = discGame;
        setup.disc_name = discGame === "spec2" ? "Gran Turismo 4 Spec II v1.10.iso" : "Gran Turismo 4 (USA) (v2.00).iso";
        setup.problem = "";
        return { path: "D:\\Games\\" + setup.disc_name, serial: discGame === "spec2" ? "SCUS-97436" : "SCUS-97328",
          version: discGame === "spec2" ? "1.10" : "2.00", name: gameById(discGame).disc_name, game: discGame,
          gameName: gameById(discGame).name, mismatch: !!asked && asked !== discGame, requested: asked };
      }
      case "games_list":
        return gamesView();
      case "select_game":
        if (!gameById(args.id)) {
          throw new Error(args.id + " is not a game of this launcher.");
        }
        if (gameRunning) {
          throw new Error("Stop the game before choosing another one.");
        }
        selectedGame = args.id;
        return gamesView();
      case "games_remove":
        if (!gameById(args.id)) {
          throw new Error(args.id + " is not a game of this launcher.");
        }
        gameById(args.id).installed = false;
        return gamesView();
      case "setup_cancel":
        clearInterval(setupTimer);
        setup.state = "failed";
        setup.problem = "Setup was cancelled. Nothing was changed.";
        return null;
      case "play":
        if (!gameById(selectedGame).installed) {
          throw new Error("Set up the game first.");
        }
        gameRunning = true;
        return null;
      case "stop_game":
        gameRunning = false;
        return null;
      case "get_settings_table":
        return tableView();
      case "set_setting": {
        var row = findRow(args.id) || analog.filter(function (item) { return item.id === args.id; })[0];
        if (!row) {
          throw new Error("Unknown setting " + args.id);
        }
        row.current = args.value;
        if (args.id === "monitor") {
          // As the launcher: another monitor's Resolution goes back to its desktop mode.
          findRow("resolution").current = "desktop";
        }
        // The real set_setting returns nothing; the UI reads the table again.
        return null;
      }
      case "reset_gs_settings_to_defaults":
        rows.forEach(function (row) {
          if (row.page === "basic" || row.page === "advanced") {
            row.current = row.default;
          }
        });
        return tableView();
      case "pad_status":
        return padStatusPayload();
      case "get_controls":
        return controlsView();
      case "capture_binding":
        return captureBinding();
      case "cancel_capture":
        if (pendingCapture) {
          var rejectCapture = pendingCapture;
          pendingCapture = null;
          rejectCapture(new Error("capture cancelled"));
        }
        return null;
      case "assign_binding":
        return assignBinding(args.control, args.source, !!args.swap);
      case "reset_binding":
        bindings[args.control] = DEFAULT_SOURCES[args.control].slice();
        return null;
      case "reset_all_bindings":
        bindings = JSON.parse(JSON.stringify(DEFAULT_SOURCES));
        analog = analogDefaults();
        return controlsView();
      case "saves_list":
        return libraryView("saves");
      case "saves_import":
      case "replays_import":
        // Nothing is guessed: the page names the game the file belongs to.
        if (!args.game) {
          throw new Error("Choose which game these saves belong to.");
        }
        return libraryView(command === "saves_import" ? "saves" : "replays");
      case "saves_activate":
        libraryOf().saves.forEach(function (save) {
          save.active = save.id === args.id;
        });
        return libraryView("saves");
      case "saves_delete":
        libraryOf().saves = libraryOf().saves.filter(function (save) {
          return save.id !== args.id;
        });
        return libraryView("saves");
      case "replays_list":
        return libraryView("replays");
      case "replays_delete":
        libraryOf().replays = libraryOf().replays.filter(function (replay) {
          return replay.id !== args.id;
        });
        return libraryView("replays");
      case "release_notes":
        // 1.3.0 is sample text; the older entries are copies of docs/release-notes/launcher/<version>.md (the real build
        // embeds every short notes file up to its own version, newest first).
        return {
          version: "1.3.0",
          releases: [
            { version: "1.3.0", markdown: "# GT4Recomp 1.3.0\n\n**Gran Turismo 4 Spec II comes to GT4Recomp.**\n\n## New\n- **Spec II 1.10** runs next to the normal game, with upscaling and texture packs.\n- Pick the version under **Play**: Normal or Spec II. Each keeps its own saves.\n- A **What's new** panel on the home screen, with every past update.\n- A portable version: carry the whole folder to another PC.\n\n## Good to know\n- Spec II needs your own Spec II 1.10 disc image (the Brazilian Portuguese edition works too).\n- <b>raw HTML</b> stays plain text." },
            { version: "1.2.3", markdown: "# GT4Recomp 1.2.3\n\n**Quick fixes and a desktop shortcut.**\n\n## Fixes\n- The game no longer crashes at start for players who were not on GSdx.\n\n## New\n- Texture packs can go straight into the Textures folder, with no extra subfolder.\n- The installer can create a **Gran Turismo 4** shortcut on your desktop.\n- GSdx is now the only renderer; our own renderer, CoreGS 2.0, is coming later.\n" },
            { version: "1.2.2", markdown: "# GT4Recomp 1.2.2\n\n**A lighter game on your processor.**\n\n## Performance\n- **About 15% less processor work** per frame in the heaviest races, so slower PCs keep 60 FPS more easily.\n- The picture is exactly the same as before.\n" },
            { version: "1.2.1", markdown: "# GT4Recomp 1.2.1\n\n**A cleaner install and updates that just work.**\n\n## New\n- GT4Recomp now installs in Program Files, for every user of the PC, in any folder you pick.\n- Your settings and saves live in your Windows user folder; the big game files sit next to the program.\n- Updates install silently: one confirmation, and the launcher reopens by itself.\n- **Performance Mode** (Settings > Graphics): faster on weaker PCs, by skipping a few light effects.\n- Settings now sits next to Play on the home screen.\n\n## Fixes\n- The launcher no longer opens as a blank white window on some PCs.\n" },
            { version: "1.2.0", markdown: "# GT4Recomp 1.2.0\n\n**Custom texture packs arrive.**\n\n## New\n- **HD texture packs**: drop a pack in the Textures folder (Settings > Graphics > Open textures folder) and play.\n- The renderer is now called **GSdx**, and every GSdx picture option is available in Settings.\n- The resolution follows your desktop automatically, and monitors show their real names.\n- The game window starts black instead of white.\n\n## Thanks\n- To **JulioHeroZ** for their contribution to this release.\n" },
            { version: "1.1.0", markdown: "# GT4Recomp 1.1.0\n\n**Ultrawide support: the game now fills any screen.**\n\n## New\n- **21:9, 32:9, 16:10 and more**: races fill your screen and show more to the sides, with the HUD kept undistorted.\n- Choose the monitor and resolution the game opens on.\n- The game's sharpness adjusts to your resolution automatically, with a Render scale slider for more speed.\n- All picture settings in one place: Settings > Graphics.\n\n## Fixes\n- Updating from inside the launcher works now.\n\n## Good to know\n- Menus, movies and photo mode stay 16:9.\n" },
            { version: "1.0.0", markdown: "# GT4Recomp 1.0.0\n\n**The first release: Gran Turismo 4 running natively on your PC.**\n\n## New\n- One-click installer and a launcher that sets the game up from your own disc image.\n- **60 FPS** at full game speed in every scene we tested.\n- Any controller works out of the box (Xbox, DualShock 4, DualSense, Switch Pro), with button remapping.\n- Widescreen 16:9 and progressive 480p races, always on.\n- Your saves and replays are easy to find and back up, and survive a crash.\n\n## Good to know\n- Racing wheels and ultrawide screens are not supported yet.\n- Some license demos and menu backgrounds do not play.\n" }
          ]
        };
      case "open_releases_page":
        return null;
      case "about_info":
        return {
          version: "1.3.0",
          gt4recomp: "1.3.0",
          gt4gs: "1.2.1",
          disc_path: "D:\\Games\\PS2\\Gran Turismo 4 (USA) (v2.00).iso",
          game_files: "C:\\Program Files\\GT4Recomp\\data\\games\\" + selectedGame + "\\disc",
          data_folder: "C:\\Program Files\\GT4Recomp\\data",
          saves_folder: "C:\\Users\\Player\\AppData\\Roaming\\GT4Recomp",
          game_name: gameById(selectedGame).name,
          credits: "Gran Turismo 4 Spec II is a mod of the GT4 Online Public Beta made by [AUTHORS: names to be filled in by the owner, with their permission and a link]. GT4Recomp prepares it from your own disc image and includes none of its files. The Spec II logo on the home screen is a placeholder until the authors logo and credit are added."
        };
      case "open_log_folder":
      case "open_game_files_folder":
      case "open_player_folder":
      case "open_saves_folder":
      case "open_replays_folder":
      case "open_textures_folder":
        return null;
      case "check_updates":
        // Demo: ?update=1 offers both streams; otherwise nothing is new.
        if (query.get("update") === "1") {
          return {
            offers: DEMO_OFFERS.map(function (offer) { return JSON.parse(JSON.stringify(offer)); }),
            errors: [], upToDate: false, launcher: "1.3.0", renderer: "1.2.1"
          };
        }
        return { offers: [], errors: [], upToDate: true, launcher: "1.3.0", renderer: "1.2.1" };
      case "apply_update":
        return { ok: true, message: args.stream === "launcher" ? "GT4Recomp setup started." : "gt4-gs 1.1.1 is ready." };
      case "skip_update":
      case "confirm_close":
      case "ui_ready":
        return null;
      default:
        throw new Error("Mock has no command " + command);
    }
  }

  // Events: pages listen through GT4.backend.listen; a demo offer is emitted after load with ?update=1.
  var eventHandlers = {};
  function listen(name, handler) {
    (eventHandlers[name] = eventHandlers[name] || []).push(handler);
  }
  function emit(name, payload) {
    (eventHandlers[name] || []).forEach(function (handler) { handler(payload); });
  }
  // Pad events in the shape of the Rust pad service: pad-status on change, pad-live every 50 ms while a pad is
  // connected. The source is the dev Gamepad-like object (?pad=, ?pads=, ?nopad=); the app itself never reads one.
  function devSummary() {
    var pads = root.GT4.devGamepads ? root.GT4.devGamepads() : [];
    return root.GT4.controlsModel.padSummary(pads);
  }
  function padStatusPayload() {
    var summary = devSummary();
    return { available: true, connected: summary.connected, name: summary.connected ? summary.name : "",
      kind: summary.connected ? "xbox360" : "", player_index: summary.connected ? 0 : null, count: summary.count, error: "" };
  }
  var lastPadStatus = "";
  setInterval(function () {
    if (!root.GT4.controlsModel) {
      return;
    }
    var status = padStatusPayload();
    var key = JSON.stringify(status);
    if (key !== lastPadStatus) {
      lastPadStatus = key;
      emit("pad-status", status);
    }
    var summary = devSummary();
    if (summary.connected) {
      emit("pad-live", root.GT4.controlsModel.padLive(summary.pad));
    }
  }, 50);
  root.GT4_MOCK = { invoke: invoke, listen: listen, emit: emit };
  if (query.get("update") === "1") {
    setTimeout(function () { emit("update-available", { offers: DEMO_OFFERS }); }, 900);
  }
})(window);
