/*
 * Home: cover art, logo, Play, Settings gear, and the version arrow under them. While the game runs, Play
 * becomes "Running · Stop". Polls app_state for game_running.
 * Commands: app_state, play, stop_game, select_game (js/games.js), setup_choose_disc and setup_start (Install).
 * Two games (TASK-101): a small arrow under Play opens the version menu, Normal (Gran Turismo 4) or Spec II (owner,
 * 2026-10-23). An installed game shows Play; a game that is not installed shows Install, which opens the disc picker and
 * runs that game's setup. The logo stays the Gran Turismo 4 logo for both.
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

  // The short names in the Version menu (owner, 2026-10-23); the full game names stay in Settings > Games.
  var VERSION_LABELS = { gt4: "Normal", spec2: "Spec II" };

  function chevronIcon() {
    var svg = document.createElementNS("http://www.w3.org/2000/svg", "svg");
    svg.setAttribute("width", "18");
    svg.setAttribute("height", "18");
    svg.setAttribute("viewBox", "0 0 24 24");
    svg.setAttribute("fill", "none");
    svg.setAttribute("stroke", "currentColor");
    svg.setAttribute("stroke-width", "1.8");
    svg.setAttribute("aria-hidden", "true");
    var path = document.createElementNS("http://www.w3.org/2000/svg", "path");
    path.setAttribute("d", "M6 9l6 6 6-6");
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
      // The version arrow under Play, with the current version's name beside it, and its menu (Normal, Spec II). The menu
      // is a small list that opens downwards (owner, 2026-10-23).
      var versionName = h("span", { class: "version-name", text: VERSION_LABELS.gt4 });
      var versionToggle = h("button", {
        class: "version-toggle",
        type: "button",
        "aria-haspopup": "listbox",
        "aria-expanded": "false",
        "aria-label": "Version",
        title: "Version"
      }, [versionName, chevronIcon()]);
      var versionMenu = h("div", { class: "version-menu", role: "listbox", "aria-label": "Version" });
      versionMenu.hidden = true;
      var versionPicker = h("div", { class: "version-picker" }, [versionMenu, versionToggle]);
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

      function menuOpen() {
        return !versionMenu.hidden;
      }

      function closeMenu(focusToggle) {
        if (!menuOpen()) {
          return;
        }
        versionMenu.hidden = true;
        versionToggle.setAttribute("aria-expanded", "false");
        if (focusToggle) {
          versionToggle.focus({ preventScroll: true });
        }
      }

      function openMenu() {
        if (isRunning || gameList.length < 2) {
          return;
        }
        versionMenu.hidden = false;
        versionToggle.setAttribute("aria-expanded", "true");
        var current = versionMenu.querySelector(".version-option.is-selected") || versionMenu.querySelector(".version-option");
        if (current) {
          current.focus({ preventScroll: true });
        }
      }

      // The Version menu and the main button follow the game list (rebuilt only when it changed).
      function paintGames() {
        var signature = JSON.stringify([selectedId, isRunning, gameList.map(function (game) { return [game.id, game.installed]; })]);
        if (signature === gameSignature) {
          return;
        }
        gameSignature = signature;
        var game = selectedGame();
        installedNow = !game || game.installed;
        page.classList.toggle("is-spec2", selectedId === "spec2");
        versionToggle.disabled = isRunning || gameList.length < 2;
        versionName.textContent = VERSION_LABELS[selectedId] || (game ? game.name : "");
        versionToggle.setAttribute("title", "Version: " + versionName.textContent);
        playButton.textContent = installedNow ? "Play" : "Install";
        playButton.classList.toggle("is-install", !installedNow);
        playButton.setAttribute("title", installedNow || !game || !game.reason ? "" : game.reason);
        var focusedId = document.activeElement && document.activeElement.dataset ? document.activeElement.dataset.gameId : null;
        GT4.ui.clear(versionMenu);
        gameList.forEach(function (item) {
          var on = item.id === selectedId;
          var option = h("button", {
            class: "version-option" + (on ? " is-selected" : "") + (item.installed ? "" : " not-installed"),
            type: "button",
            role: "option",
            "aria-selected": String(on),
            title: item.name,
            data: { gameId: item.id },
            on: { click: function () { closeMenu(true); chooseGame(item.id); } }
          }, [
            h("span", { class: "version-option-name", text: VERSION_LABELS[item.id] || item.name }),
            item.installed ? null : h("small", { text: "Not installed" })
          ]);
          versionMenu.appendChild(option);
          if (focusedId === item.id && menuOpen()) {
            option.focus({ preventScroll: true });
          }
        });
        if (isRunning) {
          closeMenu(false);
        }
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

      // LB / RB: the other game (there are two).
      function cycleGame(delta) {
        if (gameList.length < 2 || isRunning) {
          return;
        }
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
      versionToggle.addEventListener("click", function () {
        if (menuOpen()) {
          closeMenu(true);
        } else {
          openMenu();
        }
      });
      // Escape closes the menu; a click anywhere else closes it too.
      versionMenu.addEventListener("keydown", function (event) {
        if (event.key === "Escape") {
          event.preventDefault();
          closeMenu(true);
        }
      });
      function closeOnOutsideClick(event) {
        if (menuOpen() && !versionPicker.contains(event.target)) {
          closeMenu(false);
        }
      }
      document.addEventListener("click", closeOnOutsideClick);

      // Settings: a square button beside Play with the same look and focus (owner, 1.2.1).
      var gear = h("button", {
        class: "play play-square",
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
      page.appendChild(h("div", { class: "home-bottom" }, [
        logoImage,
        h("div", { class: "home-actions" }, [playButton, running, gear]),
        versionPicker
      ]));
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
          document.removeEventListener("click", closeOnOutsideClick);
          document.body.classList.remove("is-home");
        }
      };
    }
  };
})(window);
