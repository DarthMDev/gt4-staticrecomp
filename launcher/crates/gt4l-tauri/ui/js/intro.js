/*
 * The 1.3.0 intro (owner, 2026-10-23): a short animation, about 7 s, the first time the launcher opens on this version,
 * to say that GT4Recomp now runs Gran Turismo 4 Spec II. Light streaks, the GT4 logo, "spec-II" landing with a red bar
 * (lower case in a wide geometric face, as on the community Spec II cover: Michroma, SIL OFL, assets/Michroma-Regular.ttf),
 * one line of text, then the launcher. Any key, click or pad button skips it. Sound and music are made live with the
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
  var DURATION_MS = 7200;

  function reducedMotion() {
    return !!(root.matchMedia && root.matchMedia("(prefers-reduced-motion: reduce)").matches);
  }

  // ---------- sound: a whoosh, an engine rise, an impact and a rising chord, all synthesised ----------
  function playSound() {
    var Context = root.AudioContext || root.webkitAudioContext;
    if (!Context) {
      return null;
    }
    var ctx;
    try {
      ctx = new Context();
    } catch (error) {
      return null;
    }
    if (ctx.state === "suspended" && ctx.resume) {
      ctx.resume().catch(function () {});
    }
    var t0 = ctx.currentTime + 0.05;
    var master = ctx.createGain();
    master.gain.value = 0.32;
    master.connect(ctx.destination);

    function noiseBuffer(seconds) {
      var length = Math.floor(ctx.sampleRate * seconds);
      var buffer = ctx.createBuffer(1, length, ctx.sampleRate);
      var data = buffer.getChannelData(0);
      for (var i = 0; i < length; i++) {
        data[i] = Math.random() * 2 - 1;
      }
      return buffer;
    }

    function envelope(gainNode, at, attack, hold, release, peak) {
      var g = gainNode.gain;
      g.setValueAtTime(0.0001, at);
      g.exponentialRampToValueAtTime(peak, at + attack);
      g.setValueAtTime(peak, at + attack + hold);
      g.exponentialRampToValueAtTime(0.0001, at + attack + hold + release);
    }

    // Whoosh: filtered noise sweeping up, then away (the light streaks).
    function whoosh(at, length, from, to, peak) {
      var source = ctx.createBufferSource();
      source.buffer = noiseBuffer(length + 0.2);
      var filter = ctx.createBiquadFilter();
      filter.type = "bandpass";
      filter.Q.value = 1.2;
      filter.frequency.setValueAtTime(from, at);
      filter.frequency.exponentialRampToValueAtTime(to, at + length);
      var gain = ctx.createGain();
      envelope(gain, at, length * 0.45, 0, length * 0.55, peak);
      source.connect(filter);
      filter.connect(gain);
      gain.connect(master);
      source.start(at);
      source.stop(at + length + 0.2);
    }

    // Engine rise: two detuned saws through a low-pass that opens (a car passing before the logo).
    function engine(at, length) {
      var gain = ctx.createGain();
      envelope(gain, at, 0.4, length - 0.9, 0.5, 0.22);
      var filter = ctx.createBiquadFilter();
      filter.type = "lowpass";
      filter.frequency.setValueAtTime(220, at);
      filter.frequency.exponentialRampToValueAtTime(1400, at + length * 0.8);
      filter.connect(gain);
      gain.connect(master);
      [0, 7].forEach(function (detune) {
        var osc = ctx.createOscillator();
        osc.type = "sawtooth";
        osc.detune.value = detune;
        osc.frequency.setValueAtTime(48, at);
        osc.frequency.exponentialRampToValueAtTime(132, at + length * 0.85);
        osc.connect(filter);
        osc.start(at);
        osc.stop(at + length);
      });
    }

    // Impact: a falling sine thump plus a short bright noise burst ("SPEC II" lands).
    function impact(at) {
      var osc = ctx.createOscillator();
      osc.type = "sine";
      osc.frequency.setValueAtTime(130, at);
      osc.frequency.exponentialRampToValueAtTime(38, at + 0.5);
      var gain = ctx.createGain();
      envelope(gain, at, 0.005, 0.02, 0.6, 0.9);
      osc.connect(gain);
      gain.connect(master);
      osc.start(at);
      osc.stop(at + 0.7);
      var burst = ctx.createBufferSource();
      burst.buffer = noiseBuffer(0.4);
      var high = ctx.createBiquadFilter();
      high.type = "highpass";
      high.frequency.value = 2400;
      var burstGain = ctx.createGain();
      envelope(burstGain, at, 0.003, 0.01, 0.3, 0.35);
      burst.connect(high);
      high.connect(burstGain);
      burstGain.connect(master);
      burst.start(at);
      burst.stop(at + 0.4);
    }

    // A short theme: a minor chord that lifts to its major relative, soft saw pads through a low-pass.
    function pad(at, notes, length, peak) {
      var gain = ctx.createGain();
      envelope(gain, at, 0.35, length - 1.0, 0.65, peak);
      var filter = ctx.createBiquadFilter();
      filter.type = "lowpass";
      filter.frequency.setValueAtTime(900, at);
      filter.frequency.linearRampToValueAtTime(2600, at + length * 0.6);
      filter.connect(gain);
      gain.connect(master);
      notes.forEach(function (frequency, index) {
        [-6, 6].forEach(function (detune) {
          var osc = ctx.createOscillator();
          osc.type = index === 0 ? "triangle" : "sawtooth";
          osc.frequency.value = frequency;
          osc.detune.value = detune;
          osc.connect(filter);
          osc.start(at);
          osc.stop(at + length);
        });
      });
    }

    // A quick rising arpeggio after the impact (a short "ta-da").
    function arpeggio(at, notes, step) {
      notes.forEach(function (frequency, index) {
        var start = at + index * step;
        var osc = ctx.createOscillator();
        osc.type = "square";
        osc.frequency.value = frequency;
        var filter = ctx.createBiquadFilter();
        filter.type = "lowpass";
        filter.frequency.value = 3200;
        var gain = ctx.createGain();
        envelope(gain, start, 0.01, 0.05, 0.35, 0.08);
        osc.connect(filter);
        filter.connect(gain);
        gain.connect(master);
        osc.start(start);
        osc.stop(start + 0.5);
      });
    }

    whoosh(t0 + 0.0, 1.1, 300, 4200, 0.5);
    whoosh(t0 + 0.55, 0.9, 500, 6000, 0.35);
    engine(t0 + 0.2, 2.4);
    pad(t0 + 1.1, [110.0, 130.81, 164.81], 1.6, 0.16);   // A minor
    impact(t0 + 2.6);
    pad(t0 + 2.6, [130.81, 164.81, 196.0, 261.63], 4.0, 0.2); // C major, the lift
    arpeggio(t0 + 2.75, [523.25, 659.25, 783.99, 1046.5], 0.09);
    whoosh(t0 + 6.0, 1.0, 3800, 400, 0.3);
    master.gain.setValueAtTime(0.32, t0 + 6.2);
    master.gain.linearRampToValueAtTime(0.0001, t0 + 7.2);
    return ctx;
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
    if (state.audio && state.audio.close) {
      // Let the last sounds ring out under the fade, then free the audio device.
      setTimeout(function () { state.audio.close().catch(function () {}); }, 700);
    }
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
      h("div", { class: "intro-stage" }, [
        h("img", { class: "intro-logo", src: "assets/logo-gt4-white.png", alt: "Gran Turismo 4" }),
        h("div", { class: "intro-spec" }, letters),
        h("div", { class: "intro-bar", "aria-hidden": "true" }),
        h("div", { class: "intro-line", text: "Now playable in GT4Recomp" }),
        h("div", { class: "intro-sub", text: "Gran Turismo 4 Spec II 1.10" })
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
    active = { overlay: overlay, onKey: onKey, audio: reduced ? null : playSound(), timer: 0 };
    active.timer = setTimeout(finish, reduced ? 3200 : DURATION_MS);
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
