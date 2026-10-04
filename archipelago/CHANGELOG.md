# Changelog

## 0.2.0
- New options: `starting_weapon` (vanilla / random handgun / random weapon, with ammo), `starting_supplies`,
  `starting_pesetas`. Archipelago's `start_inventory` also works.
- New option `enemy_randomizer_compat` for playing alongside re_duke's RE4 Enemy/Merchant Randomizer: turns off
  the checks that mod changes (Merchant, bosses, medallion reward); the goal triggers at the jet-ski escape.
- Tests: APWorld option/logic/data tests (57), game-module harness grows to 34 scenarios and runs in CI under wine.

## 0.1.0
- First public build: ~700 checks (key items, weapons, treasures, consumables, bosses, Merchant, shooting gallery,
  medallion reward), launcher client, re4_tweaks-based game mod.
