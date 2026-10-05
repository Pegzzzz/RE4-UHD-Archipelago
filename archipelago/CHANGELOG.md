# Changelog

## 0.5.3
Second audit (independent code review plus a logic check against the community Manual, Evil Resource and
StrategyWiki):
- **Logic fixes that could make a seed unwinnable:** the 4-1 pit, waterway and Verdugo rooms now need the Grails and
  ornaments; the 3-2 hedge maze (both Moonstones) and chapter 3-3 need the Gallery Key and Goat Ornament; shooting
  gallery A needs the swords, Castle Gate Key and Prison Key, gallery B the Grails; the Storage Room Card Key (Iron
  Maiden) needs the Infrared Scope, which is now a progression item. 40 test seeds with mixed options all beatable.
- Filler-only now: Merchant checks and the medallion reward (he only sells the next attache case and may not sell
  a gun you already own), and missable or doubtful pickups (Dr. Salvador's Rubies, the crow's Spinel, the 4-1
  Treasure Chamber needing Ashley, Verdugo's Crown Jewel, five Spinels the Manual doesn't list).
- The Pearl Pendant and Brass Pocket Watch also count in their "Dirty" form (dropped in the well).
- Nothing is sent to the client under the last session's config, and checks/goal carry the save's seed tag, so a
  save from another seed can never check or finish someone else's slot. `/bindsave` needs a live connection.
- A case purchase takes the first unchecked case check (received cases skip sizes); buying back a gun you just sold
  keeps it.
- Timers only run in "world time" (not during room loads or Options), so a door between a pickup and its flag no
  longer turns it into a drop. Room snapshots reset on load/death/seed change (no phantom flags). Barrel contents
  left behind can't be "used" by a later drop.
- "Already held key item" detection is skipped right after a real pickup (late flags). Bosses count from their
  dying routine instead of an HP threshold, and Options doesn't end tracking. A key item that can't be added is
  retried (but a non-stacking copy Leon already holds counts as delivered, and it gives up after 30 tries).
- Messages to the client go through a send queue on their own thread (the game never waits on the client), and a
  half-written message drops the connection instead of corrupting it.
- The received-item list now carries where each item was found; the game skips copies Leon kept at their own
  location (the save's index always counts the same list).

## 0.5.2
A full audit of ways a check, an item or a run could be lost (from the real session log plus a code review):
- **Merchant rooms:** the game keeps its "near the Merchant" flag on the whole time Leon is in a Merchant's area,
  so every pickup there counted as a purchase (the 1-2 Emblem halves sent nothing, removals and received items
  waited). Purchases are now told apart by the shop menu or by pesetas being paid; the free Punisher next to the
  Merchant is the medallion reward; sales don't count as pesetas pickups.
- Merchant check-only: attache cases are never taken back (the Merchant sold them again); a gun equipped right in
  the shop is taken once you switch weapons (toast) instead of being left with you; buying again after a death
  takes the item again, like pickups.
- A new game is only linked to a seed by the live client, never by the last session's config read at startup.
- Bosses: a boss removed by its death cutscene at low HP, or last seen dead when the room/cutscene changes, counts.
  Escaping (Verdugo's elevator) or dying doesn't.
- A key item Leon already holds (his copy came from the multiworld): picking up the vanilla one still sends the
  check, from the room's flag, even if the game doesn't add a second copy.
- Room flags are watched for 5 s after leaving a room (a pickup's flag lands a moment after it), and hidden-item
  credits survive leaving and coming back.
- A received key item the game refuses is retried instead of lost.
- A warning when leaving the village or castle with item checks behind (Area Jump can take you back).
- Pending ammo/pesetas decisions are dropped on death, so a rolled-back inventory is never touched.
- APWorld: boss checks, Buy Handgun and the four special bottle caps are filler-only (not always obtainable /
  detection unconfirmed). The Holy Beast pieces stay on their own spots (they're inside Krauser's arena, which
  only opens with all three). Locations that hold their own vanilla item keep it in Leon's hands and the client
  doesn't deliver a second copy (also makes `shuffle_key_items: false` work offline).
- Client: checks made while the room is unreachable are sent when it's back; a bad message from the game no longer
  stops the game link; a goal from an earlier slot doesn't carry over.

## 0.5.1
Fixes from the first real session's log:
- Treasures and key items picked up without the mod seeing a pickup screen (most of them: they reach the case
  before the screen opens) now count. Before, only some did, so e.g. the Old House Road Spinels sent nothing.
  A treasure that appears because two others were combined in the inventory still doesn't count.
- Items hidden in barrels and crates (and knocked-down ones) count when you pick them up later: the game flags
  them when they appear, not when they're taken, so they used to be mistaken for enemy drops.
- `pesetas_checks` is now a number per stage (0-50, default 25): the game has far more placed pesetas than the
  Manual's list (24 in chapter 1-1 alone). `true`/`false` from 0.5.0 YAMLs still work.
- Merchant check-only: stocks are no longer "taken back" (they attach to the gun, so it failed) and, like the
  tactical vest, aren't added to the item pool.
- New client command `/check <location>` sends one of your own locations by hand, for checks the mod missed.

## 0.5.0
- Enemy drops no longer use up a room's ammo/herb checks. The game marks placed items as taken in the room's
  save data; a pickup only counts when that flag flips with it. If a game never shows those flags, the mod falls
  back to the old behaviour on its own after a few pickups (and learns it per install).
- New `pesetas_checks` (default on): 60 placed pesetas pickups (12 village, 37 castle, 11 island, counted from the
  community Manual APWorld) are filler-only checks; you keep the money, drops don't count.
- New `random_enemy_health` (built in, no other download): `mild`, `tough`, `wild`, `chaos`.
- re_duke's randomizer: Merchant checks and the medallion reward now stay on when `re_duke_merchant` is off, so
  "random enemies only" keeps them. Boss checks are only turned off with `re_duke_enemies`.
- The save's location bitset grows from 768 to 1024 (save work slots 52-59, which were reserved and empty).
- Location and item ids from 0.4 are unchanged; new ones are appended.

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
