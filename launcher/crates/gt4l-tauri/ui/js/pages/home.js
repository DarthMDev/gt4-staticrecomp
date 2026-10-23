/*
 * Home: cover art; the logo, Play and the version switch centred in one column; the Settings gear at the top left
 * (owner, 2026-10-23). While the game runs, Play
 * becomes "Running · Stop". Polls app_state for game_running.
 * Commands: app_state, play, stop_game, select_game (js/games.js), setup_choose_disc and setup_start (Install).
 * Two games (TASK-101): under Play the version's name, Normal (Gran Turismo 4) or Spec II, between two arrows; an
 * arrow switches to the next version in an endless loop and the name slides in from that side (owner, 2026-10-23). An
 * installed game shows Play; a game that is not installed shows Install, which opens the disc picker and runs that
 * game's setup. The logo stays the Gran Turismo 4 logo for both.
 * Patch notes (TASK-099): a glass card on the right shows this version's release notes (release_notes, the Markdown file
 * embedded in the launcher), rendered by js/markdown.js; its footer opens the GitHub releases page (open_releases_page).
 * Controller: A or Start = Play (or Install), Y = Settings, LB / RB = the other game. Hints show only once a pad is used.
 * Play is glass when idle (no focus): it takes the solid white state only when focused or hovered.
 */
(function (root) {
  "use strict";
  var GT4 = (root.GT4 = root.GT4 || {});
  var h = GT4.ui.h;
  var POLL_MS = 1000;

  function call(command, args) {
    return GT4.backend.call(command, args);
  }

  var GEAR_PATH = "M19.4 15a1.7 1.7 0 0 0 .3 1.8l.1.1a2 2 0 1 1-2.8 2.8l-.1-.1a1.7 1.7 0 0 0-1.8-.3 1.7 1.7 0 0 0-1 1.5V21a2 2 0 1 1-4 0v-.1a1.7 1.7 0 0 0-1.1-1.5 1.7 1.7 0 0 0-1.8.3l-.1.1a2 2 0 1 1-2.8-2.8l.1-.1a1.7 1.7 0 0 0 .3-1.8 1.7 1.7 0 0 0-1.5-1H3a2 2 0 1 1 0-4h.1a1.7 1.7 0 0 0 1.5-1.1 1.7 1.7 0 0 0-.3-1.8l-.1-.1a2 2 0 1 1 2.8-2.8l.1.1a1.7 1.7 0 0 0 1.8.3H9a1.7 1.7 0 0 0 1-1.5V3a2 2 0 1 1 4 0v.1a1.7 1.7 0 0 0 1 1.5 1.7 1.7 0 0 0 1.8-.3l.1-.1a2 2 0 1 1 2.8 2.8l-.1.1a1.7 1.7 0 0 0-.3 1.8V9a1.7 1.7 0 0 0 1.5 1H21a2 2 0 1 1 0 4h-.1a1.7 1.7 0 0 0-1.5 1z";

  function gearIcon() {
    var svg = document.createElementNS("http://www.w3.org/2000/svg", "svg");
    svg.setAttribute("width", "22");
    svg.setAttribute("height", "22");
    svg.setAttribute("viewBox", "0 0 24 24");
    svg.setAttribute("fill", "none");
    svg.setAttribute("stroke", "currentColor");
    svg.setAttribute("stroke-width", "1.5");
    svg.setAttribute("aria-hidden", "true");
    var circle = document.createElementNS("http://www.w3.org/2000/svg", "circle");
    circle.setAttribute("cx", "12");
    circle.setAttribute("cy", "12");
    circle.setAttribute("r", "3");
    var path = document.createElementNS("http://www.w3.org/2000/svg", "path");
    path.setAttribute("d", GEAR_PATH);
    svg.appendChild(circle);
    svg.appendChild(path);
    return svg;
  }

  // The short names of the versions (owner, 2026-10-23); the full game names stay in Settings > Games.
  var VERSION_LABELS = { gt4: "Normal", spec2: "Spec II" };

  // A small chevron pointing left (-1) or right (+1).
  function chevronIcon(direction) {
    var svg = document.createElementNS("http://www.w3.org/2000/svg", "svg");
    svg.setAttribute("width", "16");
    svg.setAttribute("height", "16");
    svg.setAttribute("viewBox", "0 0 24 24");
    svg.setAttribute("fill", "none");
    svg.setAttribute("stroke", "currentColor");
    svg.setAttribute("stroke-width", "1.8");
    svg.setAttribute("aria-hidden", "true");
    var path = document.createElementNS("http://www.w3.org/2000/svg", "path");
    path.setAttribute("d", direction < 0 ? "M15 6l-6 6 6 6" : "M9 6l6 6-6 6");
    svg.appendChild(path);
    return svg;
  }

  GT4.pages = GT4.pages || {};
  GT4.pages.home = {
    mount: function (root) {
      var page = h("div", { class: "stage home" });
      var playButton = h("button", { class: "play", type: "button" }, "Play");
      var gameList = [];
      var selectedId = "gt4";
      var installedNow = true;
      var gameSignature = "";
      var logoImage = h("img", { class: "home-logo", src: "assets/logo-gt4-white.png", alt: "Gran Turismo 4" });
      // The version switch under Play: the version's name between two arrows (owner, 2026-10-23). On a switch the old name
      // slides out on one side while the new one slides in from the other, inside a clipped window.
      var versionWindow = h("span", { class: "version-window", "aria-live": "polite" });
      var versionName = h("span", { class: "version-name", text: VERSION_LABELS.gt4 });
      versionWindow.appendChild(versionName);
      var versionPrev = h("button", { class: "version-arrow", type: "button", "aria-label": "Previous version", title: "Previous version" },
        [chevronIcon(-1)]);
      var versionNext = h("button", { class: "version-arrow", type: "button", "aria-label": "Next version", title: "Next version" },
        [chevronIcon(1)]);
      var versionPicker = h("div", { class: "version-picker", role: "group", "aria-label": "Version" },
        [versionPrev, versionWindow, versionNext]);
      var paintedId = null;
      var slideFrom = 0;
      var stopButton = h("button", { class: "btn", type: "button" }, "Stop");
      var stateText = h("span", { class: "state", text: "Running" });
      var running = h("div", { class: "running" }, [h("span", { class: "dot" }), stateText, stopButton]);
      var timer = null;
      var isRunning = false;

      function paint(nowRunning) {
        isRunning = nowRunning;
        page.classList.toggle("is-running", nowRunning);
      }

      function selectedGame() {
        for (var i = 0; i < gameList.length; i++) {
          if (gameList[i].id === selectedId) {
            return gameList[i];
          }
        }
        return null;
      }

      // A switch: the shown name leaves towards the opposite side while the new name comes in from the side of the arrow
      // used (+1 = right arrow: the new name comes from the right). Without a direction the text just changes.
      function showName(text, direction) {
        if (!direction || versionName.textContent === text) {
          versionName.textContent = text;
          return;
        }
        var leaving = versionName;
        versionWindow.querySelectorAll(".version-name.is-leaving").forEach(function (old) { old.remove(); });
        versionName = h("span", { class: "version-name " + (direction > 0 ? "enter-from-right" : "enter-from-left"), text: text });
        leaving.classList.remove("enter-from-left", "enter-from-right");
        leaving.classList.add("is-leaving", direction > 0 ? "leave-to-left" : "leave-to-right");
        versionWindow.appendChild(versionName);
        var done = function () { leaving.remove(); };
        leaving.addEventListener("animationend", done, { once: true });
        setTimeout(done, 600);  // reduced motion: no animationend
      }

      // The version switch and the main button follow the game list (rebuilt only when it changed).
      function paintGames() {
        var signature = JSON.stringify([selectedId, isRunning, gameList.map(function (game) { return [game.id, game.installed]; })]);
        if (signature === gameSignature) {
          return;
        }
        gameSignature = signature;
        var game = selectedGame();
        installedNow = !game || game.installed;
        page.classList.toggle("is-spec2", selectedId === "spec2");
        var locked = isRunning || gameList.length < 2;
        versionPrev.disabled = locked;
        versionNext.disabled = locked;
        var label = VERSION_LABELS[selectedId] || (game ? game.name : "");
        showName(label, paintedId !== null && paintedId !== selectedId ? (slideFrom || 1) : 0);
        versionWindow.setAttribute("title", game ? game.name : "");
        paintedId = selectedId;
        slideFrom = 0;
        playButton.textContent = installedNow ? "Play" : "Install";
        playButton.classList.toggle("is-install", !installedNow);
        playButton.setAttribute("title", installedNow || !game || !game.reason ? "" : game.reason);
      }

      function applyGames(view) {
        if (view && Array.isArray(view.games)) {
          gameList = view.games;
          selectedId = view.selected || view.selected_game || selectedId;
          paintGames();
        }
      }

      function chooseGame(id) {
        if (id === selectedId || isRunning) {
          return;
        }
        GT4.games.select(id).then(applyGames, function (err) {
          GT4.ui.toast(GT4.errorText(err), true);
        });
      }

      // The arrows and LB / RB: the next version on that side, in an endless loop.
      function cycleGame(delta) {
        if (gameList.length < 2 || isRunning) {
          return;
        }
        slideFrom = delta;
        var at = 0;
        gameList.forEach(function (game, index) {
          if (game.id === selectedId) {
            at = index;
          }
        });
        chooseGame(gameList[(at + delta + gameList.length) % gameList.length].id);
      }

      // Install: the disc picker for the selected game, then its setup on the Setup page.
      function install() {
        GT4.games.installGame(selectedId, function () {
          GT4.go("#/setup");
        });
      }

      function play() {
        if (isRunning) {
          return;
        }
        if (!installedNow) {
          install();
          return;
        }
        paint(true);
        call("play").then(function () {
          // Follow the next poll for the real state.
        }, function (err) {
          paint(false);
          GT4.ui.toast(GT4.errorText(err), true);
          // Game files missing (the data folder was cleaned): the button becomes Install for that game.
          call("app_state").then(function (appState) {
            applyGames(appState);
            if (!appState.any_installed && appState.setup && appState.setup.state !== "done") {
              GT4.go("#/setup");
            }
          }, function () {});
        });
      }

      function stop() {
        call("stop_game").then(function () {
          paint(false);
        }, function (err) {
          GT4.ui.toast(GT4.errorText(err), true);
        });
      }

      playButton.addEventListener("click", play);
      stopButton.addEventListener("click", stop);
      versionPrev.addEventListener("click", function () { cycleGame(-1); });
      versionNext.addEventListener("click", function () { cycleGame(1); });

      // Settings: the plain gear at the top left (owner, 2026-10-23), no box until hovered or focused.
      var gear = h("button", {
        class: "gear",
        type: "button",
        "aria-label": "Settings",
        title: "Settings",
        on: { click: function () { GT4.go("#/settings/graphics"); } }
      }, [gearIcon()]);

      // Slideshow: one img per slot (bg-slide-1..6 crossfade in CSS). The slots come from GT4.config.
      // At most six slots: css/app.css defines bg-slide-1..6.
      var backgrounds = ((GT4.config && GT4.config.backgrounds) || []).slice(0, 6);
      var slides = backgrounds.map(function (src, index) {
        return h("img", { class: "bg-slide bg-slide-" + (index + 1), src: src, alt: "" });
      });
      page.appendChild(h("div", { class: "art" }, slides.concat([h("div", { class: "shade" })])));
      var versionLabel = h("div", { class: "home-version", text: "" });
      page.appendChild(versionLabel);
      call("about_info").then(function (info) {
        versionLabel.textContent = "GT4Recomp " + (info.version || "");
      }, function () {});
      // The patch notes card (TASK-099). It scrolls on its own; wide windows only (css/app.css hides it when narrow).
      var notesBody = h("div", { class: "home-notes-body", tabindex: "0", "aria-label": "Release notes" });
      var notesVersion = h("span", { class: "home-notes-version", text: "" });
      var notesCard = h("aside", { class: "home-notes", "aria-label": "What's new" }, [
        h("header", { class: "home-notes-head" }, [h("span", { class: "home-notes-title", text: "What's new" }), notesVersion]),
        notesBody,
        h("button", {
          class: "home-notes-more",
          type: "button",
          on: { click: function () { call("open_releases_page").catch(function (err) { GT4.ui.toast(GT4.errorText(err), true); }); } }
        }, "All release notes on GitHub")
      ]);
      notesCard.hidden = true;
      page.appendChild(notesCard);
      call("release_notes").then(function (notes) {
        if (!notes || !notes.markdown) {
          return;
        }
        notesVersion.textContent = notes.version || "";
        notesBody.appendChild(GT4.markdown.render(notes.markdown, { skipTitle: true }));
        notesCard.hidden = false;
      }, function () {});
      page.appendChild(h("div", { class: "home-bottom" }, [
        logoImage,
        h("div", { class: "home-actions" }, [playButton, running]),
        versionPicker
      ]));
      page.appendChild(gear);
      root.appendChild(page);
      document.body.classList.add("is-home");

      function poll() {
        call("app_state").then(function (appState) {
          var was = isRunning;
          paint(!!appState.game_running);
          if (was !== isRunning) {
            gameSignature = "";
          }
          applyGames(appState);
        }, function () {});
      }
      poll();
      timer = setInterval(poll, POLL_MS);

      // Dev harness only (dev/mock-data.js sets GT4.devFocus from ?focus=play): focus Play for screenshots.
      if (GT4.devFocus === "play") {
        playButton.focus({ preventScroll: true });
      }

      GT4.hooks.start = play;
      GT4.hooks.y = function () { GT4.go("#/settings/graphics"); };
      GT4.hooks.tabs = cycleGame;
      GT4.ui.setHints([
        { key: "A", label: "Select", run: play },
        { key: "LB / RB", label: "Version", run: function () { cycleGame(1); } },
        { key: "Y", label: "Settings", run: function () { GT4.go("#/settings/graphics"); } }
      ]);

      return {
        destroy: function () {
          clearInterval(timer);
          document.body.classList.remove("is-home");
        }
      };
    }
  };
})(window);
