/*
 * Router and boot. Routes (location.hash):
 *   #/setup                  first run (also shown until setup is done)
 *   #/home                   Play and Settings
 *   #/settings/<tab>         graphics | controls | saves | replays | about (mods only when GT4.features.mods;
 *                            Display and Advanced are sections of Graphics, so #/settings/display and
 *                            #/settings/advanced fall back to graphics)
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
    var step = payload && payload.step;
    // TASK-089 extension (1.2.0): a switch in Settings shows its progress inline, under the Renderer row.
    var labels = { listing: "Finding GSdx…", downloading: "Downloading GSdx…", installing: "Installing…" };
    if (GT4.setRendererStatus && labels[step]) {
      GT4.setRendererStatus(labels[step]);
    }
    if (step === "failed") {
      GT4.ui.toast("GSdx could not be installed. CoreGS 1.1 will be used.", true);
    } else if (step === "done") {
      GT4.ui.toast("gt4-gs is ready.");
    }
  });
  GT4.backend.listen("renderer-switched", function (payload) {
    // Choosing GSdx in Settings finished: the notice, and the page shows the saved choice again.
    if (GT4.setRendererStatus) {
      GT4.setRendererStatus("");
    }
    GT4.ui.toast(payload && payload.message ? payload.message : "GSdx", !(payload && payload.ok));
    if (parseRoute().name === "settings") {
      GT4.go(root.location.hash);
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

  // TASK-091 (1.2.1): the first start moves the files of GT4Recomp 1.2.0 to the new folders. While the game files
  // move (in the background), a small status line shows the progress; Play and setup wait. The result is a toast,
  // shown once per notice text.
  var migrationBox = null;
  var shownNotice = "";
  // The status box: a label with the percentage on the right and a bar under it. Without a byte count (a move
  // inside one drive) the bar slides and no percentage is shown.
  function renderMigration(box, migration) {
    var done = migration.bytes_done || 0;
    var total = migration.bytes_total || 0;
    var percent = total > 0 ? Math.min(100, Math.floor((done * 100) / total)) : -1;
    if (!box.firstChild) {
      var head = document.createElement("div");
      head.className = "migration-head";
      head.appendChild(document.createElement("span"));
      head.appendChild(document.createElement("span"));
      var bar = document.createElement("div");
      bar.className = "migration-bar";
      var fill = document.createElement("div");
      fill.className = "migration-fill";
      bar.appendChild(fill);
      box.appendChild(head);
      box.appendChild(bar);
    }
    var label = box.firstChild.firstChild;
    var number = box.firstChild.lastChild;
    var barNode = box.lastChild;
    label.textContent = "Moving " + (migration.item || "game files") + " to the install folder…";
    number.textContent = percent >= 0 ? percent + "%" : "";
    barNode.classList.toggle("indeterminate", percent < 0);
    barNode.firstChild.style.width = percent >= 0 ? percent + "%" : "";
  }
  function showMigration(appState) {
    var migration = appState && appState.migration;
    if (!migration) {
      return;
    }
    if (migration.running) {
      if (!migrationBox) {
        migrationBox = document.createElement("div");
        migrationBox.className = "migration";
        migrationBox.setAttribute("role", "status");
        migrationBox.setAttribute("aria-live", "polite");
        document.body.appendChild(migrationBox);
      }
      renderMigration(migrationBox, migration);
      root.setTimeout(function () {
        GT4.backend.call("app_state").then(showMigration, function () {});
      }, 700);
      return;
    }
    if (migrationBox) {
      migrationBox.parentNode.removeChild(migrationBox);
      migrationBox = null;
    }
    if (migration.notice && migration.notice !== shownNotice) {
      shownNotice = migration.notice;
      GT4.ui.toast(migration.notice, !!migration.problem);
    }
  }
  GT4.showMigration = showMigration;

  GT4.backend.call("app_state").then(function (appState) {
    showMigration(appState);
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
