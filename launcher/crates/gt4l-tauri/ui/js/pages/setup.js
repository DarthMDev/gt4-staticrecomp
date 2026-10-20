/*
 * Setup page. Polls app_state while it is open and draws the state it reports.
 *
 * app_state().setup = {
 *   state: "needed" | "checking" | "wrong_disc" | "done",
 *   step: "checking" | "preparing" | "card",   (while checking)
 *   progress: 0..1,                            (checking step)
 *   disc_name: "Gran Turismo 4 · USA · v2.00",
 *   problem: "text shown for wrong_disc"
 * }
 * "needed" starts setup once (setup_start). Commands: setup_choose_disc,
 * setup_cancel, play.
 */
(function (root) {
  "use strict";
  var GT4 = (root.GT4 = root.GT4 || {});
  var h = GT4.ui.h;

  var DEFAULT_DISC = "Gran Turismo 4 · USA · v2.00";
  var DEFAULT_PROBLEM = "Only the USA v2.00 disc of Gran Turismo 4 (SCUS-97328) works. PAL, Japanese and Spec II discs are not supported.";
  var ORDER = ["checking", "preparing", "card"];
  var POLL_MS = 600;

  function call(command, args) {
    return GT4.backend.call(command, args);
  }

  function stepRow(number, label, detail, status, progressPct) {
    var children = [
      h("span", { class: "n", text: number }),
      h("span", null, [
        h("span", { class: "t", text: label }),
        detail ? h("span", { class: "d", text: detail }) : null,
        progressPct !== null ? barNode(progressPct) : null
      ])
    ];
    var value = "";
    if (status === "ok") {
      value = "✓";
    } else if (progressPct !== null) {
      value = progressPct + "%";
    }
    children.push(h("span", { class: "v" + (status === "ok" ? " ok" : ""), text: value }));
    return h("li", { class: "step" + (status === "wait" ? " wait" : "") }, children);
  }

  function barNode(pct) {
    var fill = h("i");
    fill.style.width = pct + "%";
    return h("span", { class: "bar" }, [fill]);
  }

  // Signature decides whether the step list has to be rebuilt.
  function signature(setup) {
    return [setup.state, setup.step, Math.round((setup.progress || 0) * 100), setup.problem || ""].join("|");
  }

  function build(setup, mount) {
    var state = setup.state;
    var done = state === "done";
    var wrong = state === "wrong_disc";
    var title = mount.title;
    var sub = mount.sub;
    var steps = [];
    var buttons = [];
    var hint = null;

    title.textContent = done ? "Ready" : "Set up your game";
    sub.textContent = done ? "Next time GT4Recomp opens straight to Play." : "This only happens once.";

    var discName = setup.disc_name || DEFAULT_DISC;
    if (wrong) {
      steps.push(h("li", { class: "step" }, [
        h("span", { class: "n", text: "01" }),
        h("span", null, [h("span", { class: "t", text: "Disc" }), h("span", { class: "d", text: discName })]),
        h("span", { class: "v bad", text: "Not supported" })
      ]));
      mount.problem.textContent = setup.problem || DEFAULT_PROBLEM;
      mount.problem.hidden = false;
      buttons.push({ label: "Choose another file", primary: true, run: function () { chooseDisc(); } });
    } else {
      mount.problem.hidden = true;
      var order = ORDER.indexOf(setup.step);
      steps.push(h("li", { class: "step" }, [
        h("span", { class: "n", text: "01" }),
        h("span", null, [h("span", { class: "t", text: "Disc" }), h("span", { class: "d", text: discName })]),
        h("span", { class: "v ok", text: "✓" })
      ]));
      var pct = Math.round((setup.progress || 0) * 100);
      var labels = [
        ["02", "Checking disc", null],
        ["03", "Preparing game files", "about a minute"],
        ["04", "Memory card", null]
      ];
      labels.forEach(function (entry, index) {
        var status;
        if (done || index < order) {
          status = "ok";
        } else if (index === order) {
          status = "active";
        } else {
          status = "wait";
        }
        steps.push(stepRow(entry[0], entry[1], entry[2], status, index === order && index === 0 ? pct : null));
      });
      if (done) {
        buttons.push({ label: "Play", primary: true, run: function () { playNow(); } });
        hint = "Play";
      } else {
        buttons.push({ label: "Cancel", run: function () { cancelSetup(); } });
        buttons.push({ label: "Continue", primary: true, disabled: true });
      }
    }

    GT4.ui.clear(mount.steps);
    steps.forEach(function (node) {
      mount.steps.appendChild(node);
    });

    GT4.ui.clear(mount.foot);
    buttons.forEach(function (button) {
      var node = h("button", {
        class: "btn" + (button.primary ? " primary" : ""),
        type: "button",
        disabled: !!button.disabled,
        on: {
          click: function () {
            if (button.run) {
              button.run();
            }
          }
        }
      }, button.label);
      mount.foot.appendChild(node);
    });
    mount.primaryButton = buttons.filter(function (b) { return b.primary && !b.disabled; })[0] || null;
    GT4.ui.setHints(hint ? [{ key: "A", label: hint, run: playNow }] : []);
    return mount;
  }

  function chooseDisc() {
    call("setup_choose_disc").then(function () {
      poll();
    }, function (err) {
      GT4.ui.toast(GT4.errorText(err), true);
    });
  }

  function cancelSetup() {
    call("setup_cancel").then(function () {
      poll();
    }, function (err) {
      GT4.ui.toast(GT4.errorText(err), true);
    });
  }

  function playNow() {
    call("play").then(function () {
      GT4.go("#/home");
    }, function (err) {
      GT4.ui.toast(GT4.errorText(err), true);
    });
  }

  var active = null;

  function poll() {
    if (!active) {
      return;
    }
    call("app_state").then(function (appState) {
      if (!active) {
        return;
      }
      var setup = appState.setup || { state: "needed" };
      var sig = signature(setup);
      if (sig !== active.last) {
        active.last = sig;
        var focusKept = document.activeElement && active.root.contains(document.activeElement) ? document.activeElement.textContent : null;
        build(setup, active.parts);
        if (focusKept) {
          var again = Array.prototype.find.call(active.parts.foot.querySelectorAll("button"), function (b) {
            return b.textContent === focusKept && !b.disabled;
          });
          (again || active.parts.foot.querySelector("button.primary:not([disabled])") || active.parts.foot.querySelector("button")).focus({ preventScroll: true });
        } else if (active.first) {
          var primary = active.parts.foot.querySelector("button.primary:not([disabled])");
          if (primary) {
            primary.focus({ preventScroll: true });
          }
        }
        active.first = false;
      }
      if (setup.state === "needed" && !active.started) {
        active.started = true;
        call("setup_start").catch(function (err) {
          GT4.ui.toast(GT4.errorText(err), true);
        });
      }
    }, function (err) {
      GT4.ui.toast(GT4.errorText(err), true);
    });
  }

  GT4.pages = GT4.pages || {};
  GT4.pages.setup = {
    mount: function (root) {
      var cover = h("img", { class: "cover", src: "assets/cover-front.png", alt: "Gran Turismo 4 cover" });
      var title = h("h1", { text: "Set up your game" });
      var sub = h("p", { class: "sub", text: "This only happens once." });
      var steps = h("ul", { class: "steps" });
      var problem = h("p", { class: "problem", hidden: true });
      var foot = h("div", { class: "foot" });
      var parts = { title: title, sub: sub, steps: steps, problem: problem, foot: foot };
      var page = h("div", { class: "stage setup" }, [
        h("div", { class: "cover-col" }, [cover]),
        h("div", { class: "setup-main" }, [
          h("img", { class: "wordmark", src: "assets/logo-gt4.png", alt: "Gran Turismo 4" }),
          title,
          sub,
          steps,
          problem,
          foot
        ])
      ]);
      root.appendChild(page);
      active = { root: page, parts: parts, last: null, first: true, started: false };
      poll();
      var timer = setInterval(poll, POLL_MS);
      return {
        destroy: function () {
          active = null;
          clearInterval(timer);
        }
      };
    }
  };
})(window);
