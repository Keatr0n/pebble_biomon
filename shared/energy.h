#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "stress.h"

// Pure integer maths for the energy ("body battery") meter. No Pebble headers, so it can
// be compiled and tested on a desktop.

#define ENERGY_FULL 600000     // Chosen so that 100 units a minute is 1% an hour
#define ENERGY_PER_PCT (ENERGY_FULL / 100)
#define ENERGY_START_PCT 70    // Where the meter sits before it knows anything
#define ENERGY_LOW_PCT 20      // Below this the HUD says to rest

#define ENERGY_USUAL_NIGHT 70  // Quality score of a night that matches the baseline
#define ENERGY_NIGHTS_WINDOW 14
#define ENERGY_MIN_NIGHTS 3    // Nights needed before a night is judged against the others
#define ENERGY_DEFAULT_RHR 60  // Sleeping heart rate assumed until some nights are in
#define ENERGY_WAKE_HR_GAP 10  // How far above that the waking rate is assumed to sit

#define ENERGY_SLEEP_NEED_MIN 420  // A night shorter than this adds to the sleep debt
#define ENERGY_DEBT_MAX_MIN 600

typedef struct {
  bool asleep;
  bool assume_asleep;  // Not being worn during quiet hours: take it as an ordinary night
  int hr;              // Current bpm, or <= 0 when there's no recent reading
  int rest_hr;         // Usual heart rate awake and still
  int age;
  int stress;          // 0-100, or -1 when unknown
  int night_quality;   // 0-100 for the night in progress
} EnergyInput;

// How a night's HRV and heart rate compare with the wearer's usual, 0-100. A night like
// any other scores ENERGY_USUAL_NIGHT; so does every night until the baseline is learned.
int energy_night_quality(const Baseline *night_hrv, int32_t night_ln_q12, int rhr, int night_hr);

// Change in level per minute: positive while recharging, negative while draining.
int32_t energy_rate(const EnergyInput *in);

// The running sleep debt in minutes after a day in which slept_min were slept. Old debt
// fades by a quarter a day; a long night pays it down faster.
int energy_sleep_debt(int debt_min, int slept_min);

// The highest percentage the meter can recharge to while carrying that much debt.
int energy_ceiling_pct(int debt_min);
