#pragma once

#include "common.h"
#include "energy.h"

// The energy meter and what it has learned about the wearer's nights. Written by
// whichever process is doing the measuring.
typedef struct {
  int32_t level;
  time_t updated_at;       // When the level was last moved up to
  Baseline night_hrv;      // One sample per night: that night's mean ln(RMSSD)
  uint16_t rhr_x16;        // Usual sleeping heart rate, in sixteenths of a bpm
  uint16_t wake_hr_x16;    // Usual heart rate awake and still, likewise
  uint16_t sleep_debt_min; // Running shortfall of sleep, which caps how far it recharges
  time_t debt_day;         // The day the debt was last brought up to date
  uint8_t night_quality;   // How the last finished night scored, 0 if there hasn't been one
  // The night in progress.
  time_t asleep_at;        // The last minute the wearer was seen asleep
  int32_t night_ln_sum;
  uint16_t night_hr_sum;
  uint16_t night_samples;
  uint16_t night_min;      // Minutes asleep so far
} EnergyStore;

// Reads the stored meter, or a fresh one if nothing has been saved yet.
void energy_core_load(EnergyStore *store);

// Moves the meter on to now; call once a minute from whichever process is measuring.
// stress is the current 0-100 score, or -1. Returns true if the store was rewritten.
bool energy_core_tick(time_t now, const Settings *settings, int stress);

// Adds one HRV reading taken during sleep to the night in progress.
void energy_core_night_sample(int32_t ln_q12, int hr);

// Adds the heart rate from a reading taken awake and still to the waking baseline.
void energy_core_wake_sample(int hr);

int energy_core_pct(const EnergyStore *store);
