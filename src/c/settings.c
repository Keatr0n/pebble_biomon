#include "biomon.h"

#define PERSIST_KEY_SETTINGS 1

Settings g_settings;

static void prv_defaults(void) {
  g_settings = (Settings) {
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
}

void settings_load(void) {
  prv_defaults();
  Settings stored;
  // A size mismatch means the struct changed shape since it was written.
  if (persist_read_data(PERSIST_KEY_SETTINGS, &stored, sizeof(stored)) == (int)sizeof(stored)) {
    g_settings = stored;
  }
}

// Clay sends selects as strings and everything else as integers of varying width.
static bool prv_read(DictionaryIterator *iter, uint32_t key, int32_t *out) {
  Tuple *tuple = dict_find(iter, key);
  if (!tuple) return false;

  if (tuple->type == TUPLE_CSTRING) {
    *out = atoi(tuple->value->cstring);
  } else if (tuple->length == 1) {
    *out = tuple->value->uint8;
  } else if (tuple->length == 2) {
    *out = tuple->value->uint16;
  } else {
    *out = tuple->value->int32;
  }
  return true;
}

static int32_t prv_clamp(int32_t value, int32_t low, int32_t high) {
  if (value < low) return low;
  if (value > high) return high;
  return value;
}

bool settings_handle_message(DictionaryIterator *iter) {
  bool changed = false;
  int32_t value;

  if (prv_read(iter, MESSAGE_KEY_Theme, &value)) {
    g_settings.theme = (uint8_t)prv_clamp(value, 0, THEME_COUNT - 1);
    changed = true;
  }
  if (prv_read(iter, MESSAGE_KEY_TrackWater, &value)) {
    g_settings.track_water = value != 0;
    changed = true;
  }
  if (prv_read(iter, MESSAGE_KEY_TrackFood, &value)) {
    g_settings.track_food = value != 0;
    changed = true;
  }
  if (prv_read(iter, MESSAGE_KEY_Vibrate, &value)) {
    g_settings.vibrate = value != 0;
    changed = true;
  }
  if (prv_read(iter, MESSAGE_KEY_QuietStart, &value)) {
    g_settings.quiet_start = (uint8_t)prv_clamp(value, 0, 23);
    changed = true;
  }
  if (prv_read(iter, MESSAGE_KEY_QuietEnd, &value)) {
    g_settings.quiet_end = (uint8_t)prv_clamp(value, 0, 23);
    changed = true;
  }
  if (prv_read(iter, MESSAGE_KEY_HrvInterval, &value)) {
    g_settings.hrv_interval_min = (uint8_t)prv_clamp(value, 5, 120);
    changed = true;
  }
  if (prv_read(iter, MESSAGE_KEY_NagInterval, &value)) {
    g_settings.nag_min = (uint8_t)prv_clamp(value, 0, 240);
    changed = true;
  }
  if (prv_read(iter, MESSAGE_KEY_Sex, &value)) {
    g_settings.sex = (uint8_t)prv_clamp(value, SEX_MALE, SEX_OTHER);
    changed = true;
  }
  if (prv_read(iter, MESSAGE_KEY_Age, &value)) {
    g_settings.age = (uint8_t)prv_clamp(value, 10, 110);
    changed = true;
  }
  if (prv_read(iter, MESSAGE_KEY_WeightKg, &value)) {
    g_settings.weight_kg = (uint16_t)prv_clamp(value, 25, 300);
    changed = true;
  }
  if (prv_read(iter, MESSAGE_KEY_HeightCm, &value)) {
    g_settings.height_cm = (uint16_t)prv_clamp(value, 100, 250);
    changed = true;
  }
  if (prv_read(iter, MESSAGE_KEY_VolumeUnit, &value)) {
    g_settings.volume_floz = value != 0;
    changed = true;
  }
  if (prv_read(iter, MESSAGE_KEY_EnergyUnit, &value)) {
    g_settings.energy_kcal = value != 0;
    changed = true;
  }
  if (prv_read(iter, MESSAGE_KEY_ResetBaseline, &value) && value != 0) {
    hrv_reset_baseline();
    changed = true;
  }

  if (changed) persist_write_data(PERSIST_KEY_SETTINGS, &g_settings, sizeof(g_settings));
  return changed;
}
