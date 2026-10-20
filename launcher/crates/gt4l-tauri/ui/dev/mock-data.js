/*
 * Development mock of the Rust command layer. Loaded only by ui/dev/index.html.
 * It answers the same command names and payloads as the Tauri build, so the
 * pages render in a plain browser. Values are sample values, not game data.
 *
 * Query options (ui/dev/index.html?...):
 *   setup=done|checking|preparing|card|wrong|needed   (default done)
 *   running=1                                       (game running at start)
 *   focus=play                                      (home: focus the Play button, for screenshots)
 */
(function (root) {
  "use strict";

  var query = new URLSearchParams(root.location.search);
  var setupStart = query.get("setup") || "done";

  var setup = { state: "done", step: "checking", progress: 0, disc_name: "Gran Turismo 4 \u00b7 USA \u00b7 v2.00", problem: null };
  if (setupStart === "wrong") {
    setup.state = "wrong_disc";
    setup.disc_name = "Gran Turismo 4 Spec II";
  } else if (setupStart === "needed") {
    setup.state = "needed";
  } else if (setupStart === "checking") {
    setup.state = "checking";
    setup.progress = 0.62;
  } else if (setupStart === "preparing") {
    setup.state = "checking";
    setup.step = "preparing";
  } else if (setupStart === "card") {
    setup.state = "checking";
    setup.step = "card";
  }

  var gameRunning = query.get("running") === "1";

  // Read by pages/home.js: focuses Play on mount. Only the dev harness sets it.
  root.GT4 = root.GT4 || {};
  root.GT4.devFocus = query.get("focus") || "";
  var maxUpscale = 4;

  // Settings rows, the same shape as the Rust table.
  var DEFAULTS = {};
  var rows = [
    { id: "renderer", label: "Renderer", help: "PCSX2 GS draws the PS2 graphics chip on your GPU. CoreGS 1.1 is the launcher's own renderer, kept as a secondary option. CoreGS 2.0 is not available yet.",
      page: "basic", group: "graphics", control: "dropdown",
      values: [
        { value: "pcsx2", label: "PCSX2 GS" },
        { value: "coregs11", label: "CoreGS 1.1 (secondary)" },
        { value: "coregs20", label: "CoreGS 2.0", disabled: true, note: "Coming soon" }
      ],
      default: "pcsx2", current: "coregs11" },
    { id: "internal_resolution", label: "Internal resolution", help: "Renders the scene at a multiple of the PS2 resolution. Higher values need a faster GPU. Limited to what this GPU can do.",
      page: "basic", group: "graphics", control: "slider",
      values: { min: 1, max: 12, step: 1, unit: "\u00d7", limit: "max_upscale" },
      default: 1, current: 4 },
    { id: "fxaa", label: "FXAA", help: "Smooths edges with a fast post-process filter. Slightly softens fine detail.",
      page: "basic", group: "graphics", control: "toggle", values: null, default: false, current: true },
    { id: "scaling_filter", label: "Scaling filter", help: "How the final image is scaled to the window. FSR adds a sharpening pass.",
      page: "basic", group: "graphics", control: "dropdown",
      values: [
        { value: "automatic", label: "Automatic" },
        { value: "nearest", label: "Nearest" },
        { value: "linear", label: "Linear" },
        { value: "fsr", label: "FSR" }
      ],
      default: "automatic", current: "fsr" },
    { id: "fsr_sharpness", label: "FSR sharpness", help: "How much detail FSR adds. 50 % is the default.",
      page: "basic", group: "graphics", control: "slider",
      values: { min: 0, max: 100, step: 1, unit: "%" },
      default: 50, current: 50, visible_when: { id: "scaling_filter", equals: "fsr" } },
    { id: "cas_mode", label: "CAS mode", help: "Contrast adaptive sharpening. Off leaves the image as the game drew it.",
      page: "basic", group: "graphics", control: "dropdown",
      values: [
        { value: "off", label: "Off" },
        { value: "cas", label: "Contrast adaptive sharpening" }
      ],
      default: "off", current: "off" },
    { id: "cas_sharpness", label: "CAS sharpness", help: "Strength of the CAS sharpening pass.",
      page: "basic", group: "graphics", control: "slider",
      values: { min: 0, max: 100, step: 1, unit: "%" },
      default: 50, current: 50, visible_when: { id: "cas_mode", equals: "cas" } },
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
    if (setup.state === "checking") {
      // Advance the fake check so the progress bar moves while the page is open.
      setup.progress = Math.min(1, setup.progress + 0.05);
      if (setup.progress >= 1) {
        if (setup.step === "checking") {
          setup.step = "preparing";
          setup.progress = 0;
        } else if (setup.step === "preparing") {
          setup.step = "card";
          setup.progress = 0;
        } else {
          setup.state = "done";
        }
      }
    }
    return { setup: JSON.parse(JSON.stringify(setup)), game_running: gameRunning };
  }

  function invoke(command, args) {
    args = args || {};
    switch (command) {
      case "app_state":
        return appState();
      case "setup_start":
        setup.state = "checking";
        setup.progress = 0;
        setup.step = "checking";
        return null;
      case "setup_choose_disc":
        setup.state = "checking";
        setup.progress = 0;
        setup.step = "checking";
        return null;
      case "setup_cancel":
        setup.state = "needed";
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
        return row;
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
          disc_path: "D:\\Games\\PS2\\Gran Turismo 4 (USA) (v2.00).iso"
        };
      case "open_log_folder":
        return null;
      default:
        throw new Error("Mock has no command " + command);
    }
  }

  root.GT4_MOCK = { invoke: invoke };
})(window);
