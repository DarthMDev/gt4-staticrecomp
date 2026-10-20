/*
 * Spatial focus picking for pad, arrows and Tab-less navigation.
 * Pure functions only: no DOM access, so ui/dev/nav-test.html can test them.
 *
 * A rect is {left, top, width, height}. pickNext returns the index of the
 * best candidate in direction `dir` from rects[fromIndex], or -1 when nothing
 * lies that way. A candidate must lie strictly ahead of the origin centre
 * along the direction axis; sideways offset is penalised twice as much as
 * distance ahead, so a control straight below wins over a nearer one far to
 * the side.
 */
(function (root) {
  "use strict";

  var AHEAD_MIN = 0.5; // px: ignore candidates on the same line as the origin centre

  function centreOf(rect) {
    return { x: rect.left + rect.width / 2, y: rect.top + rect.height / 2 };
  }

  function pickNext(rects, fromIndex, dir) {
    if (fromIndex < 0 || fromIndex >= rects.length) {
      return -1;
    }
    var origin = centreOf(rects[fromIndex]);
    var best = -1;
    var bestScore = Infinity;
    for (var i = 0; i < rects.length; i++) {
      if (i === fromIndex) {
        continue;
      }
      var target = centreOf(rects[i]);
      var dx = target.x - origin.x;
      var dy = target.y - origin.y;
      var ahead;
      var across;
      if (dir === "right") {
        ahead = dx;
        across = dy;
      } else if (dir === "left") {
        ahead = -dx;
        across = dy;
      } else if (dir === "down") {
        ahead = dy;
        across = dx;
      } else if (dir === "up") {
        ahead = -dy;
        across = dx;
      } else {
        return -1;
      }
      if (ahead <= AHEAD_MIN) {
        continue;
      }
      var score = ahead + 2 * Math.abs(across);
      if (score < bestScore) {
        bestScore = score;
        best = i;
      }
    }
    return best;
  }

  root.GT4 = root.GT4 || {};
  root.GT4.spatial = { pickNext: pickNext, centreOf: centreOf };
})(window);
