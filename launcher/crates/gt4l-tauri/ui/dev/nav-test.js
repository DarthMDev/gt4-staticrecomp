/*
 * Unit tests for GT4.spatial.pickNext (ui/js/spatial.js). Runs in any browser
 * through nav-test.html; prints PASS/FAIL per case and a summary.
 */
(function (root) {
  "use strict";
  var pick = root.GT4.spatial.pickNext;
  var cases = [];

  function rect(left, top, width, height) {
    return { left: left, top: top, width: width || 10, height: height || 10 };
  }

  function expect(name, actual, expected) {
    cases.push({ name: name, pass: actual === expected, detail: "got " + actual + ", expected " + expected });
  }

  // Row of three: right and left move along it.
  var row = [rect(0, 0), rect(20, 0), rect(40, 0)];
  expect("right moves to the next item in a row", pick(row, 0, "right"), 1);
  expect("right from the last item finds nothing", pick(row, 2, "right"), -1);
  expect("left moves back along the row", pick(row, 2, "left"), 1);
  expect("left from the first item finds nothing", pick(row, 0, "left"), -1);

  // Column: up and down.
  var column = [rect(0, 0), rect(0, 20), rect(0, 40)];
  expect("down moves to the item below", pick(column, 0, "down"), 1);
  expect("up from the top finds nothing", pick(column, 0, "up"), -1);
  expect("up moves to the item above", pick(column, 2, "up"), 1);

  // Items behind the origin are never chosen.
  var behind = [rect(100, 100), rect(0, 100), rect(100, 0)];
  expect("items to the left are ignored for right", pick(behind, 0, "right"), -1);
  expect("items above are ignored for down", pick(behind, 0, "down"), -1);

  // Sideways offset is penalised: a straight-below item beats a nearer diagonal one.
  var penalty = [rect(0, 0), rect(0, 100), rect(30, 60)];
  expect("straight below beats a nearer diagonal item", pick(penalty, 0, "down"), 1);

  // Grid 3x3, index = row * 3 + col.
  var grid = [];
  for (var r = 0; r < 3; r++) {
    for (var c = 0; c < 3; c++) {
      grid.push(rect(c * 50, r * 50));
    }
  }
  expect("grid centre right is the middle-right cell", pick(grid, 4, "right"), 5);
  expect("grid centre left is the middle-left cell", pick(grid, 4, "left"), 3);
  expect("grid centre down is the bottom-centre cell", pick(grid, 4, "down"), 7);
  expect("grid bottom-left up is the middle-left cell", pick(grid, 6, "up"), 3);
  expect("grid top-right down is the middle-right cell", pick(grid, 2, "down"), 5);

  // Different sizes: centres are compared, not edges.
  var sizes = [rect(0, 0, 100, 40), rect(120, 10, 20, 20)];
  expect("different-sized neighbours compare by centre", pick(sizes, 0, "right"), 1);

  // Invalid input.
  expect("bad origin index gives -1", pick(row, 9, "right"), -1);
  expect("unknown direction gives -1", pick(row, 0, "diagonal"), -1);
  expect("empty list gives -1", pick([], 0, "down"), -1);

  // Sideways items on the same line as the origin are not "ahead".
  var sameLine = [rect(0, 0), rect(0, 0)];
  expect("an item exactly on the origin is not ahead", pick(sameLine, 0, "down"), -1);

  // Swipe: a finger moving left asks for the next tab (+1); right for the previous one (-1).
  expect("swipe left (finger) gives the next tab", root.GT4.input.swipeStep(-80, 5), 1);
  expect("swipe right (finger) gives the previous tab", root.GT4.input.swipeStep(90, -10), -1);
  expect("a 59 px swipe is not a swipe", root.GT4.input.swipeStep(-59, 0), 0);
  expect("exactly 60 px counts as a swipe", root.GT4.input.swipeStep(-60, 0), 1);
  expect("a mostly vertical drag is not a swipe", root.GT4.input.swipeStep(-100, 80), 0);
  expect("a diagonal drag at 1.5 ratio is not a swipe", root.GT4.input.swipeStep(-90, 60), 0);
  expect("no movement is not a swipe", root.GT4.input.swipeStep(0, 0), 0);

  // Right stick: deadzone 0.2, full deflection 18 px per frame, sign follows the stick.
  expect("stick inside the deadzone does not scroll", root.GT4.input.stickScroll(0.15), 0);
  expect("stick at rest does not scroll", root.GT4.input.stickScroll(0), 0);
  expect("stick full down scrolls 18 px", root.GT4.input.stickScroll(1), 18);
  expect("stick full up scrolls -18 px", root.GT4.input.stickScroll(-1), -18);
  expect("stick just past the deadzone scrolls a little", root.GT4.input.stickScroll(0.25) > 0 && root.GT4.input.stickScroll(0.25) < 2, true);

  // Slider from a touch position: 7 px thumb inset is applied by the caller; here the track is plain.
  expect("touch at the left end gives min", root.GT4.input.valueFromPointer(0, 0, 100, 1, 12, 1), 1);
  expect("touch at the right end gives max", root.GT4.input.valueFromPointer(100, 0, 100, 1, 12, 1), 12);
  expect("touch past the right end clamps to max", root.GT4.input.valueFromPointer(300, 0, 100, 1, 12, 1), 12);
  expect("touch before the left end clamps to min", root.GT4.input.valueFromPointer(-40, 0, 100, 1, 12, 1), 1);
  expect("touch in the middle rounds to a step", root.GT4.input.valueFromPointer(50, 0, 100, 1, 12, 1), 7);
  expect("steps of 5 snap to the step grid", root.GT4.input.valueFromPointer(52, 0, 100, 0, 100, 5), 50);
  expect("a zero-width track gives min", root.GT4.input.valueFromPointer(10, 5, 0, 1, 12, 1), 1);

  // Face and shoulder buttons: which route each one takes in each state.
  var pageHooks = { x: true, y: true, start: true, tabs: true };
  var open = { hooks: pageHooks, dialogOpen: false, tipOpen: false, capturing: false };
  var route = root.GT4.input.padRoute;
  expect("A activates", route("a", open), "activate");
  expect("B goes back on a plain page", route("b", open), "back");
  expect("B closes an open tip first", route("b", { hooks: pageHooks, tipOpen: true }), "close-tip");
  expect("B closes a dialog", route("b", { hooks: pageHooks, dialogOpen: true }), "dialog-back");
  expect("X runs the page's x hook", route("x", open), "hook-x");
  expect("Y runs the page's y hook", route("y", open), "hook-y");
  expect("X does nothing on a page without an x hook", route("x", { hooks: { y: true } }), null);
  expect("Start runs the page's start hook", route("start", open), "hook-start");
  expect("LB goes to the previous tab", route("lb", open), "tab-prev");
  expect("RB goes to the next tab", route("rb", open), "tab-next");
  expect("LB does nothing on a page without tabs", route("lb", { hooks: {} }), null);
  expect("X is blocked by an open dialog", route("x", { hooks: pageHooks, dialogOpen: true }), null);
  expect("A still activates inside a dialog", route("a", { hooks: pageHooks, dialogOpen: true }), "activate");
  expect("while a rebind waits, B cancels it", route("b", { hooks: pageHooks, capturing: true }), "cancel-capture");
  expect("while a rebind waits, A does nothing", route("a", { hooks: pageHooks, capturing: true }), null);
  expect("while a rebind waits, Y does nothing", route("y", { hooks: pageHooks, capturing: true }), null);

  // Two-level focus (js/focus-levels.js): enter, B back, LB/RB keeping the level, the remembered row per tab.
  var FL = root.GT4.focusLevels;
  var TABS = ["graphics", "advanced", "display", "controls"];
  function same(name, actual, expected) {
    expect(name, JSON.stringify(actual), JSON.stringify(expected));
  }

  var opened = FL.start("graphics");
  same("opening Settings starts on the tab list", opened.level, FL.SIDE);
  same("opening Settings shows the current tab", opened.tab, "graphics");

  var entered = FL.enter(opened);
  same("A on a tab enters its content", entered.level, FL.CONTENT);
  same("entering keeps the tab", entered.tab, "graphics");
  same("entering does not change the input state", opened.level, FL.SIDE);

  var rowed = FL.remember(entered, "fxaa");
  same("focus on a row is remembered for its tab", FL.rowFor(rowed), "fxaa");
  var backed = FL.back(rowed);
  same("B in the content returns to the tab list", backed.level, FL.SIDE);
  same("B returns to the same tab", backed.tab, "graphics");
  same("B resets nothing: the row is still remembered", FL.rowFor(backed), "fxaa");
  same("entering again lands on the remembered row", FL.rowFor(FL.enter(backed)), "fxaa");

  var lbContent = FL.moveTab(entered, 1, TABS);
  same("LB/RB from content keeps the content level", lbContent.level, FL.CONTENT);
  same("LB/RB from content moves to the next tab", lbContent.tab, "advanced");
  var lbSide = FL.moveTab(backed, 1, TABS);
  same("LB/RB from the tab list keeps the tab-list level", lbSide.level, FL.SIDE);
  same("the previous tab is remembered per tab", FL.rowFor(FL.moveTab(FL.moveTab(rowed, 1, TABS), -1, TABS)), "fxaa");
  same("another tab has no remembered row yet", FL.rowFor(lbContent), null);
  same("LB/RB wraps from the last tab to the first", FL.moveTab(FL.select(backed, "controls"), 1, TABS).tab, "graphics");
  same("RB/LB wraps back from the first tab", FL.moveTab(FL.select(backed, "graphics"), -1, TABS).tab, "controls");
  same("the preview (select) changes only the tab", FL.select(backed, "display").level, FL.SIDE);

  var reopened = FL.open(backed, "display");
  same("reopening Settings puts focus on the tab list", reopened.level, FL.SIDE);
  same("reopening keeps the remembered rows", FL.rowFor(FL.select(reopened, "graphics")), "fxaa");

  same("the tab-list footer shows A Enter, B Back, LB RB Tabs", FL.footer(FL.SIDE).map(function (item) { return item.key + " " + item.label; }).join("|"), "A Enter|B Back|LB RB Tabs");
  same("the content footer shows A Select, B Tabs", FL.footer(FL.CONTENT).map(function (item) { return item.key + " " + item.label; }).join("|"), "A Select|B Tabs");

  // Preview and focus (js/focus-levels.js onContentFocus): drawing a tab's content never moves focus.
  var sidePreview = FL.start("saves");
  same("a focus event from rendering the preview keeps the tab list", FL.onContentFocus(sidePreview, true).level, FL.SIDE);
  same("a focus event from the player moves to the rows", FL.onContentFocus(sidePreview, false).level, FL.CONTENT);
  same("a focus event in the rows changes nothing", FL.onContentFocus(entered, false) === entered, true);
  same("a preview render inside the rows changes nothing", FL.onContentFocus(entered, true) === entered, true);

  // Entry (js/settings-model.js): A on Saves or Replays focuses the first row, or Import when the list is empty.
  var SM = root.GT4.settingsModel;
  same("entering Saves with rows focuses the first row", SM.libraryEntry(3), "row");
  same("entering Replays with one row focuses that row", SM.libraryEntry(1), "row");
  same("entering Saves with an empty list focuses Import", SM.libraryEntry(0), "import");

  // Value changes (js/settings-model.js): the row model holds the written value and visibility follows it.
  var model = {
    max_upscale: 4,
    rows: [
      { id: "upscale", current: "1080p", control: "dropdown",
        values: [{ value: "1080p", label: "1080p (3x)" }, { value: "4K", label: "4K (6x)" }] },
      { id: "sharp", current: 50, control: "slider", page: "basic", visible_when: { id: "upscale", equals: "4K" } },
      { id: "fxaa", current: false, control: "toggle", page: "basic" }
    ]
  };
  same("a dropdown shows the label of the chosen option", SM.optionLabel(model.rows[0].values, "4K"), "4K (6x)");
  same("a value without an option shows the raw value", SM.optionLabel(model.rows[0].values, "8K"), "8K");
  same("a row hidden by visible_when stays hidden at the old value", SM.rowVisible(model, model.rows[1], false), false);
  SM.applyChange(model, "upscale", "4K");
  same("the row model holds the dropdown value after a change", SM.findRow(model, "upscale").current, "4K");
  same("a row that depends on the changed row shows at once", SM.rowVisible(model, model.rows[1], false), true);
  SM.applyChange(model, "upscale", "1080p");
  same("the dependent row hides again when the value goes back", SM.rowVisible(model, model.rows[1], false), false);
  SM.applyChange(model, "fxaa", true);
  same("a toggle holds a boolean after a change", SM.findRow(model, "fxaa").current, true);
  same("a change to an unknown row changes nothing", SM.applyChange(model, "nope", 1), null);
  same("mod rows stay hidden while the feature is off", SM.rowVisible(model, { id: "m", page: "mods" }, false), false);

  // Overlapping reads: only the newest answer is drawn.
  var gate = SM.readGate();
  var firstRead = gate.ticket();
  var secondRead = gate.ticket();
  same("an older read is not applied", gate.isLatest(firstRead), false);
  same("the newest read is applied", gate.isLatest(secondRead), true);

  function run() {
    var list = document.getElementById("results");
    var passed = 0;
    cases.forEach(function (item) {
      if (item.pass) {
        passed += 1;
      }
      var li = document.createElement("li");
      li.className = "step";
      var label = document.createElement("span");
      label.className = "t";
      label.textContent = (item.pass ? "PASS  " : "FAIL  ") + item.name + (item.pass ? "" : " (" + item.detail + ")");
      li.appendChild(label);
      list.appendChild(li);
    });
    var summary = document.getElementById("summary");
    summary.textContent = passed + " of " + cases.length + " passed.";
    root.GT4_NAV_TEST_RESULT = { passed: passed, total: cases.length };
  }

  run();
})(window);
