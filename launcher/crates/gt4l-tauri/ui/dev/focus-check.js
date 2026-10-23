/*
 * Dev only: DOM-level checks on the dev mock, run by dev/focus-check.html in headless Edge (FLOW-AUDIT 7.7).
 * Arrow keys stand in for the pad (nav.js handles both). The results are written into a <pre> at the end of the
 * page and body[data-focus-check] is "pass" or "fail".
 *
 *   (no query)          the sidebar walk over every tab, Saves and Replays entry, B back, value changes, the
 *                       Display, Graphics and Advanced sections inside Graphics (no Display or Advanced tab; the
 *                       D-pad walks through all three), and the
 *                       Controls page (entry on the DS2, spatial moves, rebind and cancel, conflict and swap, live
 *                       input from a still dev pad, Y reset, X reset all)
 *   ?scenario=4k        only sets the state for docs/launcher/shots/graphics-4k-updated.png (4K chosen, row shown)
 *   ?scenario=advanced  only sets the state for docs/launcher/shots/graphics-advanced-section.png (pad focus on the
 *                       first Advanced row inside Graphics)
 *   ?scenario=render    only sets the state for docs/launcher/shots/graphics-sections.png (pad focus on Render scale,
 *                       the Graphics section in view)
 */
(function (root) {
  "use strict";
  var GT4 = root.GT4;
  var TABS = ["graphics", "controls", "games", "saves", "replays", "about"];
  var lines = [];
  var failures = 0;

  function wait(ms) {
    return new Promise(function (resolve) {
      root.setTimeout(resolve, ms);
    });
  }

  // A key press: down and up, so the page's held-key state (keysDown) clears as it does with a real keyboard.
  function press(name) {
    document.dispatchEvent(new KeyboardEvent("keydown", { key: name, bubbles: true, cancelable: true }));
    document.dispatchEvent(new KeyboardEvent("keyup", { key: name, bubbles: true, cancelable: true }));
  }

  function check(name, ok, detail) {
    lines.push((ok ? "PASS  " : "FAIL  ") + name + (ok ? "" : " (" + detail + ")"));
    if (!ok) {
      failures += 1;
    }
  }

  function sideTab() {
    var active = document.activeElement;
    if (!active || !active.closest || !active.closest("nav.side")) {
      return null;
    }
    return active.getAttribute("data-tab") || "(back)";
  }

  function shownTab() {
    var current = document.querySelector("nav.side [aria-current=\"page\"]");
    return current ? current.getAttribute("data-tab") : null;
  }

  function focusInContent() {
    var content = document.querySelector(".stage .content");
    return !!content && content.contains(document.activeElement);
  }

  function sideLevel() {
    var stage = document.querySelector(".stage");
    return !!stage && stage.classList.contains("is-side-level");
  }

  function footerText() {
    return Array.prototype.map.call(document.querySelectorAll("#hints .hint"), function (item) {
      return item.textContent;
    }).join("|");
  }

  function sideButton(tab) {
    return document.querySelector("nav.side [data-tab=\"" + tab + "\"]");
  }

  function setHash(hash) {
    root.location.hash = hash;
    return wait(250);
  }

  // The tab list has focus, the shown tab is the highlighted one, and nothing in the rows has focus.
  function sideStepOk(tab) {
    return sideTab() === tab && shownTab() === tab && !focusInContent() && sideLevel();
  }

  function sideDetail() {
    return "focus " + sideTab() + ", shown " + shownTab() + ", in rows " + focusInContent() + ", side level " + sideLevel();
  }

  // Rendering each tab through its route never moves focus off the tab list.
  async function routeChecks() {
    for (var i = 0; i < TABS.length; i++) {
      await setHash("#/settings/" + TABS[i]);
      check("opening #/settings/" + TABS[i] + " keeps focus on its tab", sideStepOk(TABS[i]), sideDetail());
      check("the footer on the tab list has no Save (" + TABS[i] + ")", footerText().indexOf("Save") < 0, footerText());
    }
  }

  // Arrow keys move the highlight through every tab and down again: focus stays in the tab list each time.
  async function sideWalk() {
    await setHash("#/settings/graphics");
    for (var i = 1; i < TABS.length; i++) {
      press("ArrowDown");
      await wait(150);
      check("ArrowDown highlights " + TABS[i] + "; focus stays in the tab list", sideStepOk(TABS[i]), sideDetail());
    }
    for (var j = TABS.length - 2; j >= 0; j--) {
      press("ArrowUp");
      await wait(150);
      check("ArrowUp highlights " + TABS[j] + "; focus stays in the tab list", sideStepOk(TABS[j]), sideDetail());
    }
  }

  // From the tab list, D-pad right (A) enters a tab; B goes back to the tab list on the same tab.
  async function enterChecks() {
    await setHash("#/settings/graphics");
    var expected = {
      saves: "01",
      replays: "01"
    };
    for (var i = 0; i < TABS.length; i++) {
      var tab = TABS[i];
      await setHash("#/settings/" + tab);
      press("ArrowRight");
      await wait(200);
      var active = document.activeElement;
      if (expected[tab]) {
        var first = active && active.getAttribute && active.getAttribute("data-item-id") === expected[tab];
        check("entering " + tab + " focuses its first row", !!first, "focus " + (active && active.outerHTML ? active.outerHTML.slice(0, 80) : active));
      } else if (tab === "controls") {
        var control = active && active.getAttribute ? active.getAttribute("data-control") : null;
        check("entering controls focuses Cross on the drawing", control === "cross", "focus " + control);
      } else {
        check("entering " + tab + " puts focus in its rows", focusInContent() && !sideLevel(), sideDetail());
      }
      check("entering " + tab + " shows no Save in the footer", footerText().indexOf("Save") < 0, footerText());
      press("Escape");
      await wait(150);
      check("B returns " + tab + " to the tab list on the same tab", sideStepOk(tab), sideDetail());
    }
  }

  // Saves with an empty list would focus Import; the mock has three saves, so the first row is the entry.
  async function importEntryCheck() {
    await setHash("#/settings/saves");
    press("ArrowRight");
    await wait(200);
    var active = document.activeElement;
    check("Saves never auto-selects Import", !(active && active.textContent && active.textContent.indexOf("Import") >= 0), active ? active.textContent : "none");
  }

  // Changed values show on screen at once and stay after the table is read again.
  async function valueChecks() {
    await setHash("#/settings/graphics");
    // TASK-080 addendum: no Internal resolution or Aspect ratio row; Render scale shows the resulting resolution.
    var gone = document.querySelector(".content [data-setting=\"upscale\"], .content [data-setting=\"aspect_ratio\"], " +
      ".content [data-setting=\"aspect_custom\"]");
    check("no internal resolution or aspect row is shown", !gone, gone ? gone.getAttribute("data-setting") : "");
    var scale = document.querySelector("input[type=range][data-setting=\"render_scale\"]");
    check("render scale is a slider on the Graphics tab", !!scale, "no control");
    var readout = scale && scale.parentNode.querySelector(".val");
    check("the render scale readout shows the resulting resolution", !!readout && readout.textContent === "100% \u00B7 3x \u00B7 1920x1344",
      readout ? readout.textContent : "none");
    if (scale) {
      scale.focus({ preventScroll: true });
      scale.dispatchEvent(new CustomEvent("gt4-adjust", { detail: { delta: 1 } }));
      await wait(300);
      var after = document.querySelector("input[type=range][data-setting=\"render_scale\"]");
      var afterReadout = after && after.parentNode.querySelector(".val");
      check("one step right gives 125% and 4x", !!afterReadout && afterReadout.textContent === "125% \u00B7 4x \u00B7 2560x1792",
        afterReadout ? afterReadout.textContent : "none");
      var scaleFocus = document.activeElement && document.activeElement.getAttribute("data-setting");
      check("focus stays on the render scale slider", scaleFocus === "render_scale", "focus " + scaleFocus);
      after.dispatchEvent(new CustomEvent("gt4-adjust", { detail: { delta: -1 } }));
      await wait(300);
    }

    await setHash("#/settings/graphics");
    var select = document.querySelector("[data-section=\"display\"] button.select[data-setting=\"resolution\"]");
    check("resolution is in the Display section of Graphics", !!select, "no control");
    check("monitor is in the Display section of Graphics",
      !!document.querySelector("[data-section=\"display\"] button.select[data-setting=\"monitor\"]"), "no control");
    if (!select) {
      return;
    }
    // A pad A (or a click) comes with focus on the control, as in the app.
    select.focus({ preventScroll: true });
    select.click();
    await wait(150);
    var option = Array.prototype.filter.call(document.querySelectorAll(".modal .option"), function (item) {
      return item.textContent.indexOf("1280x720") === 0;
    })[0];
    check("the resolution list offers the monitor's modes", !!option, "no 1280x720 option");
    if (option) {
      option.click();
      await wait(300);
    }
    var label = document.querySelector("button.select[data-setting=\"resolution\"] span");
    check("the resolution row shows 1280x720 after the change", !!label && label.textContent === "1280x720", label ? label.textContent : "none");
    var table = await GT4.backend.call("get_settings_table");
    var row = GT4.settingsModel.findRow(table, "resolution");
    check("the table holds 1280x720 after the change", !!row && row.current === "1280x720", row ? String(row.current) : "no row");
    var scaleRow = GT4.settingsModel.findRow(table, "render_scale");
    check("the render scale readout follows the resolution", scaleRow.values.labels["100"] === "100% \u00B7 2x \u00B7 1280x896",
      scaleRow.values.labels["100"]);
    var focused = document.activeElement && document.activeElement.getAttribute("data-setting");
    check("focus stays on the changed control", focused === "resolution", "focus " + focused);
    await GT4.backend.call("set_setting", { id: "resolution", value: "1920x1080" });
    await setHash("#/settings/graphics");

    var toggle = document.querySelector("button.toggle[data-setting=\"fxaa\"]");
    check("anti-aliasing is a toggle on Graphics", !!toggle, "no control");
    if (toggle) {
      toggle.click();
      await wait(300);
      var on = document.querySelector("button.toggle[data-setting=\"fxaa\"]");
      check("the toggle shows on after the change", !!on && on.getAttribute("aria-checked") === "true", on ? on.getAttribute("aria-checked") : "none");
      var fxaa = GT4.settingsModel.findRow(await GT4.backend.call("get_settings_table"), "fxaa");
      check("the table holds the toggle value", !!fxaa && fxaa.current === true, fxaa ? String(fxaa.current) : "no row");
    }

    var slider = document.querySelector("input[type=range][data-setting=\"cas_sharpness\"]");
    check("sharpening amount is a slider on Graphics", !!slider, "no control");
    if (slider) {
      var before = Number(slider.value);
      slider.dispatchEvent(new CustomEvent("gt4-adjust", { detail: { delta: 1 } }));
      await wait(300);
      var after = document.querySelector("input[type=range][data-setting=\"cas_sharpness\"]");
      check("the slider moves one step and shows the new value", !!after && Number(after.value) === before + 1, after ? after.value : "none");
      var sharp = GT4.settingsModel.findRow(await GT4.backend.call("get_settings_table"), "cas_sharpness");
      check("the table holds the slider value", !!sharp && sharp.current === before + 1, sharp ? String(sharp.current) : "no row");
    }
  }

  // Advanced is a section at the bottom of Graphics (owner, 2026-10-21), not a tab: the old route falls back to
  // Graphics, the D-pad walks from the Graphics rows into the Advanced rows and the reset button, Y is Reset, and B
  // returns to the tab list from inside the section.
  function focusedSetting() {
    var active = document.activeElement;
    return active && active.getAttribute ? active.getAttribute("data-setting") || active.getAttribute("data-action") : null;
  }

  async function advancedSectionChecks() {
    check("the tab list has no Advanced tab", !sideButton("advanced"), "an Advanced tab is shown");
    check("the tab list has no Display tab", !sideButton("display"), "a Display tab is shown");
    await setHash("#/settings/advanced");
    check("#/settings/advanced falls back to Graphics", shownTab() === "graphics", "shown " + shownTab());
    await setHash("#/settings/display");
    check("#/settings/display falls back to Graphics", shownTab() === "graphics", "shown " + shownTab());
    await setHash("#/settings/graphics");
    var heads = Array.prototype.map.call(document.querySelectorAll(".content .section-head"), function (item) {
      return item.textContent;
    }).join("|");
    check("Graphics shows the Display, Graphics and Advanced sections in order", heads === "Display|Graphics|Advanced", heads);
    var displayIds = Array.prototype.map.call(document.querySelectorAll("[data-section=\"display\"] .row"), function (item) {
      return item.getAttribute("data-row");
    }).join("|");
    check("the Display section holds Monitor, Resolution, Fullscreen and VSync", displayIds === "monitor|resolution|fullscreen|vsync", displayIds);
    var graphicsSection = document.querySelector("[data-section=\"graphics\"]");
    var inGraphics = !!graphicsSection && ["renderer", "render_scale", "fxaa"].every(function (id) {
      return !!graphicsSection.querySelector("[data-row=\"" + id + "\"]");
    });
    check("the Graphics section holds Renderer, Render scale and FXAA", inGraphics, "missing rows");
    var looseRows = document.querySelectorAll(".content > .row").length;
    check("every Graphics row sits in a section", looseRows === 0, looseRows + " loose rows");
    var firstAdvanced = document.querySelector("[data-section=\"advanced\"] [data-setting]");
    var firstId = firstAdvanced ? firstAdvanced.getAttribute("data-setting") : null;
    check("the Advanced section holds the advanced rows", firstId === "async_gs", "first " + firstId);
    press("ArrowRight");
    await wait(200);
    // Without a remembered row, A enters on the first control of the tab (settings.js focusRow): Monitor, at the top of
    // Display. Earlier checks left a remembered row, so the walk starts from Monitor explicitly.
    var firstControl = document.querySelector(".content .ctl [data-setting]");
    var firstControlId = firstControl ? firstControl.getAttribute("data-setting") : null;
    check("the first control of Graphics is Monitor, at the top of Display", firstControlId === "monitor", "first " + firstControlId);
    if (firstControl) {
      firstControl.focus({ preventScroll: true });
    }
    var reached = false;
    var reset = false;
    var passedGraphics = false;
    var walk = [];
    for (var i = 0; i < 60 && !reset; i++) {
      press("ArrowDown");
      await wait(80);
      var id = focusedSetting();
      walk.push(id || (document.activeElement ? document.activeElement.className : "none"));
      var active = document.activeElement;
      if (active && active.closest && active.closest("[data-section=\"graphics\"] .row")) {
        passedGraphics = true; // a control or the "i" button of a Graphics row
      }
      if (active && active.closest && active.closest("[data-section=\"advanced\"] .row")) {
        reached = true; // a control or the "i" button of an Advanced row
      }
      if (id === "reset-defaults") {
        reset = true;
      }
    }
    check("D-pad down from the Display rows passes through the Graphics rows", passedGraphics, "walk " + walk.join(" > "));
    check("D-pad down from the Graphics rows reaches the Advanced rows", reached, "walk " + walk.join(" > "));
    check("D-pad down reaches Reset to defaults at the end of the section", reset, "focus " + focusedSetting());
    check("the Graphics content footer offers Y Reset", /Reset/.test(footerText()), footerText());
    var inSection = document.querySelector("[data-section=\"advanced\"] [data-setting=\"perf_overlay\"]");
    check("our performance overlay row is in the Advanced section", !!inSection, "no perf_overlay row");
    var osd = document.querySelector(".content [data-setting=\"osd_fps\"], .content [data-setting=\"aspect\"], " +
      ".content [data-setting=\"input_resolution\"], .content [data-setting=\"widescreen\"], " +
      ".content [data-setting=\"aspect_ratio\"], .content [data-setting=\"upscale\"]");
    check("no PCSX2 OSD, aspect, input resolution or widescreen row is shown", !osd, osd ? osd.getAttribute("data-setting") : "");
    press("ArrowUp");
    await wait(80);
    press("Escape");
    await wait(150);
    check("B inside the Advanced section returns to the tab list on Graphics", sideStepOk("graphics"), sideDetail());
  }

  async function scenarioAdvanced() {
    GT4.nav.setMode("pad");
    await setHash("#/settings/graphics");
    press("ArrowRight");
    await wait(200);
    var first = document.querySelector("[data-section=\"advanced\"] [data-setting]");
    if (first) {
      first.focus();
      first.scrollIntoView({ block: "center" });
    }
    await wait(300);
  }

  // The screenshot state of docs/launcher/shots/graphics-sections.png: pad focus on Render scale, centred, so the end
  // of the Display section, the Graphics section and the start of Advanced are in view.
  async function scenarioRender() {
    GT4.nav.setMode("pad");
    await setHash("#/settings/graphics");
    press("ArrowRight");
    await wait(200);
    var scale = document.querySelector("input[type=range][data-setting=\"render_scale\"]");
    if (scale) {
      scale.focus();
      scale.scrollIntoView({ block: "center" });
    }
    await wait(300);
  }

  // Controls (js/pages/controls.js): entry on the DS2, spatial moves, rebind with cancel, conflicts, live input, reset.
  function key(name, code) {
    document.dispatchEvent(new KeyboardEvent("keydown", { key: name, code: code || name, bubbles: true, cancelable: true }));
    document.dispatchEvent(new KeyboardEvent("keyup", { key: name, code: code || name, bubbles: true, cancelable: true }));
  }

  function focusedControl() {
    var active = document.activeElement;
    var group = active && active.closest ? active.closest(".pad-ctl") : null;
    return group ? group.getAttribute("data-control") : null;
  }

  function cardName() {
    var name = document.querySelector(".cx-card .cx-name");
    return name ? name.textContent : null;
  }

  function badgeLabel(id) {
    var badge = document.querySelector(".badge[data-for=\"" + id + "\"]");
    return badge ? badge.getAttribute("data-label") : null;
  }

  async function controlsOf(id) {
    var view = await GT4.backend.call("get_controls");
    return view.controls.filter(function (control) { return control.id === id; })[0];
  }

  async function controlsChecks() {
    GT4.devGamepads = function () { return []; };
    await setHash("#/settings/controls");
    await wait(400);
    check("the Controls preview with live input running keeps focus on the tab list", sideStepOk("controls"), sideDetail());
    press("ArrowRight");
    await wait(200);
    check("entering Controls focuses Cross on the drawing", focusedControl() === "cross", "focus " + focusedControl());
    check("the Controls footer offers Rebind, Reset button and Reset all", /Rebind/.test(footerText()) && /Reset button/.test(footerText()) && /Reset all/.test(footerText()), footerText());

    press("ArrowUp");
    await wait(120);
    check("D-pad up from Cross reaches Triangle", focusedControl() === "triangle", "focus " + focusedControl());
    press("ArrowDown");
    await wait(120);
    press("ArrowLeft");
    await wait(120);
    check("D-pad left from Cross reaches Square", focusedControl() === "square", "focus " + focusedControl());
    check("the selection follows focus (panel shows Square)", cardName() === "Square", cardName());
    press("ArrowLeft");
    await wait(120);
    check("D-pad left from Square reaches Start", focusedControl() === "start", "focus " + focusedControl());
    press("ArrowRight");
    await wait(120);

    // Rebind, then cancel with Esc (B on the pad goes through the same GT4.cancelCapture).
    GT4.nav.activate();
    await wait(200);
    check("A on a DS2 button opens the rebind overlay", !!document.querySelector(".modal.is-capture") && GT4.capturing === true, "capturing " + GT4.capturing);
    check("the overlay names the button", /Square/.test((document.querySelector(".modal.is-capture h3") || {}).textContent || ""), "no title");
    check("the rebind footer shows only B Cancel", footerText() === "BCancel", footerText());
    press("Escape");
    await wait(200);
    check("Esc cancels the rebind and closes the overlay", !document.querySelector(".modal.is-capture") && GT4.capturing === false, "capturing " + GT4.capturing);
    check("after a cancel, focus is back on the button", focusedControl() === "square", "focus " + focusedControl());
    check("a cancelled rebind changes nothing", (await controlsOf("square")).key === "A", "key " + (await controlsOf("square")).key);

    // Enter on a focused button starts a rebind; that same Enter is not taken as the new key.
    var squareGroup = document.querySelector(".pad-ctl[data-control=\"square\"]");
    squareGroup.dispatchEvent(new KeyboardEvent("keydown", { key: "Enter", code: "Enter", bubbles: true, cancelable: true }));
    squareGroup.dispatchEvent(new KeyboardEvent("keyup", { key: "Enter", code: "Enter", bubbles: true, cancelable: true }));
    await wait(200);
    check("Enter on a DS2 button opens the rebind and keeps waiting", !!document.querySelector(".modal.is-capture") && GT4.capturing === true, "capturing " + GT4.capturing);
    press("Escape");
    await wait(200);
    check("Enter did not become the binding", (await controlsOf("square")).key === "A", "key " + (await controlsOf("square")).key);

    // A controller button already on another DS2 button: the dev mock's capture answers RB (R1's); the conflict
    // dialog opens. Esc keeps everything. (Keyboard rebinding is off: a key only cancels.)
    GT4.nav.activate();
    await wait(2600);
    var dialogTitle = (document.querySelector(".modal h3") || {}).textContent || "";
    check("a pad button already on R1 opens the conflict dialog", /already on R1/.test(dialogTitle), dialogTitle);
    press("Escape");
    await wait(200);
    check("cancelling the conflict changes nothing", (await controlsOf("square")).xinput === "X" && (await controlsOf("r1")).xinput === "RB", "square/r1 changed");

    // Swap: Square takes RB from R1, R1 gets X (Square's previous controller button). The chips follow at once.
    GT4.nav.activate();
    await wait(2600);
    var swap = Array.prototype.filter.call(document.querySelectorAll(".modal button"), function (button) {
      return button.textContent === "Swap";
    })[0];
    check("the conflict dialog offers Swap", !!swap, "no Swap button");
    if (swap) {
      swap.click();
      await wait(300);
    }
    var square = await controlsOf("square");
    var r1 = await controlsOf("r1");
    check("Swap gives Square RB and R1 X", square.xinput === "RB" && r1.xinput === "X", square.xinput + "/" + r1.xinput);
    check("the chips show the swap (controller view)", badgeLabel("square") === "RB" && badgeLabel("r1") === "X", badgeLabel("square") + "/" + badgeLabel("r1"));
    check("a swapped button reads Changed in the panel", (document.querySelector(".cx-card .cx-state") || {}).textContent === "Changed", "state");

    // Live input from the pad events (a still dev pad, sent by the mock like the Rust service): Cross lights from A,
    // L2 fills to 60%.
    GT4.devGamepads = function () {
      var buttons = GT4.controlsModel.STANDARD_TO_XINPUT.map(function (name) {
        var value = name === "A" ? 1 : (name === "LT" ? 0.6 : 0);
        return { pressed: value > 0.12, value: value };
      });
      return [{ id: "Test Pad (XInput STANDARD GAMEPAD)", index: 0, connected: true, buttons: buttons, axes: [1, 0, 0, -1] }];
    };
    await wait(200);
    var cross = document.querySelector(".pad-ctl[data-control=\"cross\"]");
    check("pad A lights Cross on the drawing", !!cross && cross.classList.contains("is-down"), "no is-down");
    var level = document.querySelector(".pad-ctl[data-control=\"l2\"] .level");
    check("LT at 60% fills L2 to 60%", !!level && Math.abs(Number(level.getAttribute("height")) - 42 * 0.6) < 0.2, level ? level.getAttribute("height") : "none");
    var capL = document.querySelector(".pad-ctl[data-control=\"l3\"] .stick-cap");
    check("the left stick moves to its real position", !!capL && capL.getAttribute("transform") === "translate(24 0)", capL ? capL.getAttribute("transform") : "none");
    check("the status names the controller", /Test Pad/.test((document.querySelector(".cx-status") || {}).textContent || ""), "status");
    check("live input never moves focus", focusedControl() === "square", "focus " + focusedControl());
    GT4.devGamepads = function () { return []; };
    await wait(150);
    check("released buttons go dark", !cross.classList.contains("is-down"), "still down");

    // Y resets the selected button; X resets everything after a confirmation.
    GT4.hooks.y();
    await wait(300);
    check("Y resets Square to its default key", (await controlsOf("square")).key === "A", (await controlsOf("square")).key);
    GT4.hooks.x();
    await wait(150);
    var resetAll = Array.prototype.filter.call(document.querySelectorAll(".modal button"), function (button) {
      return button.textContent === "Reset all";
    })[0];
    check("X asks before resetting everything", !!resetAll, "no confirmation");
    if (resetAll) {
      resetAll.click();
      await wait(300);
    }
    check("Reset all restores L1", (await controlsOf("l1")).key === "Q" && (await controlsOf("l1")).is_default, "l1 " + (await controlsOf("l1")).key);
    press("Escape");
    await wait(150);
    check("B returns Controls to the tab list", sideStepOk("controls"), sideDetail());
    delete GT4.devGamepads;
  }

  // The screenshot state (scenario "4k", name kept for the screenshot list): Graphics with Render scale at 200%, the
  // slider focused in pad mode (TASK-080 addendum: there is no Internal resolution row any more).
  async function scenario4k() {
    GT4.nav.setMode("pad");
    await setHash("#/settings/graphics");
    press("ArrowRight");
    await wait(200);
    var scale = document.querySelector("input[type=range][data-setting=\"render_scale\"]");
    if (scale) {
      scale.focus({ preventScroll: true });
      for (var i = 0; i < 4; i++) {
        var current = document.querySelector("input[type=range][data-setting=\"render_scale\"]");
        current.focus({ preventScroll: true });
        current.dispatchEvent(new CustomEvent("gt4-adjust", { detail: { delta: 1 } }));
        await wait(250);
      }
    }
    await wait(400);
  }

  // Two games (TASK-101): the version switch under Play (the name between two arrows, an endless loop; owner, 2026-10-23),
  // Play or Install, Settings > Games and the game on the Saves title. The mock starts with Gran Turismo 4 installed and
  // Spec II not (?games=gt4).
  async function gamesChecks() {
    await setHash("#/home");
    await wait(500);
    var arrows = document.querySelectorAll(".version-arrow");
    // The name that is shown (a leaving name may still be sliding out beside it).
    var currentName = function () {
      var names = document.querySelectorAll(".version-name:not(.is-leaving)");
      return names[names.length - 1];
    };
    var nameText = function () {
      var names = document.querySelectorAll(".version-name:not(.is-leaving)");
      return names.length ? names[names.length - 1].textContent : "";
    };
    var playText = function () { return document.querySelector(".play").textContent; };
    check("Home has the version switch: two arrows around Normal", arrows.length === 2 && nameText() === "Normal",
      arrows.length + " " + nameText());
    check("Gran Turismo 4 is selected first and shows Play", playText() === "Play", playText());
    check("the logo is the Gran Turismo 4 logo, with no Spec II line", !document.querySelector(".home-logo-spec2") &&
      !!document.querySelector(".home-logo"), "");
    arrows[1].click();
    await wait(400);
    check("the right arrow switches to Spec II and shows Install (no other not-installed mark)", nameText() === "Spec II" &&
      playText() === "Install" && !document.querySelector(".version-note"), nameText() + " " + playText());
    check("the new name came in from the right", currentName().classList.contains("enter-from-right"), "");
    arrows[1].click();
    await wait(400);
    check("the loop is endless: right again comes back to Normal", nameText() === "Normal" && playText() === "Play", nameText());
    arrows[0].click();
    await wait(400);
    check("the left arrow switches too, sliding in from the left", nameText() === "Spec II" &&
      currentName().classList.contains("enter-from-left"), nameText());
    GT4.hooks.tabs(-1);
    await wait(400);
    check("LB / RB switches back to Normal and Play", playText() === "Play" && nameText() === "Normal", nameText());
    var notes = document.querySelector(".home-notes");
    check("the patch notes card shows this version's notes (TASK-099)", !!notes && !notes.hidden &&
      !!notes.querySelector(".home-notes-body .md-h2") && notes.querySelectorAll(".home-notes-body li").length > 0 &&
      document.querySelector(".home-notes-version").textContent.length > 0, notes ? "card" : "none");
    check("the notes title line is not repeated and raw HTML stays text", !notes.querySelector(".md-h1") &&
      !notes.querySelector(".home-notes-body b") && /<b>raw HTML<\/b>/.test(notes.textContent), "");
    check("the notes footer opens the releases page", !!notes.querySelector(".home-notes-more"), "");
    var older = notes.querySelectorAll(".notes-arrow")[0];
    var newer = notes.querySelectorAll(".notes-arrow")[1];
    var shownVersion = function () { return document.querySelector(".home-notes-version").textContent; };
    check("the notes open on the newest release; newer is disabled at the start", shownVersion() === "1.3.0" &&
      newer.disabled && !older.disabled, shownVersion());
    older.click();
    await wait(100);
    check("the left arrow shows an older release, sliding in from the left", shownVersion() === "1.2.3" &&
      !!notes.querySelector(".home-notes-page.enter-from-left") && !newer.disabled, shownVersion());
    for (var step = 0; step < 10 && !older.disabled; step++) {
      older.click();
    }
    await wait(100);
    check("no loop: the oldest release disables the left arrow", shownVersion() === "1.0.0" && older.disabled, shownVersion());
    older.click();
    await wait(100);
    check("an extra click at the oldest stays there", shownVersion() === "1.0.0", shownVersion());
    newer.click();
    await wait(100);
    check("the right arrow goes back to a newer release", shownVersion() === "1.1.0" &&
      !!notes.querySelector(".home-notes-page.enter-from-right"), shownVersion());
    check("the pad hints name the game switch", /LB \/ RB/.test(document.getElementById("hints").textContent), document.getElementById("hints").textContent);

    await setHash("#/settings/games");
    await wait(400);
    var rows = document.querySelectorAll(".game-row");
    check("Settings > Games lists both games", rows.length === 2, String(rows.length));
    var labels = Array.prototype.map.call(rows[0].querySelectorAll("button"), function (b) { return b.textContent; });
    check("an installed game offers Change disc image and Remove", labels.join("|") === "Change disc image|Remove", labels.join("|"));
    var other = Array.prototype.map.call(rows[1].querySelectorAll("button"), function (b) { return b.textContent; });
    check("a game that is not installed offers Install", other.join("|") === "Install", other.join("|"));
    rows[0].querySelectorAll("button")[1].click();
    await wait(200);
    var dialog = document.querySelector(".modal");
    check("Remove asks first and says the saves are kept", !!dialog && /saves and replays are kept/i.test(dialog.textContent), dialog ? dialog.textContent : "no dialog");
    if (dialog) {
      GT4.ui.topDialog().close();
    }

    await setHash("#/settings/saves");
    await wait(400);
    var tag = document.querySelector(".game-tag");
    check("the Saves title names the game", !!tag && tag.textContent === "Gran Turismo 4", tag ? tag.textContent : "no tag");
    await GT4.games.select("spec2");
    await setHash("#/settings/replays");
    await wait(400);
    await setHash("#/settings/saves");
    await wait(400);
    tag = document.querySelector(".game-tag");
    check("another game has its own list and title", !!tag && tag.textContent === "Gran Turismo 4 Spec II" &&
      !!document.querySelector(".emptystate"), tag ? tag.textContent : "no tag");
    await GT4.games.select("gt4");
  }

  async function run() {
    await wait(300);
    var scenario = new URLSearchParams(root.location.search).get("scenario") || "";
    try {
      if (scenario === "4k") {
        await scenario4k();
        return;
      }
      if (scenario === "advanced") {
        await scenarioAdvanced();
        return;
      }
      if (scenario === "render") {
        await scenarioRender();
        return;
      }
      await routeChecks();
      await sideWalk();
      await enterChecks();
      await importEntryCheck();
      await valueChecks();
      await advancedSectionChecks();
      await controlsChecks();
      await gamesChecks();
    } catch (err) {
      check("the checks ran to the end", false, String(err && err.message ? err.message : err));
    }
    var summary = lines.length - failures + " of " + lines.length + " passed";
    var pre = document.createElement("pre");
    pre.id = "focus-check-results";
    pre.textContent = summary + "\n" + lines.join("\n");
    document.body.appendChild(pre);
    document.body.setAttribute("data-focus-check", failures ? "fail" : "pass");
  }

  run();
})(window);
