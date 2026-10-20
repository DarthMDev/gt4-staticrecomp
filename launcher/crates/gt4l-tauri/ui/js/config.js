/*
 * UI feature flags. A flag set to false removes the feature from navigation
 * (tabs, LB/RB and Q/E cycling, routes) and hides its settings rows.
 * mods: the mod manager is release 1.1.0 (owner decision, 2026-10-16).
 */
(function (root) {
  "use strict";
  var GT4 = (root.GT4 = root.GT4 || {});
  GT4.features = {
    mods: false
  };
  // Home background slideshow (three slots, in order). Placeholders until the final 16:9 images arrive;
  // see ui/assets/backgrounds/README.md. Each slot has a CSS class bg-slide-1..3 in css/app.css.
  GT4.config = {
    backgrounds: [
      "assets/backgrounds/bg-01.png",
      "assets/backgrounds/bg-02.png",
      "assets/backgrounds/bg-03.png"
    ]
  };
})(window);
