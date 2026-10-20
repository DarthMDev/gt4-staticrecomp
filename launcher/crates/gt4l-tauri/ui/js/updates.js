/*
 * Update popup and "Check for updates" (owner rules, 2026-10-20). The launcher offers an update; the player decides.
 * Update installs it (gt4-gs in place; GT4Recomp starts its setup program and the launcher closes).
 * Not now closes the popup. Skip this version is remembered for exactly that version.
 * Release notes are shown as text only: no HTML from the release body. Supported: "#" headings, "-" or "*" bullets
 * and **bold**. Everything else is plain text.
 * Controller: A selects the focused button, B closes the popup as Not now (the dialog's Back).
 */
(function (root) {
  "use strict";
  var GT4 = (root.GT4 = root.GT4 || {});
  var h = GT4.ui.h;
  var queue = [];

  var NAMES = {
    launcher: "GT4Recomp",
    renderer: "gt4-gs (PCSX2 GS renderer)"
  };

  function errorText(err) {
    return GT4.errorText ? GT4.errorText(err) : String(err);
  }

  // Text with **bold** runs as nodes; everything else is a text node (no innerHTML anywhere).
  function inline(text) {
    var parts = [];
    var pattern = /\*\*([^*]+)\*\*/g;
    var last = 0;
    var match;
    while ((match = pattern.exec(text)) !== null) {
      if (match.index > last) {
        parts.push(text.slice(last, match.index));
      }
      parts.push(h("strong", { text: match[1] }));
      last = pattern.lastIndex;
    }
    if (last < text.length) {
      parts.push(text.slice(last));
    }
    return parts;
  }

  function renderNotes(text) {
    var box = h("div", { class: "release-notes" });
    var list = null;
    String(text || "").split(/\r?\n/).forEach(function (raw) {
      var line = raw.trim();
      var heading = /^#{1,6}\s+(.*)$/.exec(line);
      var bullet = /^[-*]\s+(.*)$/.exec(line);
      if (!line) {
        list = null;
      } else if (heading) {
        list = null;
        box.appendChild(h("p", { class: "notes-heading" }, inline(heading[1])));
      } else if (bullet) {
        if (!list) {
          list = h("ul", {});
          box.appendChild(list);
        }
        list.appendChild(h("li", {}, inline(bullet[1])));
      } else {
        list = null;
        box.appendChild(h("p", {}, inline(line)));
      }
    });
    if (!box.childNodes.length) {
      box.appendChild(h("p", { text: "No release notes were written for this version." }));
    }
    return box;
  }

  function startUpdate(offer) {
    GT4.ui.toast(offer.stream === "launcher" ? "Starting the GT4Recomp setup..." : "Installing gt4-gs...");
    GT4.backend.call("apply_update", { stream: offer.stream }).then(null, function (err) {
      GT4.ui.toast(errorText(err), true);
    });
  }

  function skip(offer) {
    GT4.backend.call("skip_update", { stream: offer.stream, version: offer.latest }).then(function () {
      GT4.ui.toast("Skipped " + NAMES[offer.stream] + " " + offer.latest + ".");
    }, function (err) {
      GT4.ui.toast(errorText(err), true);
    });
  }

  function offerDialog(offer) {
    var name = NAMES[offer.stream] || offer.stream;
    var summary = (offer.installed ? "You have " + offer.installed + ". " : "") + "The new version is " + offer.latest + ".";
    var body = h("div", { class: "update-body" }, [
      h("p", { class: "modal-text", text: summary }),
      h("h4", { class: "notes-title", text: "What's new" }),
      renderNotes(offer.notes)
    ]);
    GT4.ui.openDialog({
      title: name + " " + offer.latest + " is available",
      body: body,
      wide: true,
      dismissable: true,
      onClose: showNext,
      buttons: [
        { label: "Update", primary: true, run: function () { startUpdate(offer); } },
        { label: "Not now" },
        { label: "Skip this version", run: function () { skip(offer); } }
      ]
    });
  }

  // Shows the queued offers one at a time; waits while another dialog is open.
  function showNext() {
    if (!queue.length) {
      return;
    }
    if (GT4.ui.topDialog()) {
      setTimeout(showNext, 800);
      return;
    }
    offerDialog(queue.shift());
  }

  // Called by the launch-time `update-available` event with {offers: [OfferView]}.
  function offer(offers) {
    queue = queue.concat((offers || []).filter(function (item) {
      return item && item.update;
    }));
    showNext();
  }

  // Called by the window close button while the game runs (`close-confirm`).
  function confirmClose(message) {
    if (GT4.ui.topDialog()) {
      return;
    }
    GT4.ui.openDialog({
      title: "Close the launcher?",
      text: message || "The game is running. Close the launcher anyway?",
      dismissable: true,
      buttons: [
        { label: "Keep open", primary: true },
        {
          label: "Close launcher",
          danger: true,
          run: function () {
            GT4.backend.call("confirm_close").then(null, function (err) {
              GT4.ui.toast(errorText(err), true);
            });
          }
        }
      ]
    });
  }

  // Settings > About > "Check for updates": same flow as the launch check; says so when nothing is new.
  function checkNow() {
    GT4.ui.toast("Checking for updates...");
    return GT4.backend.call("check_updates").then(function (result) {
      if (result.offers && result.offers.length) {
        offer(result.offers);
      } else if (result.upToDate) {
        GT4.ui.toast("You're up to date.");
      } else {
        GT4.ui.toast("Could not check for updates: " + (result.errors || []).join("; "), true);
      }
    }, function (err) {
      GT4.ui.toast(errorText(err), true);
    });
  }

  GT4.updates = { offer: offer, confirmClose: confirmClose, checkNow: checkNow, renderNotes: renderNotes };
})(window);
