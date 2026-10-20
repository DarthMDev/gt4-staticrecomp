/*
 * Development mock of the Rust command layer. Loaded only by ui/dev/index.html.
 * It answers the same command names and payloads as the Tauri build, so the
 * pages render in a plain browser. Values are sample values, not game data.
 *
 * Query options (ui/dev/index.html?...):
 *   setup=done|needed|wrong|copying|checking|preparing|renderer|failed|missing|offline   (default done)
 *   pick=spec                                       (setup: the picker returns a Spec II image, refused)
 *   running=1                                       (game running at start)
 *   focus=play                                      (home: focus the Play button, for screenshots)
 */
(function (root) {
  "use strict";

  var query = new URLSearchParams(root.location.search);
  var setupStart = query.get("setup") || "done";
  var IMAGE_BYTES = 5314478080;
  var SPEC_MESSAGE = "This disc is not supported yet. GT4Recomp supports Gran Turismo 4 (USA, SCUS-97328). " +
    "This looks like Gran Turismo 4 Spec II or GT4 Online (SCUS-97436).";

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
    setup.note = "PCSX2 GS could not be downloaded, so CoreGS 1.1 will be used. GT4Recomp tries again at the next start.";
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
    { id: "renderer", label: "Renderer", help: "PCSX2 GS (the default) draws the game on your GPU in its own process. CoreGS 1.1 is the launcher's built-in renderer, a secondary choice. CoreGS 2.0 is in development.",
      page: "basic", group: "graphics", control: "dropdown",
      values: [
        { value: "pcsx2", label: "PCSX2 GS" },
        { value: "coregs-1.1", label: "CoreGS 1.1 (secondary)" },
        { value: "coregs-2.0", label: "CoreGS 2.0 (coming soon)", disabled: true }
      ],
      default: "pcsx2", current: "pcsx2" },
    { id: "upscale", label: "Internal resolution", help: "The resolution the game is drawn at before it is shown. 480p is the PS2 size; higher presets look sharper and need a faster GPU.",
      page: "basic", group: "graphics", control: "dropdown",
      values: [
        { value: "480p", label: "480p (1x)" },
        { value: "720p", label: "720p (2x)" },
        { value: "1080p", label: "1080p (3x)" },
        { value: "1440p", label: "1440p (4x)" },
        { value: "4K", label: "4K (6x)" },
        { value: "8K", label: "8K (12x)" }
      ],
      default: "1080p", current: "1080p" },
    { id: "fxaa", label: "Anti-aliasing (FXAA)", help: "Smooths jagged edges with a fast filter. It softens fine detail a little. Off by default.",
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
    { id: "debug_internal", label: "Internal debug", help: "Not shown to users.",
      page: "hidden", control: "toggle", values: null, default: false, current: true }
  ];
  rows.forEach(function (row) {
    DEFAULTS[row.id] = row.default;
  });

  function tableView() {
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
    { stream: "renderer", installed: "1.0.1", latest: "1.0.2", update: true,
      package: "gt4-gs-1.0.2-windows-x64.zip",
      notes: "## gt4-gs 1.0.2\n\n- **Faster** present on VSync Off.\n- Fixes a crash on exit when the game ends first.\n- Sample text for the launcher preview."},
    { stream: "launcher", installed: "1.0.0", latest: "1.0.1", update: true,
      package: "GT4Recomp-Setup-1.0.1.exe",
      notes: "GT4Recomp 1.0.1\n\n* Tray: the launcher comes back when the game ends.\n* Sample text for the launcher preview." }
  ];
  var controls = [
    { id: "l1", name: "L1", binding: "Pad: LB" },
    { id: "l2", name: "L2", binding: "Pad: LT" },
    { id: "r1", name: "R1", binding: "Pad: RB" },
    { id: "r2", name: "R2", binding: "Pad: RT" },
    { id: "up", name: "D-pad up", binding: "Pad: D-pad up" },
    { id: "down", name: "D-pad down", binding: "Pad: D-pad down" },
    { id: "left", name: "D-pad left", binding: "Pad: D-pad left" },
    { id: "right", name: "D-pad right", binding: "Pad: D-pad right" },
    { id: "triangle", name: "Triangle", binding: "Pad: Y" },
    { id: "circle", name: "Circle", binding: "Pad: B" },
    { id: "cross", name: "Cross", binding: "Pad: A" },
    { id: "square", name: "Square", binding: "Pad: X" },
    { id: "select", name: "SELECT", binding: "Pad: Back" },
    { id: "start", name: "START", binding: "Pad: Start" },
    { id: "analog", name: "ANALOG", binding: "" },
    { id: "l3", name: "L3 (left stick)", binding: "Pad: LS press" },
    { id: "r3", name: "R3 (right stick)", binding: "" }
  ];

  var triggers = [
    { id: "l2_deadzone", label: "L2 (from LT) deadzone", help: "Ignores the first part of the trigger travel.", control: "slider", values: { min: 0, max: 50, step: 1, unit: "%" }, default: 10, current: 10, page: "hidden" },
    { id: "l2_curve", label: "L2 (from LT) curve", help: "0 is linear; higher values need a harder press for the same output.", control: "slider", values: { min: 0, max: 100, step: 1, unit: "%" }, default: 0, current: 0, page: "hidden" },
    { id: "r2_deadzone", label: "R2 (from RT) deadzone", help: "Ignores the first part of the trigger travel.", control: "slider", values: { min: 0, max: 50, step: 1, unit: "%" }, default: 10, current: 10, page: "hidden" },
    { id: "r2_curve", label: "R2 (from RT) curve", help: "0 is linear; higher values need a harder press for the same output.", control: "slider", values: { min: 0, max: 100, step: 1, unit: "%" }, default: 0, current: 0, page: "hidden" }
  ];

  var saves = [
    { id: "01", name: "Save 01", modified: "2026-10-16T06:31:00", active: true },
    { id: "02", name: "Save 02", modified: "2026-10-12T22:14:00", active: false },
    { id: "03", name: "Save 03", modified: "2026-09-03T19:02:00", active: false }
  ];
  var replays = [
    { id: "01", name: "Replay 01", modified: "2026-10-16T21:40:00" },
    { id: "02", name: "Replay 02", modified: "2026-10-16T20:55:00" },
    { id: "03", name: "Replay 03", modified: "2026-10-12T22:31:00" }
  ];

  function appState() {
    return { setup: JSON.parse(JSON.stringify(setup)), game_running: gameRunning,
      game_files: "C:\\Users\\Player\\AppData\\Local\\GT4Recomp\\disc" };
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
      case "setup_choose_disc":
        if (query.get("pick") === "spec") {
          setup.state = "wrong_disc";
          setup.disc_name = "Gran Turismo 4 Spec II v1.10.iso";
          setup.problem = SPEC_MESSAGE;
          throw new Error(SPEC_MESSAGE);
        }
        setup.disc_name = "Gran Turismo 4 (USA) (v2.00).iso";
        setup.problem = "";
        return { path: "D:\\Games\\Gran Turismo 4 (USA) (v2.00).iso", serial: "SCUS-97328", version: "2.00",
          name: "Gran Turismo 4 \u00b7 USA \u00b7 v2.00" };
      case "setup_cancel":
        clearInterval(setupTimer);
        setup.state = "failed";
        setup.problem = "Setup was cancelled. Nothing was changed.";
        return null;
      case "play":
        gameRunning = true;
        return null;
      case "stop_game":
        gameRunning = false;
        return null;
      case "get_settings_table":
        return tableView();
      case "set_setting": {
        var row = findRow(args.id);
        if (!row) {
          throw new Error("Unknown setting " + args.id);
        }
        row.current = args.value;
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
      case "get_controls":
        return {
          controls: JSON.parse(JSON.stringify(controls)),
          triggers: JSON.parse(JSON.stringify(triggers))
        };
      case "capture_binding":
        // Fake capture: resolves after 1.5 s with a sample binding.
        return new Promise(function (resolve) {
          setTimeout(function () {
            controls.forEach(function (control) {
              if (control.id === args.control) {
                control.binding = "Pad: Sample";
              }
            });
            resolve({ control: args.control, binding: "Pad: Sample" });
          }, 1500);
        });
      case "cancel_capture":
        return null;
      case "reset_binding":
        controls.forEach(function (control) {
          if (control.id === args.control) {
            control.binding = "Pad: default";
          }
        });
        return null;
      case "saves_list":
        return { items: JSON.parse(JSON.stringify(saves)) };
      case "saves_import":
        return null;
      case "saves_activate":
        saves.forEach(function (save) {
          save.active = save.id === args.id;
        });
        return { items: JSON.parse(JSON.stringify(saves)) };
      case "saves_delete":
        saves = saves.filter(function (save) {
          return save.id !== args.id;
        });
        return { items: JSON.parse(JSON.stringify(saves)) };
      case "replays_list":
        return { items: JSON.parse(JSON.stringify(replays)) };
      case "replays_import":
        return null;
      case "replays_delete":
        replays = replays.filter(function (replay) {
          return replay.id !== args.id;
        });
        return { items: JSON.parse(JSON.stringify(replays)) };
      case "about_info":
        return {
          version: "1.0.0",
          gt4recomp: "1.0.0",
          gt4gs: "1.0.1",
          disc_path: "D:\\Games\\PS2\\Gran Turismo 4 (USA) (v2.00).iso",
          game_files: "C:\\Users\\Player\\AppData\\Local\\GT4Recomp\\disc",
          data_folder: "C:\\Users\\Player\\AppData\\Local\\GT4Recomp"
        };
      case "open_log_folder":
      case "open_game_files_folder":
        return null;
      case "check_updates":
        // Demo: ?update=1 offers both streams; otherwise nothing is new.
        if (query.get("update") === "1") {
          return {
            offers: DEMO_OFFERS.map(function (offer) { return JSON.parse(JSON.stringify(offer)); }),
            errors: [], upToDate: false, launcher: "1.0.0", renderer: "1.0.1"
          };
        }
        return { offers: [], errors: [], upToDate: true, launcher: "1.0.0", renderer: "1.0.1" };
      case "apply_update":
        return { ok: true, message: args.stream === "launcher" ? "GT4Recomp setup started." : "gt4-gs 1.0.2 is ready." };
      case "skip_update":
      case "confirm_close":
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
  root.GT4_MOCK = { invoke: invoke, listen: listen, emit: emit };
  if (query.get("update") === "1") {
    setTimeout(function () { emit("update-available", { offers: DEMO_OFFERS }); }, 900);
  }
})(window);
