/*
 * Router and boot. Routes (location.hash):
 *   #/setup                  first run (also shown until setup is done)
 *   #/home                   Play and Settings
 *   #/settings/<tab>         graphics | display | controls | saves | replays | about (mods only when GT4.features.mods;
 *                            Advanced is a section of Graphics, so #/settings/advanced falls back to graphics)
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

  // Events from Rust (see ui/dev/README.md): update offers at launch, close confirmation, install progress,
  // and the end of a game session (the Home page shows the message).
  GT4.backend.listen("update-available", function (payload) {
    GT4.updates.offer(payload && payload.offers);
  });
  GT4.backend.listen("close-confirm", function (payload) {
    GT4.updates.confirmClose(payload && payload.message);
  });
  GT4.backend.listen("update-finished", function (payload) {
    GT4.ui.toast(payload && payload.message ? payload.message : "Update finished.", !(payload && payload.ok));
  });
  GT4.backend.listen("renderer-progress", function (payload) {
    // The setup page draws its own renderer row; these toasts are for the retry at a later launch and updates.
    if (parseRoute().name === "setup") {
      return;
    }
    if (payload && payload.step === "failed") {
      GT4.ui.toast("gt4-gs could not be installed. CoreGS 1.1 will be used.", true);
    } else if (payload && payload.step === "done") {
      GT4.ui.toast("gt4-gs is ready.");
    }
  });
  GT4.backend.listen("game-state", function (payload) {
    if (payload && payload.message) {
      GT4.ui.toast(payload.message, true);
    }
    if (payload && payload.running === false && root.location.hash !== "#/home") {
      GT4.go("#/home");
    }
  });

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
