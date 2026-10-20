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
