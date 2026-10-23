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
        // 1.3.0 is sample text; the older entries are copies of docs/release-notes/<version>.md (the real build embeds
        // every file up to its own version, newest first). Refresh them with the coordinator's mock_notes script.
        return {
          version: "1.3.0",
          releases: [
            { version: "1.3.0", markdown: [
              "# GT4Recomp 1.3.0",
              "",
              "**Gran Turismo 4 Spec II comes to GT4Recomp.** You need your own disc images.",
              "",
              "## Highlights",
              "",
              "- **Spec II 1.10** runs natively next to the normal game, on GSdx with upscaling and texture packs.",
              "- Choose the version under **Play**: Normal or Spec II. Each keeps its own saves.",
              "- The Brazilian Portuguese edition of Spec II 1.10 is recognised too.",
              "",
              "## Install or update",
              "",
              "- Update from 1.2.x: accept the update in the launcher. Your files move into the new per-game folders.",
              "- New install: run **GT4Recomp-Setup-1.3.0.exe**.",
              "",
              "## Fixes",
              "",
              "- Smoother version switching on the home screen.",
              "- `Textures` packs work the same in both games.",
              "",
              "| What | Normal | Spec II |",
              "|---|---|---|",
              "| Saves | games\\gt4 | games\\spec2 |",
              "",
              "## Known issues",
              "",
              "- Replays are kept apart per game until cross-play is tested.",
              "- <b>raw HTML</b> stays plain text."
            ].join("\n") },
            { version: "1.2.3", markdown: "# GT4Recomp 1.2.3\n\n**A hotfix release: the game no longer crashes at start for players who were not on GSdx, texture packs go straight\ninto the Textures folder, and the installer makes a desktop shortcut.** You need your own disc image of the game.\n\n## Install or update\n\n- Update from 1.2.1 or 1.2.2: accept the update in the launcher and say yes to the one administrator prompt. The\n  update is silent, and the launcher starts again by itself when it is done.\n- Update from 1.2.0 or 1.1.0: accept the update in the launcher; Windows shows the administrator prompt and the setup\n  pages once. If nothing happens after the download, close the launcher and run **GT4Recomp-Setup-1.2.3.exe**\n  yourself. The first start moves your files to the 1.2.1 layout (see the [1.2.1 notes](1.2.1.md)).\n- Update from 1.0.0: run the installer (the 1.0.0 update button does not work).\n- New install: download **GT4Recomp-Setup-1.2.3.exe** and run it.\n\n## Fixes\n\n- **The game crashed at start with CoreGS 1.1** in 1.2.2 (chosen in Settings, or used when GSdx could not start).\n  The cause is fixed in the game, and the launcher no longer uses CoreGS 1.1 at all (below).\n\n## Changes\n\n- **GSdx is the only renderer.** CoreGS 1.1 is removed from the launcher; Settings shows GSdx and **CoreGS 2.0\n  (coming soon)**, which is our own renderer in development. If you had chosen CoreGS 1.1, the launcher switches you\n  to GSdx. If GSdx cannot be installed or stops, Play shows a clear message with what to do and where the logs are,\n  instead of falling back to another renderer. The settings that only CoreGS 1.1 used are gone.\n- **Custom textures straight in the Textures folder.** GSdx 1.2.1 (installed by the launcher automatically) reads\n  texture packs from `data\\Textures` itself, at any folder depth: paste the pack folder straight into `Textures`\n  (Settings > Graphics > **Open textures folder**). The launcher no longer creates a `replacements` folder, and moves\n  the contents of an existing `Textures\\replacements` up into `Textures` once (nothing is overwritten: a name that\n  already exists stays where it is).\n- **Desktop shortcut.** The installer's last page has **Create a desktop shortcut**, ticked by default; it makes\n  **Gran Turismo 4** on the desktop of every user of the PC. Updates keep your choice: a shortcut you asked for is\n  made again if it is missing, and one you declined is not created. Installs from before 1.2.3 get the shortcut at\n  this update.\n\n## Unchanged\n\n- The game is the 1.2.2 game (same picture and performance) with the crash fix.\n- Settings, controls, saves and replays are kept.\n\n## Known issues\n\n- Every known issue of 1.2.2 is still open.\n" },
            { version: "1.2.2", markdown: "# GT4Recomp 1.2.2\n\n**A lighter game on the CPU, with the same picture.** You need your own disc image of the game.\n\n## Install or update\n\n- Update from 1.2.1: accept the update in the launcher and say yes to the one administrator prompt. The update is\n  silent, and the launcher starts again by itself when it is done.\n- Update from 1.2.0 or 1.1.0: accept the update in the launcher. Windows shows the administrator prompt; say yes. If\n  nothing happens after the download, close the launcher and run **GT4Recomp-Setup-1.2.2.exe** yourself. The first\n  start moves your files to the 1.2.1 layout (see the [1.2.1 notes](1.2.1.md)).\n- Update from 1.0.0: run the installer (the 1.0.0 update button does not work).\n- New install: download **GT4Recomp-Setup-1.2.2.exe** and run it.\n\n## Performance\n\nThe game spends less time per frame on the CPU. The work sent to the vector unit (VU1) and to the graphics process is\nbatched instead of handed over piece by piece, so the threads wait for each other far less:\n\n- VU1 jobs per frame dropped from thousands to a few dozen (36 in the showroom, 59 on the Nordschleife, 80 in a night\n  race with five cars).\n- Night race, the heaviest scene: the game's CPU work per frame went from 11.8 to 10.0 ms out of the 16.7 ms a frame\n  lasts at 60 FPS, so slower CPUs have about 2 ms more headroom.\n- Owner test on a Ryzen 7 5700X: 60 FPS with no late frame and no audio dropout in the showroom, a Nordschleife time\n  trial and a night race.\n\nThe picture is unchanged: menus, a day race and a night race give frames identical to 1.2.0, bit for bit.\n\n## Unchanged\n\n- The launcher is the 1.2.1 launcher, with only the version number changed.\n- GSdx (gt4-gs) stays at 1.2.0.\n- Settings, controls, saves and replays are kept.\n\n## Known issues\n\n- Every known issue of 1.2.1 is still open.\n- In a night race, the flares still make the game wait for the graphics process (about 1.3 ms per frame). Settings >\n  Graphics > Performance Mode skips them on weaker PCs.\n" },
            { version: "1.2.1", markdown: "# GT4Recomp 1.2.1\n\n**A per-machine install, with the game data beside the program and your saves in AppData.** You need your own disc\nimage of the game.\n\n## Install or update\n\n- New install: download **GT4Recomp-Setup-1.2.1.exe** and run it. Windows asks for administrator rights: GT4Recomp now\n  installs for every user of the PC, by default in `C:\\Program Files\\GT4Recomp`. You can pick any other folder or drive\n  on the folder page.\n- Update from 1.2.0 or 1.1.0: accept the update in the launcher. Windows shows the administrator prompt; say yes.\n  This update still shows the setup pages once. If nothing happens after the download, close the launcher and run\n  **GT4Recomp-Setup-1.2.1.exe** yourself.\n- Update from 1.0.0: run the installer (the 1.0.0 update button does not work).\n- Updates from 1.2.1 on are silent: accept the update in the launcher, say yes to the one administrator prompt, and\n  the launcher starts again by itself when the update is done.\n- An update waits for the game to close (up to 30 seconds, then a Retry prompt) and never leaves a half-updated install: the new\n  program is copied first and swapped in, and the previous files come back if a step fails.\n- Reinstall into another folder or drive: run the setup and pick the folder. The old program files are removed (its\n  `data` folder stays), and the launcher moves the old large data (disc copy, prepared game files, gs, runtime card,\n  sessions, logs, textures) into the new folder's `data` at its first start, with the same checks as the 1.2.0 move.\n  Your settings and saves are in AppData and are not affected.\n- The setup removes the old per-user program files (`%LOCALAPPDATA%\\Programs\\GT4Recomp`), their Start-menu shortcuts\n  and their uninstall entry. It never touches your data.\n\n## Where your files are now\n\n| What | 1.2.0 | 1.2.1 |\n|---|---|---|\n| Program | `%LOCALAPPDATA%\\Programs\\GT4Recomp` | `C:\\Program Files\\GT4Recomp` (or the folder you chose) |\n| Disc image copy, prepared game files | `%LOCALAPPDATA%\\GT4Recomp\\disc`, `\\prepared` | `<install>\\data\\disc`, `\\prepared` |\n| GSdx (gt4-gs) versions and caches | `%LOCALAPPDATA%\\GT4Recomp\\gs` | `<install>\\data\\gs` |\n| Runtime card, sessions, logs | `%LOCALAPPDATA%\\GT4Recomp\\runtime`, `\\sessions`, `\\logs` | `<install>\\data\\...` (1.2.0 logs in `data\\logs\\from-1.2.0`) |\n| Custom textures | `Documents\\Gran Turismo 4\\Textures` | `<install>\\data\\Textures` |\n| Settings | `%LOCALAPPDATA%\\GT4Recomp\\config` | `%APPDATA%\\GT4Recomp\\config` |\n| Controls | `%LOCALAPPDATA%\\GT4Recomp\\controls.toml` | `%APPDATA%\\GT4Recomp\\controls.toml` |\n| Saves and replays | `Documents\\Gran Turismo 4\\Saves`, `\\Replays` | `%APPDATA%\\GT4Recomp\\Saves`, `\\Replays` |\n| Card backups | `%LOCALAPPDATA%\\GT4Recomp\\library\\backups` | `%APPDATA%\\GT4Recomp\\backups` |\n\nThe launcher moves everything at its first start, running as you:\n\n- Your settings, controls, saves and replays are **copied** and checked byte for byte. Then\n  `Documents\\Gran Turismo 4` is renamed to **`Gran Turismo 4 (moved to AppData)`**, with a `README.txt` that says where\n  the files went. Nothing is deleted: once your saves show in the launcher, you may delete that folder yourself.\n- The game files (about 5 GB) are **moved**. On the same drive this is instant. To another drive they are copied with a\n  progress line in the launcher window, checked (size and SHA-256, and the disc image against its known hash), and only\n  then removed from the old place. Play waits until the move has finished.\n- If something cannot be moved (a file in use, OneDrive files that are only online, a full disk), the old place stays in\n  use and the launcher tries again at the next start. Nothing is overwritten: if the new place already has a file, both\n  are kept (a save goes to the next free slot).\n- Nothing moves while the game runs.\n\n## Portable\n\nThe whole `GT4Recomp` folder can be copied to another drive or another PC and started from there. The launcher finds\nits data in the `data` folder next to `GT4Recomp.exe`; on a new PC it starts with default settings and uses the disc\ncopy and GSdx it finds there. A copy placed in a folder you cannot write (such as Program Files on another PC, without\nthe installer) still works: the launcher reads its data and writes new files to `%LOCALAPPDATA%\\GT4Recomp`.\n\n## Uninstall\n\nUninstalling removes the program folder **including its data folder** (the disc copy can be made again from your own\nimage). Your settings, saves and replays in `%APPDATA%\\GT4Recomp` are kept, unless you tick the box, which deletes them\nonly for the Windows user running the uninstall.\n\n## Fixes\n\n- The launcher window no longer stays blank (issue #80, a GTX 750 Ti on Windows 10). The window is dark while it\n  starts and shows once its first screen has drawn. If the screen does not draw within 12 seconds, the launcher\n  restarts itself with GPU acceleration off in its web view (the safe mode) and says so in the log. If it still does\n  not draw, a clear message offers the WebView2 download page or the log folder. A WebView2 runtime that is missing\n  is detected at start, with a link to install it, and the same kind of message shows at once.\n- The launcher reopens faster after an update: the setup starts it before it removes the old copies, and it no\n  longer rewrites the permissions of the data folder on each update.\n- Settings > Graphics > Advanced has **Launcher GPU acceleration**. It is Off while safe mode is on; turning it On\n  takes effect at the next start of the launcher.\n\n## Settings\n\n- **Home screen:** Settings now sits beside Play, in the same box and with the same selection, and the logo is centred\n  above both.\n\n- **Performance Mode** (Settings > Graphics, off by default): faster on weaker PCs. Some effects that read the image\n  back are skipped, such as headlight and sun flares and the car icons next to the gap time. It applies the next time\n  you play. It needs GSdx: with CoreGS 1.1 the row is shown disabled (\"GSdx only\"). It replaces the Readbacks row of\n  Advanced; a stored Readbacks choice other than Accurate turns Performance Mode on, and your other settings are kept.\n\n## Known issues\n\n- Every known issue of 1.2.0 is still open (the game is unchanged: same game build and the same gt4-gs 1.2.0).\n- The update from 1.2.0 hands the new setup to Windows Explorer, which shows the administrator prompt. If the prompt is\n  declined or does not appear, run the setup yourself.\n- When a different administrator account approves the setup, the old per-user files of the player are removed by the\n  launcher at its first start instead of by the setup.\n- An update started through Windows Explorer (the fallback when the elevated start fails) shows the setup pages.\n" },
            { version: "1.2.0", markdown: "# GT4Recomp 1.2.0\n\n**Custom textures, the GSdx renderer choice, and a desktop-following resolution.** You need your own disc image of the game.\n\n## Install or update\n\n- New install: download **GT4Recomp-Setup-1.2.0.exe** and run it (per-user, no administrator rights). Setup asks which\n  graphics renderer to use: **GSdx** (recommended) or **CoreGS 1.1** (built in, no download).\n- Update from 1.1.0: accept the update in the launcher, or run the new installer over the old one (from 1.0.0, run the\n  installer: the 1.0.0 update button does not work). Your settings are kept; a\n  copy of the old settings file is made first.\n- The launcher needs **gt4-gs 1.2.0** for GSdx. An older gt4-gs is replaced at the next start.\n\n## What's new\n\n- **GSdx renderer choice.** The renderer formerly called \"PCSX2 GS\" is named **GSdx** everywhere in the launcher. Setup\n  asks once which renderer to use; GSdx is preselected and recommended, CoreGS 1.1 needs no download.\n- **Switch to GSdx any time.** Choosing GSdx in Settings > Graphics > Renderer installs it at once when it is missing.\n  The progress shows under the Renderer row (\"Downloading GSdx…\", \"Installing…\"), then \"GSdx installed successfully\".\n  If the install fails, a clear message says why and CoreGS 1.1 stays selected; the game keeps working.\n- **Custom textures.** Put a texture pack's `replacements` folder into `Documents\\Gran Turismo 4\\Textures`. Settings >\n  Graphics > **Load custom textures** is on by default; with an empty folder nothing changes. **Open textures folder**\n  is next to it. **Precache custom textures** (Advanced, off by default) loads the whole pack at start. The pack is only\n  read; texture dumps are never written.\n- **Monitor names.** The Monitor row shows the monitor's name (\"DELL U3419W\"). Two monitors with the same name are told\n  apart as \"DELL U3419W\" and \"DELL U3419W 2\".\n- **Resolution follows the desktop.** The default Resolution is the monitor's desktop resolution, read again at every\n  start and at Play. If you change the desktop resolution (for example with AMD VSR), the game follows it. A size you\n  picked yourself stays, unless the monitor no longer has that mode; then the game plays at the desktop mode and the\n  log says so.\n- **Every gt4-gs option is available.** Blending accuracy (minimum to maximum), deinterlacing, the Vulkan, D3D11 and D3D12\n  renderers, hardware readbacks, CAS sharpening, texture filtering, anisotropic filtering, trilinear filtering, dithering,\n  texture preloading, shade boost, TV shaders, FMV aspect, anti-aliasing and more; nothing is marked \"Coming soon\" any\n  more. Left out on purpose: the PCSX2 on-screen display positions (GT4 has its own overlay), the GPU name, and\n  diagnostics knobs.\n- **2-row crop at the bottom** of the picture (gt4-gs 1.2.0; the right stays 1).\n- **Black start.** The window is black from its first frame (it was white before the Sony logo).\n\n## Thanks\n\nThanks to **JulioHeroZ** (pull request #72): the change that keeps CoreGS 1.1 independent of the GSdx download (setup,\nthe start-up repair and the renderer release check) is theirs, and is kept with their authorship.\n\n## Known issues\n\n- Texture packs are not checked against every texture in the game; a pack with another file naming is ignored.\n- The D3D11 and D3D12 renderers start on this build but have not been checked on screen; Vulkan stays the default.\n- Split screen and photo mode have had little testing at ultrawide ratios (they stay 16:9, pillarboxed).\n- Menus are still interlaced at 480i.\n- A Resolution saved by 1.1.0 next to the Monitor \"primary\" cannot be recognised as \"desktop\" (no desktop size is\n  recorded there); choose Desktop once in Settings to fix it.\n" },
            { version: "1.1.0", markdown: "# GT4Recomp 1.1.0\n\n**Ultrawide and any aspect ratio.** Gran Turismo 4 now fills your screen at 21:9, 32:9, 16:10 and other shapes, taken automatically from the resolution you choose. You need your own disc image of the game.\n\n## Install or update\n\n- New install: download **GT4Recomp-Setup-1.1.0.exe** and run it (per-user, no administrator rights). On the first start the launcher asks for your disc image, copies and checks it, and downloads the PCSX2 GS renderer (gt4-gs 1.1.0).\n- Update from 1.0.0: download and run the new installer over the old one. The update button of launcher 1.0.0 does not work (it closes without installing; fixed in 1.1.0, so later updates install from the launcher). Your settings, saves, replays and the copy of your disc image are kept; settings are moved to the new layout automatically (a backup is made first).\n- The launcher now needs **gt4-gs 1.1.0**. If an older gt4-gs is installed, the launcher updates it at the next start (until then, the old one keeps working without the ultrawide fill).\n\n## What's new\n\n- **Automatic ultrawide and any aspect ratio.** The shape of the picture follows the output resolution; there is no aspect setting to get wrong.\n  - Races fill the screen: wider screens see more to the sides (Hor+), taller screens see more above and below (Vert+). At 16:9 the picture is exactly as before.\n  - The race HUD (dials, lap times, map, mirror) stays the game's 16:9 layout, undistorted and centred.\n  - Menus, movies and photo mode stay 16:9, pillarboxed.\n- **Monitor and Resolution.** Choose the monitor the game opens on (the primary one by default) and one of its real resolutions (the desktop resolution by default). Fullscreen stays borderless on that monitor.\n- **Automatic internal resolution.** The game is drawn at the smallest whole multiple of the PS2 picture that covers your resolution (for example 3x at 1920x1080, 4x at 2560x1440, 6x at 3440x1440 and 3840x2160).\n- **Render scale 50-200%.** Trade sharpness for speed in 25% steps; the row shows the result, for example \"100% · 3x · 1920x1344\".\n- **gt4-gs 1.1.0**: the PCSX2 GS renderer can stretch the race view to the screen, and it always crops the 1-pixel line GT4 leaves on the right and bottom edges.\n- **Settings > Graphics** now holds everything about the picture, in three sections: **Display** (Monitor, Resolution, Fullscreen, VSync), **Graphics** (Renderer, Render scale, anti-aliasing, sharpening) and **Advanced**. The separate Display tab is gone.\n\n## Fixes\n\n- **Updates from the launcher.** Accepting an update downloaded the setup, closed the launcher and installed nothing: Windows ended the setup together with the launcher. The setup now runs on its own, and the launcher refuses to update while the game is running.\n\n## Known issues\n\n- Split screen and photo mode have had little testing at ultrawide ratios (they stay 16:9, pillarboxed).\n- Menus are still interlaced at 480i; progressive menus come in a later update.\n- Some license demos and menu backgrounds do not play (the same as PCSX2 with default settings).\n- Racing wheels are not supported yet.\n- The installer and the launcher are not code-signed, so Windows SmartScreen may warn on the first start (\"More info\", then \"Run anyway\").\n- Most testing was on an AMD GPU; please report problems with the launcher log (Settings > About > Open log folder).\n\n## Requirements\n\nUnchanged from 1.0.0: Windows 10 or 11 64-bit, a processor with SSE4.1, a graphics card with Vulkan 1.2, your own Gran Turismo 4 USA v2.00 (SCUS-97328) disc image as a plain .iso, and about 6 GB free.\n\n## Legal\n\nGT4Recomp is an unofficial fan project, not affiliated with or endorsed by Sony Interactive Entertainment or Polyphony Digital. It contains no game files: you bring your own disc. Our code is MIT. The PCSX2 GS renderer (GPL-3.0-or-later) is a separate program, gt4-gs.exe, with its source published at github.com/moesuito/gt4recomp-gs.\n" },
            { version: "1.0.0", markdown: "# GT4Recomp 1.0.0\n\nThe first stable release: **Gran Turismo 4 (USA, SCUS-97328 v2.00) recompiled to run natively on Windows**, with a launcher and a one-click installer. You need your own disc image of the game.\n\n## Install\n\n- Download **GT4Recomp-Setup-1.0.0.exe** and run it. It installs for your Windows user only (no administrator rights) into %LOCALAPPDATA%\\Programs\\GT4Recomp.\n- If the Microsoft Edge WebView2 Runtime is missing (rare on Windows 10, built into Windows 11), the installer downloads it from Microsoft.\n- Start **Gran Turismo 4** from the Start menu. On the first start the launcher asks for your disc image (.iso), copies it, checks it and downloads the PCSX2 GS renderer (gt4-gs 1.0.1). Then press **Play**.\n\n## Requirements\n\n- Windows 10 or 11, 64-bit.\n- A processor with **SSE4.1** (Intel Core i3/i5/i7 / AMD FX or newer). The launcher checks it.\n- A graphics card with **Vulkan 1.2**. Older drivers work too (the game no longer needs 2025-era Vulkan extensions); the launcher checks Vulkan and names the driver update when it is missing.\n- Your own disc image of **Gran Turismo 4 USA v2.00 (SCUS-97328)** as a plain .iso.\n- About **6 GB** free: 5.3 GB for the copy of your disc image plus the program.\n\n## What's new\n\n- **Launcher**: setup from your disc image (copy, check, game files), Play, settings (graphics with an Advanced section, display), a save and replay library with import, controller remapping, a tray icon while you play, and update checks that never install anything without asking.\n- **Renderers**: the **PCSX2 GS** in its own process (gt4-gs 1.0.1, downloaded by the launcher, internal resolution from 480p to 8K, FXAA) and **CoreGS 1.1** built in as the fallback when the PCSX2 GS is missing or fails.\n- **Controllers through SDL3**: Xbox, DualShock 4, DualSense, Switch Pro and other pads work out of the box, mapped to the DualShock 2 layout with analog L2/R2 pressure. Remap them in the launcher.\n- **Keyboard layout**: W or Enter accelerate and confirm, S brake, Space handbrake, A/D and the arrows steer, Backspace or Esc back, Shift nitro, R and F gear up and down, Q look back, C or Tab camera, P Start. Alt+Enter or F11 switch fullscreen.\n- **16:9 and 480p, fixed**: the game always runs in widescreen 16:9 and races always run in 480p progressive. There is no setting for either (ultrawide comes in a later update).\n- **License tests are playable**. Some license demos and menu backgrounds are skipped, as in PCSX2 with default settings.\n- **RivaTuner / MSI Afterburner overlay** works with the PCSX2 GS (Arbitrary Code Guard is off by default; it can be turned on in Settings > Graphics > Advanced). The PCSX2 GS's own on-screen display is always off; GT4Recomp's performance overlay is in the same section.\n- **Crash-safe memory card**: saves survive a crash or a forced close.\n- **Your saves and replays in Documents\\Gran Turismo 4** (`Saves`, `Replays`): easy to find and back up, and always kept by the uninstaller. Settings and the copy of your disc image stay in %LOCALAPPDATA%\\GT4Recomp. A save library from an earlier test build moves there at the first start (copied and checked; the old folder is renamed, never deleted).\n- **60 FPS at 100% game speed** in every scene we test (Ryzen 7 5700X, Radeon RX 9060 XT).\n\n## Known issues\n\n- Some license demos and menu backgrounds do not play (the same as PCSX2 with default settings).\n- Racing wheels are not supported yet.\n- Ultrawide and other screen ratios are not supported yet: the game shows 16:9 (planned after 1.0.0).\n- The installer and the launcher are not code-signed, so Windows SmartScreen may warn on the first start (\"More info\", then \"Run anyway\").\n- Long sessions and GPUs other than AMD have had less testing; please report problems with the launcher log (Settings > About > Open log folder).\n\n## Legal\n\nGT4Recomp is an unofficial fan project, not affiliated with or endorsed by Sony Interactive Entertainment or Polyphony Digital. It contains no game files: you bring your own disc. Our code is MIT. The PCSX2 GS renderer (GPL-3.0-or-later) is a separate program, gt4-gs.exe, with its source published at github.com/moesuito/gt4recomp-gs.\n" }
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
