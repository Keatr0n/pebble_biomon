var Clay = require("@rebble/clay");
var clayConfig = require("./config.json");
// Events are handled by hand so the baseline reset can be a one-shot rather than a
// setting that sticks and wipes the baseline again on every later save.
var clay = new Clay(clayConfig, null, { autoHandleEvents: false });

function clearResetFlag() {
  try {
    var stored = JSON.parse(localStorage.getItem("clay-settings")) || {};
    stored.ResetBaseline = false;
    localStorage.setItem("clay-settings", JSON.stringify(stored));
  } catch (e) {
    console.log("Could not clear the baseline reset flag: " + e);
  }
}

Pebble.addEventListener("showConfiguration", function () {
  Pebble.openURL(clay.generateUrl());
});

Pebble.addEventListener("webviewclosed", function (e) {
  if (!e || !e.response) return;

  var settings = clay.getSettings(e.response);
  clearResetFlag();
  Pebble.sendAppMessage(
    settings,
    function () {},
    function (err) {
      console.log("Failed to send settings: " + JSON.stringify(err));
    }
  );
});
