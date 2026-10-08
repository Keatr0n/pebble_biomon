# pebble_biomon

A health and bio tracking watch face for the Pebble Time 2 (emery) and Pebble Round 2 (gabbro), dressed up as a video game HUD. Pick between Subnautica, Rust and Cyberpunk 2077 looks in the settings.

## What it tracks

- Heart rate, HRV (RMSSD), steps, sleep and battery
- Stress, shown as the HUD's health bar (full = calm)
- Water and food meters that drain through the day, refill when you log a drink or meal, and buzz you when they get low

## Water and food

Both are game-style meters that drain while you're awake and get topped up when you log a drink or a meal.

The drain is uneven on purpose. Water falls from full to 50% in about two hours, which is where the watch vibrates and asks you to drink. Below that it slows right down: 25% takes another six hours, 10% another day, and empty two days after that. Food follows the same shape more slowly, reaching 50% after about four hours. Reminders repeat every 20 minutes while a meter stays under 50%, and the meter turns red under 25%.

To log something, shake your wrist. The first shake opens a picker and selects nothing, so a stray flick does no harm. Each further shake steps to the next amount: one or two cups of water (250 ml / 8.5 fl oz each), then a small, medium or large meal (1000, 2000 or 3000 kJ; 300, 500 or 700 kcal). Whichever meter is emptier comes first. Hold still for four seconds and the amount showing is logged; shake past the last one to back out. The picker also shows your running total for the day against your estimated requirement.

Watch faces can't use the touchscreen: PebbleOS reserves touch for watchapps. The code for tapping a meter and adjusting the amount with + and - buttons is still there and will start working if a firmware ever lifts that.

A cup or a meal always refills the same slice of bar. The size of that slice comes from the sex, age, weight and height in the settings: water need from body weight (31-35 ml per kg), energy need from the Mifflin-St Jeor equation at a lightly active level. It's scaled so that getting through a normal day's worth keeps both meters in their top half. For a 75 kg person a cup is about 40% of the water bar and a 2000 kJ meal about 43% of the food bar.

Nothing drains or buzzes during quiet hours (22:00 to 07:00 by default), and nothing buzzes while the watch thinks you're asleep.

## Stress

The watch takes a short HRV reading every 15 minutes while you're awake and still, and learns what's normal for you. After about 20 readings it starts scoring each new one against that baseline: lower variability than your usual means more stress. The baseline keeps adapting, weighted towards roughly the last 400 readings.

HRV needs a watch with the HRV-capable heart rate sensor. Without one the stress bar just shows `--` and everything else carries on.

## Building

```
nix develop
pebble build
pebble install --emulator emery
```

`BIOMON_DEMO=1 pebble build` fills the face with canned readings for screenshots, since the emulator can't produce HRV. `BIOMON_DEMO=2` does the same with everything in the red, and `3` / `4` open the water / food log panel.
