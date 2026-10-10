#include "stress.h"

#define Q12_ONE 4096
#define LN2_Q12 2839
// Short daytime RMSSD readings scatter by 0.3-0.5 ln units for most people. Flooring the
// deviation stops a baseline learned on an unusually steady day (or from only a handful
// of nights) from flagging everything.
#define SD_FLOOR_Q12 614  // 0.15
#define SCORE_AT_BASELINE 35
#define SCORE_PER_SD 25

uint32_t stress_isqrt(uint32_t n) {
  if (n == 0) return 0;
  uint32_t x = n;
  uint32_t y = (x + 1) / 2;
  while (y < x) {
    x = y;
    y = (x + n / x) / 2;
  }
  return x;
}

int32_t stress_ln_q12(uint32_t x) {
  if (x <= 1) return 0;
  if (x > 0x7FFF) x = 0x7FFF;

  int32_t log2 = 0;
  uint32_t mantissa = x << 16;  // Q16, normalised into [1, 2) below
  while (mantissa >= (2u << 16)) {
    mantissa >>= 1;
    log2 += Q12_ONE;
  }
  // Each squaring of the mantissa yields the next fractional bit of log2.
  for (int bit = 11; bit >= 0; bit--) {
    mantissa = (uint32_t)(((uint64_t)mantissa * mantissa) >> 16);
    if (mantissa >= (2u << 16)) {
      mantissa >>= 1;
      log2 += 1 << bit;
    }
  }
  return (log2 * LN2_Q12) >> 12;
}

int stress_rmssd(const uint16_t *ppi_ms, int count, uint32_t breaks, int max_diff_ms,
                 int min_diffs) {
  uint32_t sum_sq = 0;
  int diffs = 0;
  int jumps = 0;

  for (int i = 1; i < count; i++) {
    if (breaks & (1u << i)) continue;
    int delta = (int)ppi_ms[i] - (int)ppi_ms[i - 1];
    if (delta < 0) delta = -delta;
    if (delta > max_diff_ms) {
      jumps++;
      continue;
    }
    sum_sq += (uint32_t)(delta * delta);
    diffs++;
  }

  // A window that's mostly artifacts isn't rescued by the few differences that passed.
  if (diffs < min_diffs || jumps * 3 > diffs) return 0;
  int rmssd = (int)stress_isqrt(sum_sq / (uint32_t)diffs);
  return rmssd > 0 ? rmssd : 1;
}

void stress_baseline_add(Baseline *baseline, int32_t ln_q12, int window) {
  int32_t x = ln_q12 << 8;
  if (baseline->count == 0) {
    baseline->mean = x;
    baseline->var = 0;
    baseline->count = 1;
    return;
  }

  // Up to the window size this is an exact running mean/variance; past it the divisor
  // stops growing and it becomes an exponential average that slowly forgets old readings.
  if (baseline->count < window) baseline->count++;
  int32_t n = baseline->count;

  int32_t before = x - baseline->mean;
  baseline->mean += before / n;
  int32_t after = x - baseline->mean;
  int32_t sq = (int32_t)(((int64_t)before * after) >> 20);
  baseline->var += (sq - baseline->var) / n;
}

int32_t stress_z_q8(const Baseline *baseline, int32_t ln_q12) {
  uint32_t var_q12 = baseline->var > 0 ? (uint32_t)baseline->var >> 8 : 0;
  if (var_q12 > (1u << 19)) var_q12 = 1u << 19;
  int32_t sd = (int32_t)stress_isqrt(var_q12 << 12);
  if (sd < SD_FLOOR_Q12) sd = SD_FLOOR_Q12;

  return ((ln_q12 - (baseline->mean >> 8)) * 256) / sd;
}

int stress_score(const Baseline *baseline, int32_t ln_q12) {
  // Lower variability than usual is the stress signal, so the z-score is flipped.
  int32_t score = SCORE_AT_BASELINE - (SCORE_PER_SD * stress_z_q8(baseline, ln_q12)) / 256;
  if (score < 0) score = 0;
  if (score > 100) score = 100;
  return (int)score;
}
