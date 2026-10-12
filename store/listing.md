# Store listing

**Name:** Biomon
**Category:** Health & Fitness
**Source:** https://github.com/Keatr0n/pebble_biomon
**Platforms:** emery (Pebble Time 2), gabbro (Pebble Round 2)

## Description

Your body, as a video game HUD.

Biomon turns your watch into a biomonitor. It reads your heart rate and HRV, works out how stressed and how drained you are, and shows the lot as health bars. Pick the look in the on-watch settings: Subnautica, Rust or Cyberpunk 2077.

- Heart rate, HRV, steps, sleep and battery at a glance
- Energy bar that recharges while you sleep and drains with activity and stress
- Stress scored against your own HRV baseline, not a population average
- Water and food meters that drain through the day and buzz you when they run low
- Log food and drink from a short menu: a meal size, or a cup or bottle of water, coffee, tea, juice and more
- Quiet hours, metric or imperial units, kJ or kcal
- Every setting is on the watch: hold Select to open the menu, no phone needed
- Keeps measuring in the background after you leave the app

Biomon is a toy, not a medical device. The stress, energy, water and food figures are estimates, and the wording is game flavour: "STARVING" means the food meter is low. Don't use it to make decisions about your health.

Stress needs about two days of wear before it has a baseline to score against.

## Release notes (1.0.0)

First release.

## Assets

| File | Size | Used for |
| --- | --- | --- |
| `icon_large.png` | 144x144 | Store icon |
| `icon_small.png` | 48x48 | Store list icon |
| `banner.png` | 720x320 | Header image, if the dashboard asks for one |
| `../resources/images/menu_icon.png` | 25x25 | Launcher icon, built into the app |
| `../screenshots/*_d1.png`, `*_d2.png`, `*_d3.png` | per platform | Screenshots |

`python3 store/make_assets.py` redraws the icons and the banner.
