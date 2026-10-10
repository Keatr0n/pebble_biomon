#include "hrv_core.h"
#include "energy_core.h"

// HRV comes from the sensor's peak-to-peak (RR) intervals. The Goodix algorithm reports
// bursts of up to 4 consecutive intervals, each as its own HealthEventHRVUpdate. Beats
// can go missing between one burst and the next, so only differences inside a burst are
// counted towards RMSSD.
// Holding an HRV sample period keeps the PPG sensor and the HRV algorithm running, so the
// sensor is only opened for a short measurement window every so often rather than left on.
//
// Readings are taken round the clock but kept apart. Waking ones are scored against a
// baseline of other waking ones to give the stress figure. Sleeping ones run much higher,
// so they'd wreck that baseline; they go to the energy meter instead, which uses them to
// judge how restorative the night is.
//
// Persistent storage is the only copy of the baseline. It's re-read before every change
// so that a reset from the app's settings isn't undone by whoever is measuring.
#define HRV_BUF_LEN 24             // RR intervals collected per window
#define HRV_MIN_DIFFS 6            // Successive differences needed for a usable RMSSD
#define HRV_BURST_GAP_MS 250       // Intervals of one burst arrive closer together than this
#define HRV_PPI_MIN_MS 300         // 200 bpm - anything shorter is an artifact
#define HRV_PPI_MAX_MS 2000        // 30 bpm - anything longer is an artifact
#define HRV_MAX_DIFF_MS 250        // Reject ectopic beats and gaps between bursts
#define HRV_BURST_PERIOD_SEC 5     // Sample fast inside a window so it closes quickly
#define HRV_WINDOW_MAX_SEC 120     // Abandon a window that isn't filling up
#define HRV_WINDOW_MAX_STEPS 10    // Walking wrecks the optical signal; bin the window
#define MOVING_STEPS_PER_MIN 5
#define HRV_MIN_SPAN_SEC (2 * SECONDS_PER_DAY)  // The baseline must cover this long
#define HRV_HIGH_RUN 2             // High readings in a row before it counts as an alert
// A reading this far above the baseline is more likely a noisy signal or an irregular
// beat than sudden calm, so it's set aside - unless they keep coming, which makes it real.
#define HRV_SUSPECT_Z_Q8 (5 * 128)
#define HRV_SUSPECT_RUN 2
#define HRV_USUAL_MIN_SAMPLES (3 * STRESS_BASELINE_WINDOW)
#define HRV_WORN_DOWN_Z_Q8 128     // Half a standard deviation, for a whole week's readings

void hrv_core_load(HrvStore *store) {
  if (persist_read_data(PERSIST_KEY_HRV, store, sizeof(*store)) != (int)sizeof(*store)) {
    memset(store, 0, sizeof(*store));
  }
}

bool hrv_core_fresh(const HrvStore *store, const Settings *settings, time_t now) {
  // Two missed readings, and time for the third to finish.
  int32_t limit = 2 * settings->hrv_interval_min * SECONDS_PER_MINUTE + HRV_WINDOW_MAX_SEC;
  return store->result_at != 0 && (now - store->result_at) <= limit;
}

int hrv_core_calib_pct(const HrvStore *store, time_t now) {
  if (store->baseline.count == 0) return 0;
  int by_count = (store->baseline.count * 100) / STRESS_MIN_SAMPLES;
  int32_t span = now - store->first_at;
  if (span < 0) span = 0;
  if (span > HRV_MIN_SPAN_SEC) span = HRV_MIN_SPAN_SEC;
  int by_span = (int)((span * 100) / HRV_MIN_SPAN_SEC);

  int pct = by_count < by_span ? by_count : by_span;
  return pct > 100 ? 100 : pct;
}

int hrv_core_stress(const HrvStore *store, const Settings *settings, time_t now) {
  bool learned = hrv_core_calib_pct(store, now) == 100;
  if (!learned || !hrv_core_fresh(store, settings, now)) return -1;
  return stress_score(&store->baseline, store->current_ln);
}

bool hrv_core_stress_high(const HrvStore *store, const Settings *settings, time_t now) {
  return store->high_run >= HRV_HIGH_RUN && hrv_core_stress(store, settings, now) >= STRESS_HIGH;
}

bool hrv_core_worn_down(const HrvStore *store) {
  return store->usual.count >= HRV_USUAL_MIN_SAMPLES &&
         stress_z_q8(&store->usual, store->baseline.mean >> 8) <= -HRV_WORN_DOWN_Z_Q8;
}

#if defined(BIOMON_HAS_HRV)
static uint16_t s_ppi[HRV_BUF_LEN];
static int s_ppi_count = 0;
static uint32_t s_breaks = 0;         // Bit i: interval i doesn't follow straight on from i - 1
static int32_t s_arrived_ms = 0;      // When the last interval came in, from the window's start
static bool s_dropped = false;        // The last interval was an artifact and wasn't kept
static bool s_measuring = false;  // True only while the sensor is actually open
static time_t s_window_start = 0;
static int s_window_start_steps = 0;
static int s_interval_sec = 0;
static int s_steps_last_tick = -1;
static bool s_window_asleep = false;  // Whether the open window began during sleep

static int prv_steps_today(void) {
  return (int)health_service_sum_today(HealthMetricStepCount);
}

static void prv_save(const HrvStore *store) {
  persist_write_data(PERSIST_KEY_HRV, store, sizeof(*store));
}

// Takes a waking reading into the baselines and the score. Returns false if it was set
// aside instead.
static bool prv_waking_reading(HrvStore *store, int32_t ln, time_t now) {
  bool suspect = store->baseline.count >= STRESS_MIN_SAMPLES &&
                 stress_z_q8(&store->baseline, ln) > HRV_SUSPECT_Z_Q8;
  if (!suspect) {
    store->suspect_run = 0;
  } else if (store->suspect_run < HRV_SUSPECT_RUN) {
    store->suspect_run++;
    return false;
  }

  // Averaging with the previous reading takes the edge off single noisy windows, but
  // only when that reading is recent enough to still describe the same state.
  bool recent = store->result_at != 0 && (now - store->result_at) <= 2 * s_interval_sec;
  store->current_ln = recent ? (store->current_ln + ln) / 2 : ln;
  if (store->baseline.count == 0) store->first_at = now;
  stress_baseline_add(&store->baseline, ln, STRESS_BASELINE_WINDOW);
  stress_baseline_add(&store->usual, ln, STRESS_USUAL_WINDOW);
  store->result_at = now;

  if (stress_score(&store->baseline, store->current_ln) < STRESS_HIGH) {
    store->high_run = 0;
  } else if (!recent) {
    store->high_run = 1;
  } else if (store->high_run < UINT8_MAX) {
    store->high_run++;
  }
  return true;
}

static void prv_finish_window(time_t now) {
  health_service_set_hrv_sample_period(0);  // Let the sensor go back to sleep
  s_measuring = false;

  HrvStore store;
  hrv_core_load(&store);
  store.attempt_at = now;

  bool moved = (prv_steps_today() - s_window_start_steps) > HRV_WINDOW_MAX_STEPS;
  int rmssd = moved ? 0 : stress_rmssd(s_ppi, s_ppi_count, s_breaks, HRV_MAX_DIFF_MS,
                                       HRV_MIN_DIFFS);
  if (rmssd > 0) {
    // The same intervals give the heart rate, which saves asking for it separately.
    uint32_t ppi_sum = 0;
    for (int i = 0; i < s_ppi_count; i++) ppi_sum += s_ppi[i];
    int hr = (int)((60000u * (uint32_t)s_ppi_count) / ppi_sum);
    int32_t ln = stress_ln_q12((uint32_t)rmssd);

    if (s_window_asleep) {
      energy_core_night_sample(ln, hr);
    } else if (prv_waking_reading(&store, ln, now)) {
      store.rmssd = (uint16_t)rmssd;
      energy_core_wake_sample(hr);
    }
  }
  prv_save(&store);
}
#endif

bool hrv_core_tick(time_t now, const Settings *settings) {
#if defined(BIOMON_HAS_HRV)
  int steps = prv_steps_today();
  // The count resets at midnight, which would otherwise read as a huge negative burst.
  bool moving = s_steps_last_tick >= 0 && steps >= s_steps_last_tick &&
                (steps - s_steps_last_tick) >= MOVING_STEPS_PER_MIN;
  s_steps_last_tick = steps;

  if (s_measuring) {
    if ((now - s_window_start) < HRV_WINDOW_MAX_SEC) return false;
    prv_finish_window(now);
    return true;
  }

  HrvStore store;
  hrv_core_load(&store);
  s_interval_sec = settings->hrv_interval_min * SECONDS_PER_MINUTE;
  // A clock change can leave the last attempt in the future; don't wait for it.
  if (store.attempt_at > now) store.attempt_at = 0;
  if ((now - store.attempt_at) < s_interval_sec) return false;

  bool asleep = health_service_peek_current_activities() &
                (HealthActivitySleep | HealthActivityRestfulSleep);
  // Mid-walk readings are noise; try again next minute.
  if (moving && !asleep) return false;

  store.attempt_at = now;
  prv_save(&store);
  // Fails on watches without the HRV algorithm and when heart rate is switched off.
  if (!health_service_set_hrv_sample_period(HRV_BURST_PERIOD_SEC)) return false;

  s_ppi_count = 0;
  s_breaks = 0;
  s_arrived_ms = 0;
  s_dropped = false;
  s_measuring = true;
  s_window_start = now;
  s_window_start_steps = steps;
  s_window_asleep = asleep;
#endif
  return false;
}

bool hrv_core_on_event(void) {
#if defined(BIOMON_HAS_HRV)
  if (!s_measuring) return false;

  // A 0 here means off-wrist or no reading; out-of-range values are sensor artifacts.
  uint16_t ppi = health_service_peek_hrv_ppi_ms();
  if (ppi < HRV_PPI_MIN_MS || ppi > HRV_PPI_MAX_MS) {
    s_dropped = true;
    return false;
  }

  time_t seconds;
  uint16_t ms;
  time_ms(&seconds, &ms);
  int32_t arrived_ms = (int32_t)(seconds - s_window_start) * 1000 + ms;
  int32_t gap = arrived_ms - s_arrived_ms;
  int32_t off_beat = gap > ppi ? gap - ppi : ppi - gap;
  // Intervals of one burst arrive together. Should the sensor ever hand them over as
  // they happen instead, each arrives one beat after the last. Anything else has a gap.
  bool follows = s_ppi_count > 0 && !s_dropped &&
                 (gap < HRV_BURST_GAP_MS || off_beat < ppi / 4);
  if (!follows) s_breaks |= 1u << s_ppi_count;
  s_arrived_ms = arrived_ms;
  s_dropped = false;

  s_ppi[s_ppi_count++] = ppi;
  // Close the window the moment there's enough data - every extra second is sensor power.
  if (s_ppi_count < HRV_BUF_LEN) return false;
  prv_finish_window(time(NULL));
  return true;
#else
  return false;
#endif
}

void hrv_core_stop(void) {
#if defined(BIOMON_HAS_HRV)
  // The request survives process exit if left set, so give the HRM its battery back.
  if (s_measuring) health_service_set_hrv_sample_period(0);
  s_measuring = false;
#endif
}
