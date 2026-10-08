#include "biomon.h"
#include "stress.h"

// HRV comes from the sensor's peak-to-peak (RR) intervals. The Goodix algorithm reports
// bursts of up to 4 consecutive intervals, each as its own HealthEventHRVUpdate, so RMSSD
// over a run of them is a real measurement rather than an approximation.
// Holding an HRV sample period keeps the PPG sensor and the HRV algorithm running, so the
// sensor is only opened for a short measurement window every so often rather than left on.
#define PERSIST_KEY_HRV 3
#define HRV_BUF_LEN 16             // RR intervals collected per window
#define HRV_MIN_DIFFS 4            // Successive differences needed for a usable RMSSD
#define HRV_PPI_MIN_MS 300         // 200 bpm - anything shorter is an artifact
#define HRV_PPI_MAX_MS 2000        // 30 bpm - anything longer is an artifact
#define HRV_MAX_DIFF_MS 250        // Reject ectopic beats and gaps between bursts
#define HRV_BURST_PERIOD_SEC 5     // Sample fast inside a window so it closes quickly
#define HRV_WINDOW_MAX_SEC 120     // Abandon a window that isn't filling up
#define HRV_WINDOW_MAX_STEPS 10    // Walking wrecks the optical signal; bin the window
#define HRV_STALE_SEC 7200         // Stop trusting a reading after 2 h of failed windows

typedef struct {
  Baseline baseline;
  int32_t current_ln;   // Lightly smoothed ln(RMSSD), the value scored against the baseline
  time_t result_at;     // When the last usable reading was taken
  time_t attempt_at;    // When the sensor was last opened, usable reading or not
  uint16_t rmssd;
} HrvStore;

static HrvStore s_store;

static void prv_publish(time_t now) {
  bool fresh = s_store.result_at != 0 && (now - s_store.result_at) <= HRV_STALE_SEC;
  bool learned = s_store.baseline.count >= STRESS_MIN_SAMPLES;

  g_bio.hrv = fresh ? s_store.rmssd : -1;
  g_bio.stress = (fresh && learned) ? stress_score(&s_store.baseline, s_store.current_ln) : -1;
  g_bio.calib_pct = learned ? 100 : (s_store.baseline.count * 100) / STRESS_MIN_SAMPLES;
  g_bio.calibrating = !learned && s_store.baseline.count > 0;
}

#if defined(BIOMON_HAS_HRV)
static uint16_t s_ppi[HRV_BUF_LEN];
static int s_ppi_count = 0;
static bool s_measuring = false;  // True only while the sensor is actually open
static time_t s_window_start = 0;
static int s_window_start_steps = 0;

static void prv_start_window(time_t now) {
  s_store.attempt_at = now;
  // Fails on watches without the HRV algorithm and when heart rate is switched off.
  if (!health_service_set_hrv_sample_period(HRV_BURST_PERIOD_SEC)) return;

  s_ppi_count = 0;
  s_measuring = true;
  s_window_start = now;
  s_window_start_steps = g_bio.steps;
}

static void prv_finish_window(time_t now) {
  health_service_set_hrv_sample_period(0);  // Let the sensor go back to sleep
  s_measuring = false;

  bool moved = (g_bio.steps - s_window_start_steps) > HRV_WINDOW_MAX_STEPS;
  int rmssd = moved ? 0 : stress_rmssd(s_ppi, s_ppi_count, HRV_MAX_DIFF_MS, HRV_MIN_DIFFS);
  if (rmssd > 0) {
    int32_t ln = stress_ln_q12((uint32_t)rmssd);
    // Averaging with the previous reading takes the edge off single noisy windows, but
    // only when that reading is recent enough to still describe the same state.
    bool recent = s_store.result_at != 0 &&
                  (now - s_store.result_at) <= 2 * g_settings.hrv_interval_min * SECONDS_PER_MINUTE;
    s_store.current_ln = recent ? (s_store.current_ln + ln) / 2 : ln;
    stress_baseline_add(&s_store.baseline, ln);
    s_store.rmssd = (uint16_t)rmssd;
    s_store.result_at = now;
  }

  persist_write_data(PERSIST_KEY_HRV, &s_store, sizeof(s_store));
  prv_publish(now);
  ui_redraw();
}
#endif

void hrv_init(void) {
  HrvStore stored;
  if (persist_read_data(PERSIST_KEY_HRV, &stored, sizeof(stored)) == (int)sizeof(stored)) {
    s_store = stored;
  }
  prv_publish(time(NULL));
}

void hrv_deinit(void) {
#if defined(BIOMON_HAS_HRV)
  // The request survives app exit if left set, so give the HRM its battery back.
  health_service_set_hrv_sample_period(0);
#endif
}

void hrv_reset_baseline(void) {
  memset(&s_store, 0, sizeof(s_store));
  persist_delete(PERSIST_KEY_HRV);
  prv_publish(time(NULL));
}

// Drives the measurement schedule. Called once a minute; the sensor is open only between
// prv_start_window() and prv_finish_window().
void hrv_tick(time_t now) {
#if defined(BIOMON_HAS_HRV)
  if (s_measuring) {
    if ((now - s_window_start) >= HRV_WINDOW_MAX_SEC) prv_finish_window(now);
    return;
  }

  prv_publish(now);

  // A clock change can leave the last attempt in the future; don't wait for it.
  if (s_store.attempt_at > now) s_store.attempt_at = 0;
  if ((now - s_store.attempt_at) < g_settings.hrv_interval_min * SECONDS_PER_MINUTE) return;

  // The baseline is meant to describe waking rest. Sleep reads much higher and would drag
  // it up, so nights are skipped entirely - which also spares the battery.
  if (vitals_is_asleep()) {
    s_store.attempt_at = now;
    return;
  }
  // Mid-walk readings are noise; try again next minute.
  if (vitals_is_moving()) return;

  prv_start_window(now);
#endif
}

void hrv_on_event(void) {
#if defined(BIOMON_HAS_HRV)
  if (!s_measuring) return;

  // A 0 here means off-wrist or no reading; out-of-range values are sensor artifacts.
  uint16_t ppi = health_service_peek_hrv_ppi_ms();
  if (ppi < HRV_PPI_MIN_MS || ppi > HRV_PPI_MAX_MS) return;

  s_ppi[s_ppi_count++] = ppi;
  // Close the window the moment there's enough data - every extra second is sensor power.
  if (s_ppi_count >= HRV_BUF_LEN) prv_finish_window(time(NULL));
#endif
}
