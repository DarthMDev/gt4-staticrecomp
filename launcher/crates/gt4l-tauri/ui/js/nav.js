/*
 * Input: spatial focus movement, keyboard and Web Gamepad polling. Pages describe
 * their actions through GT4.hooks = {x, y, start, back, tabs(delta), horizontal(delta)}; the router resets it per page.
 *
 * Standard gamepad mapping (XInput layout in WebView2):
 *   0 A  activate            1 B  back / cancel
 *   2 X  page action 1       3 Y  page action 2
 *   4 LB previous tab        5 RB next tab
 *   9 Start: page start (Play on Home)
 *   12-15 D-pad and the left stick (deadzone 0.5): first press at once,
 *   then repeat after 420 ms and every 110 ms while held.
 *   Right stick (axes 2 and 3): scrolls the focused scroll container.
 *
 * Face buttons on a page: X and Y run GT4.hooks.x / GT4.hooks.y when the page
 * sets them. B closes an open help tip, then the top dialog, then goes back.
 *
 * Rebind (Controls): while GT4.capturing is set only B does anything on the pad (GT4.cancelCapture), and every key
 * goes to GT4.captureKey(event) first (it returns true when it used the key).
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
  // A page may narrow the scope (Settings: the tab list or the tab's rows) with GT4.focusScope().
  function scopeRoot() {
    var dialogs = document.querySelectorAll(".modal");
    if (dialogs.length) {
      return dialogs[dialogs.length - 1];
    }
    var narrowed = GT4.focusScope ? GT4.focusScope() : null;
    return narrowed || document.getElementById("app");
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
    // A page may take the press first (Settings: D-pad right on a tab enters its content).
    if (GT4.hooks.horizontal && GT4.hooks.horizontal(delta)) {
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
    } else if (el && el.hasAttribute && el.hasAttribute("data-focusable")) {
      // SVG elements (the Controls drawing) have no click(): send the event the page listens to.
      el.dispatchEvent(new MouseEvent("click", { bubbles: true, cancelable: true }));
    }
  }

  function back() {
    if (GT4.capturing) {
      if (GT4.cancelCapture) {
        GT4.cancelCapture();
      }
      return;
    }
    if (GT4.ui.closeTips()) {
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

  // One logical button press from the gamepad.
  // Face and shoulder buttons go through GT4.input.padRoute (pure, tested in dev/nav-test.html).
  function padPress(name) {
    if (GT4.capturing && name !== "b") {
      return;
    }
    setMode("pad");
    if (name === "up" || name === "down") {
      move(name);
      return;
    }
    if (name === "left" || name === "right") {
      horizontal(name === "left" ? -1 : 1);
      return;
    }
    var route = GT4.input.padRoute(name, {
      capturing: GT4.capturing,
      dialogOpen: !!GT4.ui.topDialog(),
      tipOpen: !!document.querySelector(".info.is-open"),
      hooks: { x: !!GT4.hooks.x, y: !!GT4.hooks.y, start: !!GT4.hooks.start, tabs: !!GT4.hooks.tabs }
    });
    switch (route) {
      case "activate": activate(); break;
      case "back": case "close-tip": case "dialog-back": case "cancel-capture": back(); break;
      case "hook-x": GT4.hooks.x(); break;
      case "hook-y": GT4.hooks.y(); break;
      case "hook-start": GT4.hooks.start(); break;
      case "tab-prev": GT4.hooks.tabs(-1); break;
      case "tab-next": GT4.hooks.tabs(1); break;
      default: break;
    }
  }

  function currentPad() {
    var pads = navigator.getGamepads ? navigator.getGamepads() : [];
    for (var i = 0; i < pads.length; i++) {
      if (pads[i] && pads[i].connected) {
        return pads[i];
      }
    }
    return null;
  }

  // Nearest scrollable ancestor of the focused element, else the page's main scroll area.
  function isScrollable(el) {
    if (!el || el.scrollHeight <= el.clientHeight + 1) {
      return false;
    }
    var overflow = root.getComputedStyle(el).overflowY;
    return overflow === "auto" || overflow === "scroll";
  }

  function scrollTarget() {
    var el = document.activeElement;
    while (el && el !== document.body) {
      if (isScrollable(el)) {
        return el;
      }
      el = el.parentElement;
    }
    var fallback = scopeRoot().querySelector(".option-list, .lib-list, .content");
    return isScrollable(fallback) ? fallback : null;
  }

  function scrollWithStick(pad) {
    if (!pad || GT4.capturing) {
      return;
    }
    var step = GT4.input ? GT4.input.stickScroll(pad.axes[3] || 0) : 0;
    if (step) {
      var target = scrollTarget();
      if (target) {
        target.scrollTop += step;
      }
    }
  }

  function readPad(pad) {
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
    var pad = currentPad();
    var pressed = readPad(pad);
    scrollWithStick(pad);
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
      // A rebind that takes keys (Controls) gets every key first; Esc still cancels when it does not.
      if (GT4.captureKey && GT4.captureKey(event)) {
        event.preventDefault();
        return;
      }
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

  // "mouse", "keys" or "pad": the last kind of input used. Pages use it to tell a pad A from a mouse click.
  GT4.inputMode = function () {
    return mode;
  };

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
