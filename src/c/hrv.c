#include "biomon.h"
#include "energy_core.h"
#include "hrv_core.h"

// The measuring itself lives in shared/hrv_core.c and shared/energy_core.c, where the background worker can run
// it too. This side only turns what's been stored into numbers for the screen.

void hrv_refresh(void) {
  HrvStore store;
  hrv_core_load(&store);

  time_t now = time(NULL);
  g_bio.hrv = hrv_core_fresh(&store, &g_settings, now) ? store.rmssd : -1;
  g_bio.stress = hrv_core_stress(&store, &g_settings, now);
  g_bio.stress_high = hrv_core_stress_high(&store, &g_settings, now);
  g_bio.worn_down = hrv_core_worn_down(&store);

  EnergyStore energy;
  energy_core_load(&energy);
  g_bio.energy = energy_core_pct(&energy);
  g_bio.calib_pct = hrv_core_calib_pct(&store, now);
  g_bio.calibrating = g_bio.calib_pct > 0 && g_bio.calib_pct < 100;
}

void hrv_reset_baseline(void) {
  persist_delete(PERSIST_KEY_HRV);
  persist_delete(PERSIST_KEY_ENERGY);
  hrv_refresh();
}
