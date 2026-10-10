#include "biomon.h"
#include "hrv_core.h"

#if defined(PBL_HEALTH)
static bool prv_accessible(HealthMetric metric, time_t start, time_t end) {
  return health_service_metric_accessible(metric, start, end) &
         HealthServiceAccessibilityMaskAvailable;
}

static void prv_update_hr(void) {
  time_t now = time(NULL);
  int hr = 0;
  if (prv_accessible(HealthMetricHeartRateBPM, now, now)) {
    hr = (int)health_service_peek_current_value(HealthMetricHeartRateBPM);
  }
  g_bio.hr = hr > 0 ? hr : -1;
}

static void prv_update_steps(void) {
  time_t now = time(NULL);
  g_bio.steps = prv_accessible(HealthMetricStepCount, time_start_of_today(), now)
                    ? (int)health_service_sum_today(HealthMetricStepCount)
                    : -1;
}

static void prv_update_sleep(void) {
  time_t now = time(NULL);
  g_bio.sleep_min = prv_accessible(HealthMetricSleepSeconds, time_start_of_today(), now)
                        ? (int)health_service_sum_today(HealthMetricSleepSeconds) / 60
                        : -1;
}

static void prv_push_hr(int hr) {
  if (hr <= 0) return;
  if (g_bio.hr_hist_len == HR_HIST_LEN) {
    memmove(g_bio.hr_hist, g_bio.hr_hist + 1, HR_HIST_LEN - 1);
    g_bio.hr_hist_len--;
  }
  g_bio.hr_hist[g_bio.hr_hist_len++] = (uint8_t)(hr > 255 ? 255 : hr);
}

// Seeds the heart rate trace from the watch's own log, so the graph isn't empty every
// time the face is reopened.
static void prv_load_hr_history(void) {
  HealthMinuteData *minutes = malloc(HR_HIST_LEN * sizeof(HealthMinuteData));
  if (!minutes) return;

  time_t end = time(NULL);
  time_t start = end - HR_HIST_LEN * SECONDS_PER_MINUTE;
  uint32_t count = health_service_get_minute_history(minutes, HR_HIST_LEN, &start, &end);
  for (uint32_t i = 0; i < count; i++) {
    if (!minutes[i].is_invalid) prv_push_hr(minutes[i].heart_rate_bpm);
  }
  free(minutes);
}

static void prv_health_handler(HealthEventType event, void *context) {
#if defined(BIOMON_HAS_HRV)
  // A burst of RR intervals arrives as several back-to-back events; none of them change
  // anything on screen until the measurement window closes.
  if (event == HealthEventHRVUpdate) {
    // Only does anything when the app is measuring for itself; see main.c.
    if (hrv_core_on_event()) {
      hrv_refresh();
      ui_redraw();
    }
    return;
  }
#endif

  Bio before = g_bio;
  if (event == HealthEventSignificantUpdate) {
    prv_update_hr();
    prv_update_steps();
    prv_update_sleep();
  } else if (event == HealthEventMovementUpdate) {
    prv_update_steps();
  } else if (event == HealthEventSleepUpdate) {
    prv_update_sleep();
  } else if (event == HealthEventHeartRateUpdate) {
    prv_update_hr();
  }

  if (before.hr != g_bio.hr || before.steps != g_bio.steps ||
      before.sleep_min != g_bio.sleep_min) {
    ui_redraw();
  }
}
#endif

void vitals_init(void) {
  g_bio.hr = -1;
  g_bio.steps = -1;
  g_bio.sleep_min = -1;
#if defined(PBL_HEALTH)
  health_service_events_subscribe(prv_health_handler, NULL);
  prv_update_hr();
  prv_update_steps();
  prv_update_sleep();
  prv_load_hr_history();
#endif
}

void vitals_deinit(void) {
#if defined(PBL_HEALTH)
  health_service_events_unsubscribe();
#endif
}

void vitals_tick(void) {
#if defined(PBL_HEALTH)
  prv_update_hr();
  prv_update_steps();
  prv_update_sleep();
  prv_push_hr(g_bio.hr);
#endif
}
