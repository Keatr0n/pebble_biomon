#include "biomon.h"

Settings g_settings;

void settings_load(void) {
  settings_core_load(&g_settings);
}

void settings_save(void) {
  persist_write_data(PERSIST_KEY_SETTINGS, &g_settings, sizeof(g_settings));
}
