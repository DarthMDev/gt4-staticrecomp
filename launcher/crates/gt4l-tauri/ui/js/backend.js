/*
 * Command bridge. In the Tauri window, calls go to Rust through
 * window.__TAURI__.core.invoke (withGlobalTauri is on). In a plain browser the
 * dev harness installs window.GT4_MOCK, which answers the same command names.
 * The page never touches the file system or the shell directly.
 */
(function (root) {
  "use strict";
  var GT4 = (root.GT4 = root.GT4 || {});

  function tauri() {
    return root.__TAURI__ || null;
  }

  // call("set_setting", {id: "fxaa", value: true}) -> Promise
  function call(command, args) {
    var t = tauri();
    if (t && t.core && typeof t.core.invoke === "function") {
      return t.core.invoke(command, args || {});
    }
    if (root.GT4_MOCK && typeof root.GT4_MOCK.invoke === "function") {
      return new Promise(function (resolve, reject) {
        // Asynchronous on purpose, so the mock behaves like IPC.
        setTimeout(function () {
          try {
            resolve(root.GT4_MOCK.invoke(command, args || {}));
          } catch (err) {
            reject(err);
          }
        }, 20);
      });
    }
    return Promise.reject(new Error("No backend for " + command));
  }

  // Optional Rust-side events, e.g. "gamepad" with {button, pressed}.
  function listen(eventName, handler) {
    var t = tauri();
    if (t && t.event && typeof t.event.listen === "function") {
      t.event.listen(eventName, function (event) {
        handler(event.payload);
      });
    } else if (root.GT4_MOCK && typeof root.GT4_MOCK.listen === "function") {
      root.GT4_MOCK.listen(eventName, handler);
    }
  }

  GT4.backend = { call: call, listen: listen };
})(window);
