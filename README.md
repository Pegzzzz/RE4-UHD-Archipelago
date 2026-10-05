# RE4 UHD Archipelago

An [Archipelago](https://archipelago.gg) multiworld randomizer for **Resident Evil 4 (Steam Ultimate HD Edition)**.

**→ New here? Start with the [tutorial](TUTORIAL.md).** Downloads are on the
[Releases page](https://github.com/Pegzzzz/RE4-UHD-Archipelago/releases).

## Features

- **About 750 checks:** key items, weapons found in the world, every fixed treasure, the ammo, herbs, grenades
  and sprays placed in the world, and placed pesetas (enemy drops never count). Optional: boss kills, Merchant
  purchases, shooting-gallery bottle caps and the blue medallion reward.
- **Goal:** defeat Saddler.
- **Starting inventory options:** random starting weapon, supplies and pesetas, plus Archipelago's `start_inventory`.
- **DeathLink** (experimental).
- **One-command setup:** the client's `/setup` finds your game, installs the mod and keeps a backup of what it replaces.
- **Random enemy health (built in):** from mild to chaos.
- **Random enemies (optional):** works with [re_duke's RE4 PC Randomizer](https://www.moddb.com/mods/re4randomizer)
  (separate download). `/setup` configures it so it's safe for Archipelago.
- Items arrive in game with on-screen messages, and your save remembers what it has received, so dying or reloading
  is safe.

## Status

Early release. Everything is tested against a simulated game, but it hasn't had a full in-game playthrough yet.
Some item locations come from community guides and may be off: the mod logs anything it can't match to
`Bin32/re4_tweaks/archipelago.log`. Please [open an issue](https://github.com/Pegzzzz/RE4-UHD-Archipelago/issues)
with that file if something goes wrong.

## For developers

| Path | What |
|---|---|
| `dllmain/Archipelago.cpp` | Game-side module: localhost bridge, check detection, item delivery, overlay |
| `archipelago/apworld/re4uhd/` | The APWorld (items, locations, logic, options) and the launcher client |
| `archipelago/apworld/re4uhd/rando_bridge.py` | `/setup`: finds the game, installs the mod, drives re_duke's randomizer |
| `archipelago/tests/harness/` | Runs the game module against a simulated game under wine (63 scenarios) |
| `archipelago/tools/` | Research data, `build_data.py` (regenerates the location table), packaging, `pattern_scan.py` |
| `.github/workflows/archipelago.yml` | Builds `dinput8.dll` and `re4uhd.apworld`, runs all tests, publishes releases |

More notes in [archipelago/README.md](archipelago/README.md) and the [changelog](archipelago/CHANGELOG.md).

## Credits

- Archipelago integration: **Pegz**
- The game mod is a modified version of [re4_tweaks](https://github.com/nipkownix/re4_tweaks) by nipkownix,
  emoose and contributors, used under its license (see [License.txt](License.txt)).
- [Archipelago](https://archipelago.gg) and its community
- Item location research: [Evil Resource](https://www.evilresource.com), StrategyWiki, the RE4 modding community
