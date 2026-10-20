/*
 * Router and boot. Routes (location.hash):
 *   #/setup                  first run (also shown until setup is done)
 *   #/home                   Play and Settings
 *   #/settings/<tab>         graphics | advanced | display | controls | saves | replays | about (mods only when GT4.features.mods)
 * Each page mounts into #app and sets GT4.hooks for the input layer.
 */
(function (root) {
  "use strict";
  var GT4 = (root.GT4 = root.GT4 || {});
  var app = document.getElementById("app");
  var current = null;

  function parseRoute() {
    var raw = root.location.hash.replace(/^#\/?/, "");
    var parts = raw.split("/");
    return { name: parts[0] || "", tab: parts[1] || "" };
  }

  function go(hash) {
    if (root.location.hash === hash) {
      render();
    } else {
      root.location.hash = hash;
    }
  }
  GT4.go = go;

  function render() {
    if (current && current.destroy) {
      current.destroy();
    }
    current = null;
    GT4.ui.clear(app);
    GT4.hooks = {};
    GT4.ui.setHints([]);
    GT4.capturing = false;

    var route = parseRoute();
    var name = route.name === "setup" || route.name === "settings" ? route.name : "home";
    document.body.classList.toggle("is-home", name === "home");

    var page = GT4.pages[name];
    if (name === "settings") {
      current = page.mount(app, { tab: route.tab || "graphics" });
    } else {
      current = page.mount(app);
    }
  }

  root.addEventListener("hashchange", render);

  GT4.backend.call("app_state").then(function (appState) {
    var route = parseRoute();
    if (!route.name) {
      go(appState.setup && appState.setup.state === "done" ? "#/home" : "#/setup");
    } else {
      render();
    }
  }, function (err) {
    render();
    GT4.ui.toast(GT4.errorText(err), true);
  });
})(window);
