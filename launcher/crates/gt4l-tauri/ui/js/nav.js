/*
 * Input: spatial focus movement, keyboard, Web Gamepad polling, and the
 * optional Rust "gamepad" event. Pages describe their actions through
 * GT4.hooks = {x, y, start, back, tabs(delta)}; the router resets it per page.
 *
 * Standard gamepad mapping (XInput layout in WebView2):
 *   0 A  activate            1 B  back / cancel
 *   2 X  page action 1       3 Y  page action 2
 *   4 LB previous tab        5 RB next tab
 *   9 Start: page start (Play on Home)
 *   12-15 D-pad and the left stick (deadzone 0.5): first press at once,
 *   then repeat after 420 ms and every 110 ms while held.
 */
(function (root) {
  "use strict";
  var GT4 = (root.GT4 = root.GT4 || {});

  var FOCUSABLE = [
    "button:not([disabled])",
    "a[href]",
    "input:not([disabled])",
    "select:not([disabled])",
    "[data-focusable]:not([disabled]):not([aria-disabled=\"true\"])"
  ].join(",");

  var PAD_BUTTONS = { 0: "a", 1: "b", 2: "x", 3: "y", 4: "lb", 5: "rb", 9: "start", 12: "up", 13: "down", 14: "left", 15: "right" };
  var DIRECTIONS = ["up", "down", "left", "right"];
  var STICK_DEADZONE = 0.5;
  var REPEAT_FIRST_MS = 420;
  var REPEAT_EVERY_MS = 110;

  var mode = "mouse";
  var previousPressed = {};
  var holdDirection = null;
  var holdStart = 0;
  var lastRepeat = 0;

  GT4.hooks = {};
  GT4.capturing = false;

  function setMode(next) {
    if (mode === next) {
      return;
    }
    mode = next;
    var body = document.body;
    body.classList.remove("input-mouse", "input-keys", "input-pad");
    body.classList.add("input-" + next);
  }

  // The top-most dialog owns navigation while it is open.
  function scopeRoot() {
    var dialogs = document.querySelectorAll(".modal");
    return dialogs.length ? dialogs[dialogs.length - 1] : document.getElementById("app");
  }

  function isShown(el) {
    if (el.closest("[hidden],[inert],[aria-hidden=\"true\"]")) {
      return false;
    }
    var box = el.getBoundingClientRect();
    return box.width > 0 && box.height > 0;
  }

  function focusables() {
    var scope = scopeRoot();
    if (!scope) {
      return [];
    }
    return Array.prototype.filter.call(scope.querySelectorAll(FOCUSABLE), isShown);
  }

  function focusElement(el) {
    el.focus({ preventScroll: true });
    markFocus(el);
    if (el.scrollIntoView) {
      el.scrollIntoView({ block: "nearest", inline: "nearest" });
    }
  }

  function move(direction) {
    if (GT4.capturing) {
      return;
    }
    var list = focusables();
    if (!list.length) {
      return;
    }
    var index = list.indexOf(document.activeElement);
    if (index < 0) {
      focusElement(list[0]);
      return;
    }
    var rects = list.map(function (el) {
      var box = el.getBoundingClientRect();
      return { left: box.left, top: box.top, width: box.width, height: box.height };
    });
    var next = GT4.spatial.pickNext(rects, index, direction);
    if (next >= 0) {
      focusElement(list[next]);
    }
  }

  function isAdjustable(el) {
    return !!el && (el.type === "range" || el.hasAttribute("data-adjust"));
  }

  // Left/right change the focused slider or dropdown; otherwise they move focus.
  function horizontal(delta) {
    if (GT4.capturing) {
      return;
    }
    var el = document.activeElement;
    if (isAdjustable(el)) {
      el.dispatchEvent(new CustomEvent("gt4-adjust", { detail: { delta: delta } }));
      return;
    }
    move(delta < 0 ? "left" : "right");
  }

  function activate() {
    if (GT4.capturing) {
      return;
    }
    var el = document.activeElement;
    if (el && el.tagName !== "INPUT" && typeof el.click === "function") {
      el.click();
    }
  }

  function back() {
    if (GT4.capturing) {
      if (GT4.cancelCapture) {
        GT4.cancelCapture();
      }
      return;
    }
    var dialog = GT4.ui.topDialog();
    if (dialog) {
      dialog.back();
      return;
    }
    if (GT4.hooks.back) {
      GT4.hooks.back();
    }
  }

  function blockedByOverlay() {
    return GT4.capturing || !!GT4.ui.topDialog();
  }

  function tabs(delta) {
    if (!blockedByOverlay() && GT4.hooks.tabs) {
      GT4.hooks.tabs(delta);
    }
  }

  function pageAction(name) {
    if (!blockedByOverlay() && GT4.hooks[name]) {
      GT4.hooks[name]();
    }
  }

  // One logical button press from the gamepad or the Rust event.
  function padPress(name) {
    if (GT4.capturing) {
      if (name === "b") {
        back();
      }
      return;
    }
    setMode("pad");
    switch (name) {
      case "a": activate(); break;
      case "b": back(); break;
      case "x": pageAction("x"); break;
      case "y": pageAction("y"); break;
      case "lb": tabs(-1); break;
      case "rb": tabs(1); break;
      case "start": pageAction("start"); break;
      case "up": case "down": move(name); break;
      case "left": horizontal(-1); break;
      case "right": horizontal(1); break;
      default: break;
    }
  }

  function readPad() {
    var pads = navigator.getGamepads ? navigator.getGamepads() : [];
    var pad = null;
    for (var i = 0; i < pads.length; i++) {
      if (pads[i] && pads[i].connected) {
        pad = pads[i];
        break;
      }
    }
    var pressed = {};
    if (!pad) {
      return pressed;
    }
    Object.keys(PAD_BUTTONS).forEach(function (index) {
      var button = pad.buttons[Number(index)];
      if (button && button.pressed) {
        pressed[PAD_BUTTONS[index]] = true;
      }
    });
    var x = pad.axes[0] || 0;
    var y = pad.axes[1] || 0;
    if (Math.abs(x) > STICK_DEADZONE || Math.abs(y) > STICK_DEADZONE) {
      if (Math.abs(x) >= Math.abs(y)) {
        pressed[x < 0 ? "left" : "right"] = true;
      } else {
        pressed[y < 0 ? "up" : "down"] = true;
      }
    }
    return pressed;
  }

  function frame(now) {
    var pressed = readPad();
    Object.keys(pressed).forEach(function (name) {
      if (!previousPressed[name] && DIRECTIONS.indexOf(name) < 0) {
        padPress(name);
      }
    });

    // Directions: act on the first frame, then repeat while held.
    var held = null;
    DIRECTIONS.some(function (name) {
      if (pressed[name]) {
        held = name;
        return true;
      }
      return false;
    });
    if (held !== holdDirection) {
      holdDirection = held;
      holdStart = now;
      lastRepeat = now;
      if (held) {
        padPress(held);
      }
    } else if (held && now - holdStart >= REPEAT_FIRST_MS && now - lastRepeat >= REPEAT_EVERY_MS) {
      lastRepeat = now;
      padPress(held);
    }
    previousPressed = pressed;
    root.requestAnimationFrame(frame);
  }

  document.addEventListener("keydown", function (event) {
    if (GT4.capturing) {
      if (event.key === "Escape") {
        event.preventDefault();
        back();
      }
      return;
    }
    setMode("keys");
    switch (event.key) {
      case "ArrowUp":
        event.preventDefault();
        move("up");
        break;
      case "ArrowDown":
        event.preventDefault();
        move("down");
        break;
      case "ArrowLeft":
        event.preventDefault();
        horizontal(-1);
        break;
      case "ArrowRight":
        event.preventDefault();
        horizontal(1);
        break;
      case "Escape":
        event.preventDefault();
        back();
        break;
      case "q":
      case "Q":
        tabs(-1);
        break;
      case "e":
      case "E":
        tabs(1);
        break;
      default:
        break;
    }
  });

  // Exactly one element carries the focus ring class; CSS shows it per input mode.
  // Set both by focusin (Tab key) and by focusElement (programmatic moves), since
  // focus events are not guaranteed when the window is not focused.
  function markFocus(target) {
    document.querySelectorAll(".ring-on").forEach(function (el) {
      if (el !== target) {
        el.classList.remove("ring-on");
      }
    });
    if (target && target.classList) {
      target.classList.add("ring-on");
    }
  }
  document.addEventListener("focusin", function (event) {
    markFocus(event.target);
  });

  document.addEventListener("pointerdown", function () { setMode("mouse"); }, true);
  document.addEventListener("pointermove", function () { setMode("mouse"); }, true);

  // Rust-side fallback: {button: "a" | "b" | ... | "up", pressed: true}.
  GT4.backend.listen("gamepad", function (payload) {
    if (payload && payload.pressed && typeof payload.button === "string") {
      padPress(payload.button);
    }
  });

  GT4.nav = {
    move: move,
    activate: activate,
    horizontal: horizontal,
    back: back,
    focusables: focusables,
    setMode: setMode
  };
  GT4.back = back;

  root.requestAnimationFrame(frame);
})(window);
