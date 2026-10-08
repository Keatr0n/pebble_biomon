#pragma once

#include <stdint.h>

// Pure integer maths for turning RR intervals into a stress score. No Pebble headers, so
// it can be compiled and tested on a desktop.

#define STRESS_BASELINE_WINDOW 400  // Samples the baseline remembers (roughly a week of readings)
#define STRESS_MIN_SAMPLES 20       // Readings needed before the baseline is trusted

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
// Returns 0 when fewer than min_diffs usable differences were found.
int stress_rmssd(const uint16_t *ppi_ms, int count, int max_diff_ms, int min_diffs);

void stress_baseline_add(Baseline *baseline, int32_t ln_q12);

// 0 (far more variability than usual) to 100 (far less). A reading that sits right on
// the baseline scores 35.
int stress_score(const Baseline *baseline, int32_t ln_q12);
