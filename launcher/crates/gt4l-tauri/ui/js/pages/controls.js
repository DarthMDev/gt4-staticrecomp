/*
 * Controls tab: original DualShock 2 drawing (controller-svg.js), the
 * L1/L2/R1/R2 tabs, the selected control's PS2 name and binding, and the
 * L2 (from LT) / R2 (from RT) trigger rows.
 *
 * Commands:
 *   get_controls()                 -> {controls: [{id, name, binding}], triggers: [rows]}
 *   capture_binding({control})     -> {control, binding}   (waits for the next press)
 *   cancel_capture()               -> ends a capture
 *   reset_binding({control})       -> resets one control to its default
 *   set_setting({id, value})       -> trigger rows (ids like l2_deadzone)
 */
(function (root) {
  "use strict";
  var GT4 = (root.GT4 = root.GT4 || {});
  var h = GT4.ui.h;

  var SHOULDER_TABS = [
    { id: "l1", label: "L1" },
    { id: "l2", label: "L2" },
    { id: "r1", label: "R1" },
    { id: "r2", label: "R2" }
  ];

  function call(command, args) {
    return GT4.backend.call(command, args);
  }

  GT4.tabPages.controls = function (content, host) {
    var state = { controls: [], triggers: [], selected: null };
    var pad = GT4.controllerSvg(function (id) { select(id); });
    var panelName = h("div", { class: "ps2", text: "" });
    var panelBind = h("div", { class: "bind", text: "" });
    var panelRows = h("div", { class: "trigger-rows" });
    var tabButtons = {};

    var tabsRow = h("div", { class: "tabs-row", role: "tablist", "aria-label": "Shoulder buttons" },
      SHOULDER_TABS.map(function (tab) {
        var button = h("button", {
          class: "tab-btn",
          type: "button",
          role: "tab",
          "aria-selected": "false",
          on: { click: function () { select(tab.id); } }
        }, tab.label);
        tabButtons[tab.id] = button;
        return button;
      }));

    var rebindButton = h("button", {
      class: "btn primary small",
      type: "button",
      on: { click: function () { rebind(state.selected); } }
    }, "Rebind");
    var resetButton = h("button", {
      class: "btn small",
      type: "button",
      on: { click: function () { resetBinding(state.selected); } }
    }, "Reset");

    var svgWrap = h("div", { class: "pad-wrap" }, [pad.svg]);
    var panel = h("aside", { class: "side-panel" }, [
      panelName,
      panelBind,
      h("div", { class: "btns" }, [rebindButton, resetButton]),
      h("h2", { text: "Triggers" }),
      panelRows
    ]);

    content.appendChild(h("div", { class: "page-head" }, [
      h("div", null, [
        h("h1", { text: "Controls" }),
        h("p", { class: "note", text: "Select a button, then Rebind. Press Esc or B to cancel a rebind." })
      ])
    ]));
    content.appendChild(tabsRow);
    content.appendChild(h("div", { class: "controls-page" }, [svgWrap, panel]));

    function findControl(id) {
      for (var i = 0; i < state.controls.length; i++) {
        if (state.controls[i].id === id) {
          return state.controls[i];
        }
      }
      return null;
    }

    function paintSelection() {
      var control = state.selected ? findControl(state.selected) : null;
      panelName.textContent = control ? control.name : "No button selected";
      panelBind.textContent = control
        ? (control.binding ? "Bound to " + control.binding : "Not bound")
        : "Select a button on the drawing or a shoulder tab.";
      rebindButton.disabled = !control;
      resetButton.disabled = !control;
      SHOULDER_TABS.forEach(function (tab) {
        var on = tab.id === state.selected;
        tabButtons[tab.id].setAttribute("aria-selected", String(on));
      });
    }

    function select(id) {
      state.selected = id;
      pad.select(id);
      paintSelection();
    }

    function paintMapped() {
      state.controls.forEach(function (control) {
        pad.setMapped(control.id, !!control.binding);
      });
    }

    function paintTriggers() {
      GT4.ui.clear(panelRows);
      var context = {
        table: { rows: state.triggers },
        content: panelRows,
        refresh: function (focusId) { return load(focusId); }
      };
      state.triggers.forEach(function (row) {
        var node = GT4.settingsRows.rowNode(context, row);
        if (node) {
          panelRows.appendChild(node);
        }
      });
    }

    // Only the newest read is drawn. A trigger row keeps focus only while focus is still on this tab.
    var reads = GT4.settingsModel.readGate();
    function load(focusId) {
      var ticket = reads.ticket();
      return call("get_controls").then(function (view) {
        if (!reads.isLatest(ticket)) {
          return;
        }
        var keep = focusId && content.contains(document.activeElement) ? focusId : null;
        state.controls = view.controls || [];
        state.triggers = view.triggers || [];
        paintMapped();
        paintTriggers();
        paintSelection();
        if (keep) {
          var target = content.querySelector("[data-setting=\"" + keep + "\"]");
          if (target) {
            target.focus({ preventScroll: true });
          }
        }
      }, function (err) {
        GT4.ui.toast(GT4.errorText(err), true);
      });
    }

    function rebind(id) {
      if (!id) {
        GT4.ui.toast("Select a button first", true);
        return;
      }
      var control = findControl(id);
      var finished = false;
      var dialog = null;

      function finish(result, error) {
        if (finished) {
          return;
        }
        finished = true;
        GT4.capturing = false;
        GT4.cancelCapture = null;
        pad.setCapturing(id, false);
        if (dialog) {
          dialog.close();
        }
        if (error) {
          GT4.ui.toast(GT4.errorText(error), true);
        } else if (result && result.binding) {
          GT4.ui.toast(control.name + " bound to " + result.binding);
        }
        load();
      }

      GT4.capturing = true;
      pad.setCapturing(id, true);
      GT4.cancelCapture = function () {
        call("cancel_capture").catch(function () {});
        finish(null, null);
      };
      dialog = GT4.ui.openDialog({
        title: "Press a button for " + control.name,
        text: "Press a pad button or a key. Esc or B cancels.",
        dismissable: false,
        buttons: [
          {
            label: "Cancel",
            closes: false,
            run: function () { GT4.cancelCapture(); }
          }
        ]
      });
      call("capture_binding", { control: id }).then(
        function (result) { finish(result, null); },
        function (err) { finish(null, err); }
      );
    }

    function resetBinding(id) {
      if (!id) {
        GT4.ui.toast("Select a button first", true);
        return;
      }
      call("reset_binding", { control: id }).then(function () {
        GT4.ui.toast("Reset to the default binding");
        load();
      }, function (err) {
        GT4.ui.toast(GT4.errorText(err), true);
      });
    }

    host.setHooks({
      x: function () { rebind(state.selected); },
      y: function () { resetBinding(state.selected); }
    });
    host.hints([
      { key: "X", label: "Rebind", run: function () { rebind(state.selected); } },
      { key: "Y", label: "Reset", run: function () { resetBinding(state.selected); } }
    ]);

    // A enters the tab on the first shoulder tab (L1). Drawing the page does not move focus.
    host.entry(function () { return tabButtons.l1; });
    paintSelection();
    return load();
  };
})(window);
