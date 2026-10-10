#pragma once

#include "common.h"
#include "stress.h"

typedef struct {
  Baseline baseline;
  Baseline usual;       // The same readings over about six weeks, to catch the baseline sinking
  int32_t current_ln;   // Lightly smoothed ln(RMSSD), the value scored against the baseline
  time_t first_at;      // When the baseline's first reading was taken
  time_t result_at;     // When the last usable reading was taken
  time_t attempt_at;    // When the sensor was last opened, usable reading or not
  uint16_t rmssd;
  uint8_t high_run;     // Readings in a row that scored STRESS_HIGH or more
  uint8_t suspect_run;  // Readings in a row set aside as implausibly high
} HrvStore;

// Reads the stored baseline and last reading, or zeroes if there are none yet.
void hrv_core_load(HrvStore *store);

// Whether the stored reading is recent enough to still describe the wearer.
bool hrv_core_fresh(const HrvStore *store, const Settings *settings, time_t now);

// How far the baseline is from being usable, 0-100. It needs both enough readings and
// enough days, so that one unusual afternoon doesn't become "normal".
int hrv_core_calib_pct(const HrvStore *store, time_t now);

// The 0-100 stress score for the stored reading, or -1 if it's stale or the baseline
// hasn't been learned yet.
int hrv_core_stress(const HrvStore *store, const Settings *settings, time_t now);

// Whether stress has stayed high for long enough to be worth an alert.
bool hrv_core_stress_high(const HrvStore *store, const Settings *settings, time_t now);

// Whether the last week as a whole sits well below the wearer's longer-run usual. The
// score can't show this, because the baseline it's measured against sinks too.
bool hrv_core_worn_down(const HrvStore *store);

// Runs the measurement schedule; call once a minute from whichever process is doing the
// measuring. Returns true if a measurement window closed and the store was rewritten.
bool hrv_core_tick(time_t now, const Settings *settings);

// Call on every HealthEventHRVUpdate. Returns true if that closed a window.
bool hrv_core_on_event(void);

// Releases the sensor if a window is open.
void hrv_core_stop(void);
