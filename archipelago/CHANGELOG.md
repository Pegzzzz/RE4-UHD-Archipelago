# Changelog

## 0.3.0
- All-in-one setup: the client's new `/setup` command finds the game (Steam libraries or a given folder), installs
  the game mod bundled inside the APWorld (backing up the old `dinput8.dll`), and sets up re_duke's randomizer.
- re_duke's RE4 PC Randomizer support (`re_duke_randomizer` and sub-options): the client writes an "Archipelago"
  profile built from the user's own installed presets (doors, items and key items forced off, fixed seed number
  per slot), opens the generator, and checks the generated seed's settings when connecting. `/rando` redoes it.
- `enemy_randomizer_compat` is replaced by `re_duke_randomizer` (old YAMLs still work).
- Checked offline that the game mod's hooks resolve on the randomizer's patched bio4.exe exactly like
  re4_tweaks 1.9.1, which the randomizer ships.

## 0.2.0
- New options: `starting_weapon` (vanilla / random handgun / random weapon, with ammo), `starting_supplies`,
  `starting_pesetas`. Archipelago's `start_inventory` also works.
- New option `enemy_randomizer_compat` for playing alongside re_duke's RE4 Enemy/Merchant Randomizer: turns off
  the checks that mod changes (Merchant, bosses, medallion reward); the goal triggers at the jet-ski escape.
- Tests: APWorld option/logic/data tests (57), game-module harness grows to 34 scenarios and runs in CI under wine.

## 0.1.0
- First public build: ~700 checks (key items, weapons, treasures, consumables, bosses, Merchant, shooting gallery,
  medallion reward), launcher client, re4_tweaks-based game mod.
