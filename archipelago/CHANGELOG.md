# Changelog

## 0.4.1
- The client now checks re_duke's randomizer whenever it's installed, even if the YAML doesn't use it, and
  warns (in the client and in game) when its last seed moved items or doors, which makes checks miss.
  `/setup` and `/rando` then write a safe profile: random enemies only, Merchant left alone when the YAML
  has Merchant checks.
- Diagnostics in `archipelago.log`: a `[pickup]` line per item (pickup screen / shop / no screen) and
  `[roomflag]` lines when the game marks a placed item as taken, to tell placed items from enemy drops.

## 0.4.0
- `merchant_purchases: check_only` (new default): the first purchase of each Merchant item only sends the check;
  the item is taken back when you leave the shop, and the Merchant's stock (weapons, scopes, stocks, treasure maps,
  attache cases as Progressive Attache Case, tactical vest) is shuffled into the multiworld. `keep_item` restores
  the old behaviour. Off with re_duke's randomizer (its Merchant stock is random).
- Treasures count more reliably: a treasure that isn't listed for the room it's found in now matches the same
  item anywhere in the stage, and treasures nothing accounts for (random drops, data gaps) use new
  `bonus_treasure_checks` (filler-only, 5 per stage by default).
- Unverified placements (2-3 Red Gem, a 2-3 grenade) are back in as filler-only checks.
- Location and item ids from 0.3.0 are unchanged; new ones are appended.

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
