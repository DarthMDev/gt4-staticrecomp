/*
 * The 1.3.0 intro (owner, 2026-10-23): about 8 s in three scenes, the first time the launcher opens on this version, to
 * say that GT4Recomp now runs Gran Turismo 4 Spec II: "You asked for it." (light streaks), "We made it." (an impact),
 * then the GT4 logo with "spec-II" (lower case in a wide geometric face, as on the community Spec II cover: Michroma,
 * SIL OFL, assets/Michroma-Regular.ttf), "Natively recompiled" and the version, then the launcher. Any key, click or pad button skips it. Sound and music are made live with the
 * Web Audio API (no audio files, nothing third-party). Reduced motion keeps a plain fade and no sound.
 * Shown once per version: the version is remembered in localStorage (`gt4recomp.intro.seen`). The animation is pure
 * CSS on classes (no inline styles, CSP); the timing is in css/app.css ("intro" section).
 * GT4.intro.maybePlay(version) on the home page; GT4.intro.play() replays it (dev mock: ?intro=1).
 */
(function (root) {
  "use strict";
  var GT4 = (root.GT4 = root.GT4 || {});
  var h = GT4.ui.h;
  var SEEN_KEY = "gt4recomp.intro.seen";
  // The versions that have an intro. Later releases add theirs here.
  var INTRO_VERSIONS = { "1.3.0": true };
  var DURATION_MS = 8200;

  function reducedMotion() {
    return !!(root.matchMedia && root.matchMedia("(prefers-reduced-motion: reduce)").matches);
  }

  // ---------- the overlay ----------
  var active = null;

  function finish() {
    if (!active) {
      return;
    }
    var state = active;
    active = null;
    clearTimeout(state.timer);
    document.removeEventListener("keydown", state.onKey, true);
    state.overlay.classList.add("is-leaving");
    setTimeout(function () { state.overlay.remove(); }, 650);
  }

  function play() {
    if (active || !document.body) {
      return;
    }
    var reduced = reducedMotion();
    var streaks = [];
    for (var i = 1; i <= 7; i++) {
      streaks.push(h("span", { class: "intro-streak intro-streak-" + i }));
    }
    var letters = "spec-II".split("").map(function (letter, index) {
      return h("span", { class: "intro-letter intro-letter-" + (index + 1), text: letter });
    });
    var overlay = h("div", { class: "intro" + (reduced ? " is-reduced" : ""), role: "dialog", "aria-label": "Gran Turismo 4 Spec II is now supported" }, [
      h("div", { class: "intro-streaks", "aria-hidden": "true" }, streaks),
      h("div", { class: "intro-scene intro-scene-1", text: "You asked for it." }),
      h("div", { class: "intro-scene intro-scene-2", text: "We made it." }),
      h("div", { class: "intro-stage" }, [
        h("img", { class: "intro-logo", src: "assets/logo-gt4-white.png", alt: "Gran Turismo 4" }),
        h("div", { class: "intro-spec" }, letters),
        h("div", { class: "intro-bar", "aria-hidden": "true" }),
        h("div", { class: "intro-line", text: "Natively recompiled" }),
        h("div", { class: "intro-sub", text: "Now in GT4Recomp 1.3.0" })
      ]),
      h("div", { class: "intro-skip", text: "Press any key to skip" })
    ]);
    overlay.addEventListener("click", finish);
    var onKey = function (event) {
      event.preventDefault();
      event.stopImmediatePropagation();
      finish();
    };
    document.addEventListener("keydown", onKey, true);
    document.body.appendChild(overlay);
    active = { overlay: overlay, onKey: onKey, timer: 0 };
    active.timer = setTimeout(finish, reduced ? 4200 : DURATION_MS);
  }

  function seenVersion() {
    try {
      return root.localStorage.getItem(SEEN_KEY) || "";
    } catch (error) {
      return "";
    }
  }

  function markSeen(version) {
    try {
      root.localStorage.setItem(SEEN_KEY, version);
    } catch (error) {
      // private storage: the intro may show again, which is harmless
    }
  }

  // Plays the intro once for a version that has one.
  function maybePlay(version) {
    if (!INTRO_VERSIONS[version] || seenVersion() === version) {
      return false;
    }
    markSeen(version);
    play();
    return true;
  }

  // A pad button skips the intro too (pad-nav comes from the SDL3 service in Rust).
  if (GT4.backend && GT4.backend.listen) {
    GT4.backend.listen("pad-nav", function () {
      if (active) {
        finish();
      }
    });
  }

  GT4.intro = { maybePlay: maybePlay, play: play, skip: finish, isPlaying: function () { return !!active; } };
})(window);
