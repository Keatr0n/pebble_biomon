# pebble_biomon

A health and bio tracking app for the Pebble Time 2 (emery) and Pebble Round 2 (gabbro), dressed up as a video game HUD. Pick between Subnautica, Rust and Cyberpunk 2077 looks in the settings.

Biomon is a toy, not a medical device. Its heart rate and HRV come from a wrist sensor, and the stress, energy, water and food figures are estimates built on top of that and on what you log. The wording is game HUD flavour: "STARVING" means the food meter is low, not that anything has been diagnosed. Don't use it to make decisions about your health.

## What it tracks

- Heart rate, HRV (RMSSD), steps, sleep and battery
- Energy, shown as the HUD's health bar: recharges in sleep, drains with activity and stress
- Stress, scored against your own HRV baseline
- Water and food meters that drain through the day, refill when you log a drink or meal, and buzz you when they get low

## Settings

Everything is set on the watch; there's no phone settings page. Hold Select on the main screen to open the menu. Select flips a switch or steps through a list, and for a number it opens a picker where Up and Down change it, Select saves and Back cancels. Changes take effect as you make them.

- Look: the HUD style
- About you: sex, age, weight (kg) and height (cm), used to size the water and food meters
- Water and food: whether each is tracked, how often a reminder repeats (0 for once only), vibration, and ml or fl oz, kJ or kcal
- Quiet hours: the hours they run from and until; the same hour for both turns them off
- Stress and energy: how often HRV is measured, and Reset baselines, which forgets what the watch has learned about your stress and energy. It asks for a second press first.

## Water and food

Both are game-style meters that drain while you're awake and get topped up when you log a drink or a meal.

The drain is uneven on purpose. Water falls from full to 50% in about two hours, which is where the watch vibrates and asks you to drink. Below that it slows right down: 25% takes another six hours, 10% another day, and empty two days after that. Food follows the same shape more slowly, reaching 50% after about four hours. Reminders repeat every 20 minutes while a meter stays under 50%, and the meter turns red under 25%.

The meters are timers, not measurements: they know what you've logged and how long ago, and nothing else. A low meter means it's been a while, or that you forgot to log.

To log something, press Select and pick from the menu. It's a short tree:

- Food, then how big: nibble, snack, light meal, meal, big meal or feast, running from 400 to 5000 kJ (100 to 1200 kcal), each with an example to judge by
- Drink, then what: water, coffee, tea, milky coffee, juice, soft drink or milk
- then how much of it: small cup (150 ml), cup (250 ml), mug or can (375 ml), bottle (600 ml) or large bottle (1 litre)

Up goes straight to the drinks and Down straight to the food, and so does tapping a meter. The lists can be scrolled and tapped by touch as well. Picking an amount logs it and returns to the HUD; Back goes up a level.

Every drink counts in full towards water. The ones with energy in them (milky coffee, juice, soft drink and milk) add to food as well, so a cup of juice moves both meters.

After you've logged something the top of the menu has an Undo last row, which takes the most recent entry back. It stays until the day's totals reset.

The menu also shows your running total for the day against your estimated requirement, marked with a ~ because it is an estimate. The requirement is left off until you've set your details in the settings, since until then it describes the defaults rather than you.

A cup or a meal always refills the same slice of bar. The size of that slice comes from the sex, age, weight and height in the settings:

- Water to drink is 80% of 31-35 ml per kg of body weight, the rest being what arrives in food, and is kept between 1.5 and 3.5 litres whatever the weight.
- Energy is the Mifflin-St Jeor resting figure times 1.2, plus what the watch counts you burning in activity on a typical day. Until the watch has that history it uses a flat 1.4 instead.

It's scaled so that getting through a normal day's worth keeps both meters in their top half. For a 75 kg person a cup is about half of the water bar and a 2500 kJ meal a bit over half of the food bar.

A big meal isn't wasted on a full bar. The meters carry on filling out of sight, food up to a whole day's worth and water up to half a bar extra, so the bar stays full for longer before it starts to fall. Once the day's total reaches your requirement the reminders stop until tomorrow, even if a meter runs low.

Nothing drains or buzzes during quiet hours (22:00 to 07:00 by default), and nothing buzzes while the watch thinks you're asleep.

## Why it's an app and not a watch face

PebbleOS gives watch faces no buttons and no touch, which leaves shaking your wrist as the only way to log anything. So Biomon is a watchapp with a background worker.

The worker keeps running after you leave the app. It takes the HRV readings, keeps the baselines up to date, moves the energy meter and watches the water and food meters. A worker can't vibrate or draw, so when a meter drops under 50% it opens the app, which buzzes. Nothing pops up over the HUD: the low meter and the status line say what it's after, and the app stays open until you leave it.

Only one app's worker can run at a time across the whole watch. If another app's worker has the slot, Biomon still measures and reminds, but only while it's open. The energy meter catches up from the watch's health log the next time it is.

## Stress

The watch takes a short HRV reading every 15 minutes. The ones taken while you're awake and still are used for stress: it learns what's normal for you. Once it has at least 20 readings spread over two days it starts scoring each new one against that baseline: lower variability than your usual means more stress. The baseline keeps adapting, weighted towards roughly the last 400 readings.

What it calls stress is really "HRV lower than your usual". Worry does that, but so do coffee, alcohol, a big meal, standing up, a cold coming on and the time of day, and the watch can't tell them apart.

A few things keep the score honest:

- A reading is about two dozen heartbeats. If too many of them look like sensor noise the whole reading is thrown away rather than patched up.
- A reading far above your baseline is more likely a noisy signal or an irregular beat than sudden calm, so it's set aside. If three in a row agree, it's believed.
- The score disappears once it's two readings old. Walking stops readings being taken, so after a long walk there's no score until you've sat down again.
- The status line only raises a stress alert after two high readings in a row.

Because the baseline adapts, a rough month slowly becomes the new normal and scores as ordinary. To catch that, the watch keeps a second baseline over about six weeks. When the last week sits well below it, the status line says so (CHRONIC STRESS DETECTED, WORN DOWN or CHRONIC OVERLOAD, depending on the look). This needs about three weeks of readings before it can fire.

Stress shows up in the status line when it's high, and as a number in the Rust look's status bar. When there's no heart rate and no stress score at all, the status line says there's no signal rather than that all is well.

HRV needs a watch with the HRV-capable heart rate sensor. Without one there's no stress score, nights all count as ordinary, and everything else carries on.

## Energy

The health bar is a body-battery style energy meter.

Asleep, it recharges. Eight hours of ordinary sleep puts back about 70%. The watch keeps taking HRV readings through the night and reduces each night to an average HRV and heart rate. Once it has three nights to compare against (it remembers about two weeks), a night with lower HRV or a higher heart rate than your usual recharges more slowly, down to 0.4x, and a better one faster, up to 1.3x. Sleeping readings are kept out of the stress baseline, because HRV runs much higher in sleep.

Awake, it drains: 2.5% an hour at rest, more as stress rises above your baseline, and much more with exertion (roughly 12% an hour for moderate exercise, 24% for hard). Exertion is judged by how far your heart rate is above your usual rate when awake and still, which the watch learns from the daytime HRV readings. When you're properly relaxed it creeps back up instead. Under 20% the HUD tells you to rest.

Short nights add up. Each night under seven hours adds the shortfall to a sleep debt, which fades by a quarter a day and is paid down faster by a long night. Every 12 minutes of debt takes 1% off how far the meter can recharge, down to a ceiling of 50%, so a run of five-hour nights tops out in the low 60s however calm the days are.

If the watch is off your wrist the meter holds still, except during quiet hours, when it assumes an ordinary night's sleep. It has no way of knowing whether you actually slept.

If nothing was measuring for a while, because another app's worker had the slot or the watch restarted, the meter is caught up from the watch's own log of sleep and heart rate for up to the last 24 hours. Stress isn't known for that stretch, so it's left out.

These rates are estimates chosen to give a believable day; they live at the top of `shared/energy.c` if you want to tune them.

## Building

```
nix develop
pebble build
pebble install --emulator emery
```

`BIOMON_DEMO=1 pebble build` fills the face with canned readings for screenshots, since the emulator can't produce HRV. `BIOMON_DEMO=2` does the same with everything in the red, and `3` / `4` open the log menu at drinks / food.
