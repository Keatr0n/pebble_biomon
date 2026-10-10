#include "energy.h"

// The meter recharges in sleep and drains while awake, faster the harder the body works.
// The rates are estimates picked to give a believable day, not a validated model: a calm
// desk day costs about 40%, an hour of moderate exercise about 15% more, and eight hours
// of ordinary sleep puts back 70% - enough to cover a normal day, but not so much that a
// short night after a hard one goes unnoticed. Short nights also build a sleep debt that
// lowers how far it can recharge, so a calm week can't hide a run of them.
#define SLEEP_RATE ((ENERGY_FULL * 7) / (10 * 8 * 60))
#define QUALITY_MIN 28                       // A bad night still recharges at 0.4x...
#define QUALITY_MAX 91                       // ...and a great one at no more than 1.3x
#define QUALITY_PER_SD 20
#define QUALITY_PER_BPM 2                    // Each beat above the usual sleeping rate

#define BASE_DRAIN 250         // 2.5% an hour just for being awake
#define CALM_RECHARGE 150      // 1.5% an hour back while properly relaxed
#define CALM_BELOW_STRESS 25
#define STRESS_NEUTRAL 35      // The score a reading on the baseline gets
#define STRESS_DRAIN_MAX 400   // Extra drain at a stress score of 100
#define EFFORT_FLOOR_PCT 20    // Heart rate reserve used up just pottering about
#define EFFORT_DRAIN 40        // Per point of reserve above that: 12%/h at 50%, 24%/h at 80%
#define MIN_HR_RESERVE 40
#define DEBT_MIN_PER_PCT 12    // Each 12 minutes of sleep debt takes 1% off the ceiling

static int prv_clamp(int value, int low, int high) {
  return value < low ? low : (value > high ? high : value);
}

int energy_night_quality(const Baseline *night_hrv, int32_t night_ln_q12, int rhr, int night_hr) {
  if (night_hrv->count < ENERGY_MIN_NIGHTS) return ENERGY_USUAL_NIGHT;

  int32_t quality = ENERGY_USUAL_NIGHT +
                    (QUALITY_PER_SD * stress_z_q8(night_hrv, night_ln_q12)) / 256;
  if (rhr > 0 && night_hr > 0) quality -= QUALITY_PER_BPM * (night_hr - rhr);
  return prv_clamp((int)quality, 0, 100);
}

int32_t energy_rate(const EnergyInput *in) {
  if (in->asleep) {
    return (SLEEP_RATE * prv_clamp(in->night_quality, QUALITY_MIN, QUALITY_MAX)) /
           ENERGY_USUAL_NIGHT;
  }
  // No heart rate means the watch is off the wrist, and there's nothing to go on.
  if (in->hr <= 0) return in->assume_asleep ? SLEEP_RATE : 0;

  // Effort is how much of the gap between resting and maximum heart rate is in use.
  int hr_max = 208 - (7 * in->age) / 10;
  int reserve = hr_max - in->rest_hr;
  if (reserve < MIN_HR_RESERVE) reserve = MIN_HR_RESERVE;
  int effort = prv_clamp(((in->hr - in->rest_hr) * 100) / reserve, 0, 100);
  if (effort > EFFORT_FLOOR_PCT) {
    return -(BASE_DRAIN + (effort - EFFORT_FLOOR_PCT) * EFFORT_DRAIN);
  }

  if (in->stress < 0) return -BASE_DRAIN;
  if (in->stress < CALM_BELOW_STRESS) return CALM_RECHARGE;
  if (in->stress <= STRESS_NEUTRAL) return -BASE_DRAIN;
  return -(BASE_DRAIN +
           ((in->stress - STRESS_NEUTRAL) * STRESS_DRAIN_MAX) / (100 - STRESS_NEUTRAL));
}

int energy_sleep_debt(int debt_min, int slept_min) {
  return prv_clamp((debt_min * 3) / 4 + ENERGY_SLEEP_NEED_MIN - slept_min, 0, ENERGY_DEBT_MAX_MIN);
}

int energy_ceiling_pct(int debt_min) {
  return 100 - prv_clamp(debt_min, 0, ENERGY_DEBT_MAX_MIN) / DEBT_MIN_PER_PCT;
}
