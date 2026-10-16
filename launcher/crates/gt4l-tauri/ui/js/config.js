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
})(window);
