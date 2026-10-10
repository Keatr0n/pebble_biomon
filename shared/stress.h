#pragma once

#include <stdint.h>

// Pure integer maths for turning RR intervals into a stress score. No Pebble headers, so
// it can be compiled and tested on a desktop.

#define STRESS_BASELINE_WINDOW 400  // Daytime readings the baseline remembers (roughly a week)
#define STRESS_USUAL_WINDOW 2400    // The slow baseline the weekly one is checked against
#define STRESS_MIN_SAMPLES 20       // Readings needed before the baseline is trusted
#define STRESS_HIGH 75              // Score that counts as a stress alert

// Running mean and variance of ln(RMSSD). Both are Q12 fixed point shifted up a further
// 8 bits, so a 1/400 step of a small deviation doesn't round away to nothing.
typedef struct {
  int32_t mean;
  int32_t var;
  uint16_t count;
} Baseline;

uint32_t stress_isqrt(uint32_t n);

// Natural log of x as Q12 fixed point. x must be >= 1.
int32_t stress_ln_q12(uint32_t x);

// RMSSD in ms over consecutive intervals, skipping any jump larger than max_diff_ms.
// Bit i of breaks marks interval i as not following straight on from interval i - 1, so
// that pair isn't a successive difference and is left out.
// Returns 0 when fewer than min_diffs usable differences were found, or when more than a
// quarter of the pairs had to be skipped as jumps.
int stress_rmssd(const uint16_t *ppi_ms, int count, uint32_t breaks, int max_diff_ms,
                 int min_diffs);

// Past `window` samples the baseline stops averaging evenly and starts forgetting.
void stress_baseline_add(Baseline *baseline, int32_t ln_q12, int window);

// How far a reading sits above the baseline, in standard deviations as Q8 fixed point.
int32_t stress_z_q8(const Baseline *baseline, int32_t ln_q12);

// 0 (far more variability than usual) to 100 (far less). A reading that sits right on
// the baseline scores 35.
int stress_score(const Baseline *baseline, int32_t ln_q12);
