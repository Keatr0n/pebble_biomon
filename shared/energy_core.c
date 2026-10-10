#include "energy_core.h"
#include "needs_core.h"

// Unlike the HRV store this one is kept in memory between ticks, because the level moves
// every minute and writing flash that often would be wasteful. It's written out whenever
// the visible percentage changes or something is learned about the night.
#define MAX_CATCH_UP_MIN 10    // A longer gap is rebuilt from the watch's own health log
#define MAX_BACKFILL_MIN (24 * 60)
#define BACKFILL_CHUNK 16      // Minute records read from the log at a time
#define BACKFILL_HR_CARRY_MIN 15  // The log has a heart rate every ten minutes or so
#define BACKFILL_MAX_SLEEPS 8
#define DEBT_HOUR 12           // Sleep debt is settled at midday, when any lie-in is over
#define DEBT_MIN_SLEPT_MIN 60  // Less sleep than this on record means the watch wasn't worn
#define NIGHT_OVER_SEC (30 * SECONDS_PER_MINUTE)  // Awake this long and the night is done
#define NIGHT_MIN_MINUTES 180  // Anything shorter is a nap and teaches the baseline nothing
#define NIGHT_MIN_SAMPLES 4
#define NIGHT_MAX_SAMPLES 200

static EnergyStore s_store;
static bool s_loaded = false;
static int s_saved_pct = -1;

void energy_core_load(EnergyStore *store) {
  if (persist_read_data(PERSIST_KEY_ENERGY, store, sizeof(*store)) == (int)sizeof(*store)) return;
  *store = (EnergyStore) {
    .level = ENERGY_START_PCT * ENERGY_PER_PCT, .updated_at = time(NULL)
  };
}

int energy_core_pct(const EnergyStore *store) {
  return (int)(store->level / ENERGY_PER_PCT);
}

static void prv_save(void) {
  persist_write_data(PERSIST_KEY_ENERGY, &s_store, sizeof(s_store));
  s_saved_pct = energy_core_pct(&s_store);
}

// Nothing saved is also how a reset from the app's settings shows up here, so the copy
// in memory is thrown away with it.
static void prv_ensure_loaded(void) {
  bool missing = !persist_exists(PERSIST_KEY_ENERGY);
  if (s_loaded && !missing) return;
  energy_core_load(&s_store);
  s_loaded = true;
  s_saved_pct = energy_core_pct(&s_store);
  if (missing) prv_save();
}

static int prv_rhr(void) {
  return s_store.rhr_x16 ? s_store.rhr_x16 / 16 : ENERGY_DEFAULT_RHR;
}

// Effort is judged from the waking rate rather than the sleeping one, which sits low
// enough that an ordinary pulse at a desk would count as exercise.
static int prv_rest_hr(void) {
  return s_store.wake_hr_x16 ? s_store.wake_hr_x16 / 16 : prv_rhr() + ENERGY_WAKE_HR_GAP;
}

// Quality of the night so far, so that sleep recharges at the right rate while it's
// still happening rather than being settled up in the morning.
static int prv_night_quality(void) {
  if (s_store.night_samples == 0) return ENERGY_USUAL_NIGHT;
  return energy_night_quality(&s_store.night_hrv,
                              s_store.night_ln_sum / s_store.night_samples, prv_rhr(),
                              s_store.night_hr_sum / s_store.night_samples);
}

static void prv_close_night(void) {
  if (s_store.night_min >= NIGHT_MIN_MINUTES && s_store.night_samples >= NIGHT_MIN_SAMPLES) {
    // Scored before it joins the baseline, so a night isn't compared with itself.
    s_store.night_quality = (uint8_t)prv_night_quality();
    stress_baseline_add(&s_store.night_hrv, s_store.night_ln_sum / s_store.night_samples,
                        ENERGY_NIGHTS_WINDOW);

    int32_t hr_x16 = (s_store.night_hr_sum * 16) / s_store.night_samples;
    s_store.rhr_x16 = s_store.rhr_x16 == 0 ? (uint16_t)hr_x16
        : (uint16_t)(s_store.rhr_x16 + (hr_x16 - s_store.rhr_x16) / s_store.night_hrv.count);
  }
  s_store.night_ln_sum = 0;
  s_store.night_hr_sum = 0;
  s_store.night_samples = 0;
  s_store.night_min = 0;
}

#if defined(PBL_HEALTH)
static void prv_move(int32_t delta) {
  int32_t level = s_store.level + delta;
  if (delta > 0) {
    // Sleep debt stops a recharge short of full; it never takes away what's there.
    int32_t ceiling = energy_ceiling_pct(s_store.sleep_debt_min) * ENERGY_PER_PCT;
    if (level > ceiling) level = s_store.level > ceiling ? s_store.level : ceiling;
  }
  s_store.level = level < 0 ? 0 : level;
}

typedef struct {
  time_t start[BACKFILL_MAX_SLEEPS];
  time_t end[BACKFILL_MAX_SLEEPS];
  int count;
} SleepLog;

static bool prv_collect_sleep(HealthActivity activity, time_t start, time_t end, void *context) {
  SleepLog *log = context;
  if (log->count == BACKFILL_MAX_SLEEPS) return false;
  log->start[log->count] = start;
  log->end[log->count] = end;
  log->count++;
  return true;
}

static bool prv_slept_at(const SleepLog *log, time_t when) {
  for (int i = 0; i < log->count; i++) {
    if (when >= log->start[i] && when < log->end[i]) return true;
  }
  return false;
}

// Moves the meter across a stretch nobody was measuring - the worker lost its slot, or
// the watch restarted - using the sleep and heart rate the watch logged for itself.
// Stress isn't known for it, and a minute with no heart rate nearby counts as off the wrist.
static void prv_backfill(time_t from, time_t to, const Settings *settings) {
  static HealthMinuteData s_minutes[BACKFILL_CHUNK];
  if (to - from > MAX_BACKFILL_MIN * SECONDS_PER_MINUTE) {
    from = to - MAX_BACKFILL_MIN * SECONDS_PER_MINUTE;
  }

  SleepLog sleeps = { .count = 0 };
  health_service_activities_iterate(HealthActivitySleep, from, to, HealthIterationDirectionFuture,
                                    prv_collect_sleep, &sleeps);

  EnergyInput input = {
    .rest_hr = prv_rest_hr(), .age = settings->age, .stress = -1,
    .night_quality = ENERGY_USUAL_NIGHT
  };
  int hr = 0;
  int hr_age = BACKFILL_HR_CARRY_MIN + 1;

  while (from < to) {
    time_t start = from;
    time_t end = to;
    uint32_t count = health_service_get_minute_history(s_minutes, BACKFILL_CHUNK, &start, &end);
    // Whatever the log doesn't cover is walked through as minutes with nothing in them.
    time_t logged_from = count ? start : to;
    time_t logged_to = count ? start + (time_t)count * SECONDS_PER_MINUTE : to;
    input.assume_asleep = needs_core_in_quiet_hours(settings, from);

    for (time_t minute = from; minute < logged_to && minute < to; minute += SECONDS_PER_MINUTE) {
      int logged_hr = 0;
      if (minute >= logged_from) {
        const HealthMinuteData *data = &s_minutes[(minute - logged_from) / SECONDS_PER_MINUTE];
        if (!data->is_invalid) logged_hr = data->heart_rate_bpm;
      }
      if (logged_hr > 0) {
        hr = logged_hr;
        hr_age = 0;
      } else {
        hr_age++;
      }

      input.asleep = prv_slept_at(&sleeps, minute);
      input.hr = hr_age <= BACKFILL_HR_CARRY_MIN ? hr : 0;
      prv_move(energy_rate(&input));
    }
    // Always forwards, even if the log hands back records from before what was asked for.
    from = logged_to > from ? logged_to : from + SECONDS_PER_MINUTE;
  }
}

// Once a day, folds last night's sleep into the running debt.
static bool prv_settle_sleep_debt(time_t now) {
  time_t today = time_start_of_today();
  if (s_store.debt_day == today || (now - today) < DEBT_HOUR * SECONDS_PER_HOUR) return false;
  s_store.debt_day = today;

  int slept = (int)health_service_sum_today(HealthMetricSleepSeconds) / SECONDS_PER_MINUTE;
  if (slept >= DEBT_MIN_SLEPT_MIN) {
    s_store.sleep_debt_min = (uint16_t)energy_sleep_debt(s_store.sleep_debt_min, slept);
  }
  return true;
}
#endif

bool energy_core_tick(time_t now, const Settings *settings, int stress) {
#if defined(PBL_HEALTH)
  prv_ensure_loaded();

  // A clock change can leave the last update in the future.
  if (s_store.updated_at > now) s_store.updated_at = now;
  int minutes = (int)((now - s_store.updated_at) / SECONDS_PER_MINUTE);
  if (minutes == 0) return false;
  bool dirty = prv_settle_sleep_debt(now);

  bool asleep = health_service_peek_current_activities() &
                (HealthActivitySleep | HealthActivityRestfulSleep);
  if (minutes > MAX_CATCH_UP_MIN) {
    prv_backfill(s_store.updated_at, now, settings);
    minutes = 0;  // None of them were watched, so they don't count towards the night
  } else {
    EnergyInput input = {
      .asleep = asleep,
      .assume_asleep = needs_core_in_quiet_hours(settings, now),
      .hr = (int)health_service_peek_current_value(HealthMetricHeartRateBPM),
      .rest_hr = prv_rest_hr(),
      .age = settings->age,
      .stress = stress,
      .night_quality = prv_night_quality()
    };
    prv_move(energy_rate(&input) * minutes);
  }
  s_store.updated_at = now;

  if (asleep) {
    s_store.asleep_at = now;
    s_store.night_min += minutes;
  } else if ((s_store.night_min > 0 || s_store.night_samples > 0) &&
             (now - s_store.asleep_at) >= NIGHT_OVER_SEC) {
    prv_close_night();
    dirty = true;
  }

  if (!dirty && energy_core_pct(&s_store) == s_saved_pct) return false;
  prv_save();
  return true;
#else
  return false;
#endif
}

void energy_core_night_sample(int32_t ln_q12, int hr) {
  prv_ensure_loaded();
  if (s_store.night_samples >= NIGHT_MAX_SAMPLES) return;
  s_store.night_ln_sum += ln_q12;
  s_store.night_hr_sum += (uint16_t)hr;
  s_store.night_samples++;
  prv_save();
}

void energy_core_wake_sample(int hr) {
  prv_ensure_loaded();
  int32_t hr_x16 = hr * 16;
  if (s_store.wake_hr_x16 == 0) {
    s_store.wake_hr_x16 = (uint16_t)hr_x16;
  } else {
    // Quick to fall and slow to rise, so it settles on the wearer at their calmest rather
    // than averaging in every reading taken just after climbing the stairs.
    int32_t diff = hr_x16 - s_store.wake_hr_x16;
    s_store.wake_hr_x16 = (uint16_t)(s_store.wake_hr_x16 + diff / (diff < 0 ? 16 : 64));
  }
  prv_save();
}
