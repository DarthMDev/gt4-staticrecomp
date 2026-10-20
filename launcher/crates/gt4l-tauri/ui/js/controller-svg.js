/*
 * Original vector drawing of a PS2-style pad. No Sony artwork: the shapes are
 * simple geometry (rounded rectangles, circles, and the four face glyphs drawn
 * as plain strokes). Each control is a focusable group with data-control.
 *
 * GT4.controllerSvg(onSelect) -> {svg, select(id), setMapped(id, bool), setCapturing(id, bool)}
 */
(function (root) {
  "use strict";
  var GT4 = (root.GT4 = root.GT4 || {});
  var NS = "http://www.w3.org/2000/svg";

  // Controls in drawing order. kind: "rect" | "circle" | "face" (circle with glyph).
  var CONTROLS = [
    { id: "l2", name: "L2", kind: "rect", x: 130, y: 18, w: 100, h: 30, rx: 10, text: "L2", textY: 38 },
    { id: "r2", name: "R2", kind: "rect", x: 370, y: 18, w: 100, h: 30, rx: 10, text: "R2", textY: 38 },
    { id: "l1", name: "L1", kind: "rect", x: 120, y: 56, w: 120, h: 24, rx: 8, text: "L1", textY: 73 },
    { id: "r1", name: "R1", kind: "rect", x: 360, y: 56, w: 120, h: 24, rx: 8, text: "R1", textY: 73 },
    { id: "up", name: "D-pad up", kind: "path", d: "M138 180 L138 160 L150 148 L162 160 L162 180 Z" },
    { id: "down", name: "D-pad down", kind: "path", d: "M138 200 L138 220 L150 232 L162 220 L162 200 Z" },
    { id: "left", name: "D-pad left", kind: "path", d: "M140 178 L120 178 L108 190 L120 202 L140 202 Z" },
    { id: "right", name: "D-pad right", kind: "path", d: "M160 178 L180 178 L192 190 L180 202 L160 202 Z" },
    { id: "triangle", name: "Triangle", kind: "face", cx: 450, cy: 152, r: 15, glyph: "triangle" },
    { id: "circle", name: "Circle", kind: "face", cx: 482, cy: 190, r: 15, glyph: "circle" },
    { id: "cross", name: "Cross", kind: "face", cx: 450, cy: 228, r: 15, glyph: "cross" },
    { id: "square", name: "Square", kind: "face", cx: 418, cy: 190, r: 15, glyph: "square" },
    { id: "select", name: "SELECT", kind: "rect", x: 232, y: 170, w: 28, h: 14, rx: 7, text: "SEL", textY: 188 },
    { id: "start", name: "START", kind: "rect", x: 340, y: 170, w: 28, h: 14, rx: 7, text: "STA", textY: 188 },
    { id: "analog", name: "ANALOG", kind: "circle", cx: 300, cy: 200, r: 11 },
    { id: "l3", name: "L3 (left stick)", kind: "circle", cx: 220, cy: 262, r: 28 },
    { id: "r3", name: "R3 (right stick)", kind: "circle", cx: 380, cy: 262, r: 28 }
  ];

  // Bounding boxes of the D-pad arms (paths have no rect of their own), used for touch hit areas.
  var PATH_BOXES = {
    up: { x: 138, y: 148, w: 24, h: 32 },
    down: { x: 138, y: 200, w: 24, h: 32 },
    left: { x: 108, y: 178, w: 32, h: 24 },
    right: { x: 160, y: 178, w: 32, h: 24 }
  };
  // Touch targets: at least 56 units (about 48 CSS px at the drawing's normal scale) around each control.
  var MIN_HIT = 56;

  function hitBox(control) {
    var box;
    if (control.kind === "rect") {
      box = { x: control.x, y: control.y, w: control.w, h: control.h };
    } else if (control.kind === "path") {
      var arm = PATH_BOXES[control.id];
      box = { x: arm.x, y: arm.y, w: arm.w, h: arm.h };
    } else {
      box = { x: control.cx - control.r, y: control.cy - control.r, w: control.r * 2, h: control.r * 2 };
    }
    var cx = box.x + box.w / 2;
    var cy = box.y + box.h / 2;
    var w = Math.max(box.w, MIN_HIT);
    var h = Math.max(box.h, MIN_HIT);
    return { x: cx - w / 2, y: cy - h / 2, w: w, h: h };
  }

  var BODY_PATH =
    "M120 96 H480 C540 96 572 130 580 190 L592 300 C598 350 566 368 532 352 " +
    "C506 340 492 316 478 292 L440 246 H160 L122 292 C108 316 94 340 68 352 " +
    "C34 368 2 350 8 300 L20 190 C28 130 60 96 120 96 Z";

  function el(tag, attrs, parent) {
    var node = document.createElementNS(NS, tag);
    Object.keys(attrs || {}).forEach(function (key) {
      node.setAttribute(key, String(attrs[key]));
    });
    if (parent) {
      parent.appendChild(node);
    }
    return node;
  }

  function drawGlyph(group, glyph, cx, cy) {
    if (glyph === "triangle") {
      el("path", { class: "glyph", d: "M" + (cx) + " " + (cy - 7) + " L" + (cx + 7) + " " + (cy + 6) + " L" + (cx - 7) + " " + (cy + 6) + " Z" }, group);
    } else if (glyph === "circle") {
      el("circle", { class: "glyph", cx: cx, cy: cy, r: 7 }, group);
    } else if (glyph === "cross") {
      el("path", { class: "glyph", d: "M" + (cx - 6) + " " + (cy - 6) + " L" + (cx + 6) + " " + (cy + 6) + " M" + (cx + 6) + " " + (cy - 6) + " L" + (cx - 6) + " " + (cy + 6) }, group);
    } else if (glyph === "square") {
      el("rect", { class: "glyph", x: cx - 6, y: cy - 6, width: 12, height: 12 }, group);
    }
  }

  function build(onSelect) {
    var svg = el("svg", { viewBox: "0 0 600 380", class: "pad-svg", role: "group", "aria-label": "DualShock 2 controller" });
    el("path", { class: "body", d: BODY_PATH }, svg);
    // Decorative ANALOG LED (not a control).
    el("circle", { class: "pad-led", cx: 300, cy: 184, r: 2.5 }, svg);

    var groups = {};
    CONTROLS.forEach(function (control) {
      var group = el("g", {
        class: "pad-ctl",
        "data-control": control.id,
        "data-focusable": "",
        tabindex: "0",
        role: "button",
        "aria-label": control.name
      }, svg);
      var title = el("title", {}, group);
      title.textContent = control.name;
      var hit = hitBox(control);
      el("rect", { class: "hit", x: hit.x, y: hit.y, width: hit.w, height: hit.h }, group);

      if (control.kind === "path") {
        el("path", { class: "shape", d: control.d }, group);
      } else if (control.kind === "rect") {
        el("rect", { class: "shape", x: control.x, y: control.y, width: control.w, height: control.h, rx: control.rx }, group);
        if (control.text) {
          var text = el("text", { class: "label", x: control.x + control.w / 2, y: control.textY }, group);
          text.textContent = control.text;
        }
      } else if (control.kind === "circle") {
        el("circle", { class: "shape", cx: control.cx, cy: control.cy, r: control.r }, group);
      } else {
        el("circle", { class: "shape", cx: control.cx, cy: control.cy, r: control.r }, group);
        drawGlyph(group, control.glyph, control.cx, control.cy);
      }
      group.addEventListener("click", function () {
        onSelect(control.id);
      });
      groups[control.id] = group;
    });

    function select(id) {
      Object.keys(groups).forEach(function (key) {
        groups[key].classList.toggle("is-selected", key === id);
      });
      if (groups[id]) {
        groups[id].focus({ preventScroll: true });
      }
    }

    function setMapped(id, mapped) {
      if (groups[id]) {
        groups[id].classList.toggle("is-mapped", !!mapped);
      }
    }

    function setCapturing(id, capturing) {
      Object.keys(groups).forEach(function (key) {
        groups[key].classList.toggle("is-capturing", capturing && key === id);
      });
    }

    return { svg: svg, select: select, setMapped: setMapped, setCapturing: setCapturing, groups: groups };
  }

  GT4.controllerSvg = build;
  GT4.controllerControls = CONTROLS.map(function (control) { return { id: control.id, name: control.name }; });
})(window);
