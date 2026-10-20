/*
 * Dev only: DOM-level checks on the dev mock, run by dev/focus-check.html in headless Edge (FLOW-AUDIT 7.7).
 * Arrow keys stand in for the pad (nav.js handles both). The results are written into a <pre> at the end of the
 * page and body[data-focus-check] is "pass" or "fail".
 *
 *   (no query)          the sidebar walk over every tab, Saves and Replays entry, B back, value changes
 *   ?scenario=4k        only sets the state for docs/launcher/shots/graphics-4k-updated.png (4K chosen, row shown)
 */
(function (root) {
  "use strict";
  var GT4 = root.GT4;
  var TABS = ["graphics", "advanced", "display", "controls", "saves", "replays", "about"];
  var lines = [];
  var failures = 0;

  function wait(ms) {
    return new Promise(function (resolve) {
      root.setTimeout(resolve, ms);
    });
  }

  function press(name) {
    document.dispatchEvent(new KeyboardEvent("keydown", { key: name, bubbles: true, cancelable: true }));
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
        check("entering controls focuses the L1 shoulder tab", !!active && active.textContent === "L1", "focus " + (active && active.textContent));
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
    var select = document.querySelector("button.select[data-setting=\"upscale\"]");
    check("internal resolution is on the Graphics tab", !!select, "no control");
    if (!select) {
      return;
    }
    // A pad A (or a click) comes with focus on the control, as in the app.
    select.focus({ preventScroll: true });
    select.click();
    await wait(150);
    var option = Array.prototype.filter.call(document.querySelectorAll(".modal .option"), function (item) {
      return item.textContent.indexOf("4K (6x)") === 0;
    })[0];
    check("the resolution list offers 4K", !!option, "no 4K option");
    if (option) {
      option.click();
      await wait(300);
    }
    var label = document.querySelector("button.select[data-setting=\"upscale\"] span");
    check("the resolution row shows 4K (6x) after the change", !!label && label.textContent === "4K (6x)", label ? label.textContent : "none");
    var table = await GT4.backend.call("get_settings_table");
    var row = GT4.settingsModel.findRow(table, "upscale");
    check("the table holds 4K after the change", !!row && row.current === "4K", row ? String(row.current) : "no row");
    var focused = document.activeElement && document.activeElement.getAttribute("data-setting");
    check("focus stays on the changed control", focused === "upscale", "focus " + focused);

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

  // The screenshot state: Graphics with 4K chosen, the resolution row focused in pad mode.
  async function scenario4k() {
    GT4.nav.setMode("pad");
    await setHash("#/settings/graphics");
    press("ArrowRight");
    await wait(200);
    var select = document.querySelector("button.select[data-setting=\"upscale\"]");
    select.focus({ preventScroll: true });
    select.click();
    await wait(150);
    var option = Array.prototype.filter.call(document.querySelectorAll(".modal .option"), function (item) {
      return item.textContent.indexOf("4K (6x)") === 0;
    })[0];
    if (option) {
      option.click();
    }
    await wait(400);
  }

  async function run() {
    await wait(300);
    var scenario = new URLSearchParams(root.location.search).get("scenario") || "";
    try {
      if (scenario === "4k") {
        await scenario4k();
        return;
      }
      await routeChecks();
      await sideWalk();
      await enterChecks();
      await importEntryCheck();
      await valueChecks();
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
