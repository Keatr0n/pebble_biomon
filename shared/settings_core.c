#include "common.h"

void settings_core_load(Settings *settings) {
  *settings = (Settings) {
    .theme = THEME_SUBNAUTICA,
    .track_water = true,
    .track_food = true,
    .vibrate = true,
    .quiet_start = 22,
    .quiet_end = 7,
    .hrv_interval_min = 15,
    .nag_min = 20,
    .sex = SEX_OTHER,
    .age = 35,
    .weight_kg = 75,
    .height_cm = 172
  };

  Settings stored;
  // A size mismatch means the struct changed shape since it was written.
  if (persist_read_data(PERSIST_KEY_SETTINGS, &stored, sizeof(stored)) == (int)sizeof(stored)) {
    *settings = stored;
  }
}
