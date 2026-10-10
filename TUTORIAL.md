# Playing Resident Evil 4 UHD in Archipelago — Tutorial

This guide takes you from nothing to playing RE4 in an Archipelago multiworld.
It takes about 15 minutes the first time.

> **Status: early release (v0.5.6).** Everything is tested against a simulated game, but this is the first public
> build, so expect rough edges. Please report problems (see [Reporting problems](#reporting-problems)).

---

## 1. What you need

| Thing | Where to get it |
|---|---|
| Resident Evil 4 on Steam (the **Ultimate HD Edition**, `bio4.exe`) | Steam |
| Archipelago **0.6.7 or newer** | [Archipelago releases](https://github.com/ArchipelagoMW/Archipelago/releases) — install it like any program |
| This mod's release files | The [**Releases** page](https://github.com/Pegzzzz/RE4-UHD-Archipelago/releases) of this repository |

From the latest release, download:

- `re4uhd.apworld` — teaches Archipelago about RE4, and contains the game mod
- `Resident.Evil.4.UHD.yaml` — your settings file (GitHub swaps the spaces for dots; the name doesn't matter)
- *(only for installing by hand)* `RE4-UHD-Archipelago.zip` — the same game mod as a zip

**Optional:** [re_duke's RE4 PC Randomizer](https://www.moddb.com/mods/re4randomizer) (or the newer build from
re_duke's Patreon), if you want random enemy *types*. See [section 3b](#3b-optional-re_dukes-randomizer-random-enemies).
Random enemy *health* is built in and needs nothing extra (`random_enemy_health`, section 4).

---

## 2. Install the APWorld

1. Double-click `re4uhd.apworld`.
2. Archipelago opens and says the world was installed. That's it.

(If double-clicking doesn't work, copy the file into the `custom_worlds` folder of your Archipelago install,
usually `C:\ProgramData\Archipelago\custom_worlds`.)

---

## 3. Install the game mod

**Easy way (recommended):**

1. Open the **Archipelago Launcher** → **Resident Evil 4 UHD Client**.
2. Type `/setup` and press Enter.

The client finds your Steam copy of Resident Evil 4 and installs the game mod into its **Bin32** folder. Your old
`dinput8.dll`, if there was one, is kept as `dinput8.dll.pre-archipelago`. If the game isn't found automatically, give
the folder yourself (the one that contains `Bin32`):

```
/setup "C:\Program Files (x86)\Steam\steamapps\common\Resident Evil 4"
```

**By hand:** in Steam, right-click **Resident Evil 4** → **Manage** → **Browse local files**, open **Bin32**, and
extract everything from `RE4-UHD-Archipelago.zip` there (say **yes** to replace files).

> The mod is built on [re4_tweaks](https://github.com/nipkownix/re4_tweaks), so you also get all of its fixes.
> If you already used re4_tweaks or the HD Project, this replaces their `dinput8.dll`. Everything else keeps working.

**To uninstall:** delete `dinput8.dll` from Bin32 and rename `dinput8.dll.pre-archipelago` back to `dinput8.dll`
(or verify the game files in Steam).

---

## 3b. Optional: re_duke's randomizer (random enemies)

[re_duke's RE4 PC Randomizer](https://www.moddb.com/mods/re4randomizer) randomizes enemies, bosses, enemy health,
the Merchant and more. It's a separate project (not included here); this mod sets it up to work with Archipelago.

1. Install it the normal way: extract its zip **into the Resident Evil 4 folder** (not Bin32), so you have
   `Resident Evil 4\RE4_PC_Randomizer\RE4RND_v2.exe`. Follow its own setup guide for anything else.
2. In your YAML, set `re_duke_randomizer: true` (and pick the `re_duke_…` options you like, see section 4).
   For random enemies only, also set `re_duke_merchant: false` (and `re_duke_enemy_health: false` if you use the
   built-in `random_enemy_health` instead).
3. After you generate the multiworld, connect the client to the room, then type `/setup`.
   - The client installs our game mod (step 3) and writes an **"Archipelago"** profile for the randomizer, made from
     your own copy's presets with your YAML choices.
   - It then opens the randomizer. Check that the settings are loaded, click **Generate Seed**, and wait for
     *"Seed generated correctly"*.
4. Start the game and play as in step 6. Each time you connect, the client checks that the last randomizer seed
   is safe for Archipelago and matches your slot. `/rando` writes the profile and opens the randomizer again.

**What's different with it on:**

- **Doors, item and key-item randomization are always off** in the Archipelago profile. Archipelago places the
  items, and its logic needs the normal room layout. (If you turn them back on in the randomizer, the client will
  warn you.)
- With `re_duke_enemies` on, boss checks are turned off: random bosses can show up in other rooms.
- With `re_duke_merchant` on, Merchant checks and the blue medallion reward are turned off, because the randomizer
  changes the Merchant. With it off, they stay on.
- The same slot always gets the same enemies (the client sets the randomizer's seed number from your slot).
- The randomizer replaces `bio4.exe` with its own patched version and keeps its own settings in `dinput8.ini`; our
  `dinput8.dll` works with both. If you reinstall the randomizer later, run `/setup` again, since its zip contains
  an older `dinput8.dll`.

---

## 4. Set up your YAML

The YAML file holds your name and your choices for the randomizer.

1. Open the `.yaml` file with Notepad.
2. Change `name: Leon` to the name you want in the multiworld (no spaces is easiest).
3. Pick your options:

| Option | What it does | Default |
|---|---|---|
| `shuffle_key_items` | Key items (Insignia Key, False Eye, Card Keys…) can be anywhere in the multiworld. `false` keeps them in their normal spots. | `true` |
| `consumable_checks` | Every ammo box, herb, grenade and spray placed in the world is a check (~450 extra checks). Enemy drops don't count. | `true` |
| `consumable_progression` | Let those ammo/herb checks hold key items too. Off: they can still hold weapons and other useful things, never something you need to finish (the number of ammo/herb spots per room comes from guides and may be one too high). | `false` |
| `pesetas_checks` | How many placed pesetas pickups (cabinets, crates, bird nests…) per stage are checks, 0–50. You keep the money. Enemy and boss drops don't count. They only hold minor items. | `25` |
| `enemy_drop_checks` | Enemies can drop important items: 0–30 checks per area (village, castle, island) for things enemies drop. Each drop you pick up sends the area's next drop check and you keep the drop. These can hold key items. `0` turns it off. | `0` |
| `merchant_checks` | The first time you buy each Merchant item, you send a check. They only hold minor items (the Merchant only sells the next attache case, and may not sell a gun you already have). | `true` |
| `merchant_purchases` | `check_only`: that first purchase only sends the check; the item is taken back when you leave the shop and comes from the multiworld instead. `keep_item`: you also keep it. Later purchases are always normal. | `check_only` |
| `bonus_treasure_checks` | 0–15 extra checks per stage (village, castle, island) for treasures nothing else counts, like random enemy drops. They only hold minor items. | `5` |
| `boss_checks` | Beating Del Lago, the El Gigantes, Mendez, Verdugo, Salazar, U-3 and Krauser are checks. They only hold minor items (Verdugo can be escaped, and boss detection hasn't been confirmed in a full playthrough yet). | `true` |
| `shooting_gallery_checks` | Each of the 24 bottle caps from the shooting gallery is a check. | `true` |
| `starting_weapon` | An extra weapon at the start, with 2 boxes of its ammo: `vanilla` (none), `random_handgun` (Red9, Blacktail or Punisher) or `random_weapon` (any). | `vanilla` |
| `starting_supplies` | 0–10 random supplies (ammo, herbs, grenades, sprays) at the start. | `0` |
| `starting_pesetas` | 0–100000 pesetas at the start. | `0` |
| `random_enemy_health` | Built in: every enemy (bosses too) spawns with random health. `off`, `mild` (75–150%), `tough` (100–200%), `wild` (50–250%) or `chaos` (25–400%). | `off` |
| `re_duke_randomizer` | Use re_duke's randomizer for random enemies (see [3b](#3b-optional-re_dukes-randomizer-random-enemies)). Turns off boss checks, and Merchant checks if `re_duke_merchant` is on. | `false` |
| `re_duke_preset` | The randomizer preset to start from: `default`, `normal` or `hard`. | `default` |
| `re_duke_enemies` | Random enemies. | `true` |
| `re_duke_enemy_health` | Random enemy health. | `true` |
| `re_duke_merchant` | Random Merchant stock, prices and weapon upgrades. | `true` |
| `re_duke_starting_loadout` | Let the randomizer also roll a random starting loadout. | `false` |
| `death_link` | When you die, everyone with DeathLink dies too (and vice versa). Experimental. | `false` |

**Goal:** defeat Saddler.

Want a shorter game? Set `consumable_checks: false` and `pesetas_checks: false` — that leaves about 250 checks.

Archipelago's own `start_inventory` option also works, for example:

```yaml
  start_inventory:
    Shotgun: 1
    Shotgun Shells: 2
```

---

## 5. Generate and host the game

Whoever runs the multiworld does this once for the whole group.

1. Put everyone's YAML files into Archipelago's **Players** folder (`C:\ProgramData\Archipelago\Players`).
2. Open the **Archipelago Launcher** and click **Generate**.
3. A `.zip` file appears in the **output** folder.
4. Host it:
   - **Online (easiest):** go to [archipelago.gg/uploads](https://archipelago.gg/uploads), upload the `.zip`,
     then click **Create New Room**. Share the room link.
   - **On your own PC:** in the Launcher, click **Host** and pick the `.zip`.

> RE4 isn't one of Archipelago's built-in games, so seeds must be **generated on a PC with the APWorld
> installed** (step 2). Hosting the result on archipelago.gg works fine.

---

## 6. Play

1. Open the **Archipelago Launcher** → **Resident Evil 4 UHD Client**.
2. In the client, type the room address in the top bar (for example `archipelago.gg:38281`) and press **Connect**.
   Enter your YAML name when asked.
3. Start **Resident Evil 4** and choose **New Game** on **Normal** or **Professional** (Easy isn't supported: it cuts
   rooms that hold checks). Use a fresh New Game for every new multiworld.
4. Before you move, look at the top-left corner of the game: wait for *"Save linked to this Archipelago seed"*. If it
   says *"waiting for…"*, the client isn't connected to the room yet, and nothing you pick up counts until it is.

That's it — play the game. The top-left corner also shows how many checks are left in the area you're in
("2 items, 3 ammo/herbs"), so you know when a room is done.

> If Windows Firewall asks about the game or the client, allow it. They talk to each other on your own PC
> (`127.0.0.1`, port `46400`), nothing is opened to the internet.

---

## 7. What happens in-game

- **Picking up a shuffled item:** you see the normal pickup screen, then the item disappears from your case as soon
  as the screen closes (the Shotgun in the village house, for example), and a message says what you found and for
  whom. The real item (yours or someone else's) is sent through Archipelago. Merchant purchases with
  `merchant_purchases: check_only` disappear the same way when you leave the shop.
- **Receiving items:** they appear in your case with a message like *"Received Insignia Key from Alex"*.
  Items are only delivered during normal gameplay, not during menus, cutscenes or while shopping.
  If your attache case is full, the normal "organize" screen opens.
- **Ammo and herbs:** each room has as many checks as the game places ammo/herbs/grenades there. Whatever
  placed consumable you pick up in that room uses the next one. When a room's checks are used up, everything you
  find there is yours to keep. **Enemy drops never count** (the mod tells them apart from placed items with the
  game's own "already taken" flags), so they're always yours.
- **Pesetas:** each placed pesetas pickup (not enemy drops) sends the next pesetas check for that area. You keep
  the money.
- **Enemy drops:** always yours. With `enemy_drop_checks`, each drop you pick up also sends that area's next drop
  check (which can be a key item, for you or someone else).
- **Merchant:** the first purchase of each item sends a check. With `merchant_purchases: check_only` (the default)
  the item is taken back when you leave the shop: you've paid for the check, and the item itself is somewhere in
  the multiworld. Buying it again later works normally. Attache cases and the tactical vest always take effect.
- **Treasures:** every treasure you pick up counts. If the data has it in another room, it still counts (and the
  log notes it so the data can be fixed). Extra treasures, like random enemy drops, use the stage's bonus treasure
  checks until they run out.
- **Dying / continuing / loading:** safe. Your save remembers what it has already received and collected.
- **Ashley's solo section (3-4):** her pickups are normal — nothing is randomized there.

### Stuck behind a door?

RE4 is linear: if a key item you need hasn't arrived yet, you have to wait for someone to find it. Use the
client's `!hint` command to see where your items are.

### Missed something before a point of no return?

The village, castle and island lock behind you. When you leave the village or the castle with item checks still
there, a message tells you how many (`/missing` in the client lists them). If something you need is stuck in an
area you can't reach anymore:

1. Press **F1** in game to open the re4_tweaks menu.
2. Go to the **Trainer** tab → **Area Jump**, and jump back to the area.

If the mod missed a check you did collect, `/check <location>` in the client sends it by hand. You can also ask the
host to use `!release` / `!collect`.

### Things that are handled for you

- **Merchant:** picking things up next to the Merchant counts normally; only real purchases count as Merchant
  checks. Attache cases and the tactical vest always take effect. If you equip a check-only purchase right away,
  it's taken when you switch weapons. Don't tune up a gun you just bought for its check: it's taken back, upgrades
  and all.
- **Holy Beast pieces (5-3/5-4):** always in their normal spots, so Krauser's arena can always be opened.
- **A key item you already have:** picking up the normal copy still sends its check. (If the area counter still
  shows it afterwards, `/check` it.)
- **The re4_tweaks trainer:** don't spawn items with it; the mod can't tell them from pickups.
- **Dying / continuing:** checks and items roll back with your save and are counted again when you redo them.
- **Two saves / two multiworlds:** each save remembers its seed; a save from another seed can't send checks to your
  current room. Start from a normal **New Game** (not New Game+ / clear data).

---

## 8. Client commands

Type these in the Resident Evil 4 UHD Client:

| Command | What it does |
|---|---|
| `/setup` | Installs the game mod and, if your YAML uses it, sets up re_duke's randomizer. `/setup "<game folder>"` if the game isn't found |
| `/rando` | Writes the Archipelago profile for re_duke's randomizer and opens it |
| `/game` | Shows whether the game mod is connected |
| `/bindsave` | Links the save you have loaded to this multiworld (see below) |
| `/check <location>` | Sends one of your own locations by hand, if the mod missed a check (part of the name is enough, e.g. `/check Old House Road: Spinel #1`). `/missing` lists what's left |
| `!hint <item>` | Asks where an item is |
| `/received` | Lists the items you've received |

---

## 9. Troubleshooting

**"The game mod is out of date"** (in game or in the client)
You updated the APWorld but not the game mod. Close the game, type `/setup` in the client, start the game again.

**"Couldn't start the randomizer: [WinError 740] The requested operation requires elevation"**
re_duke's randomizer asks for administrator rights. The settings are already written: open the randomizer's
`.exe` in the game folder yourself, accept the admin prompt, and click Generate Seed. (Since 0.5.7 the client
shows the admin prompt itself.)

**"Easy isn't supported"**
Start a New Game on Normal or Professional.

**"Archipelago: waiting for the RE4 UHD Client"**
The client isn't running or isn't connected. Open the client, connect to the room, and check that your firewall
didn't block it.

**"This save isn't linked to Archipelago"**
You loaded an old save. Start a **New Game**, or, if this really is the right save, type `/bindsave` in the client.

**"This save belongs to a different Archipelago seed"**
You loaded a save from another multiworld. Load the right save. `/bindsave` relinks it to the current one
(it will receive all items again); it only works while the client is connected to your room.

**An item stayed in my inventory / a check wasn't sent**
Some room data comes from community guides and may be off. Please report it (below) — the mod writes everything
it couldn't match to a log file.

**The game crashes on start after installing**
Make sure you're on the Steam UHD version and that you extracted into **Bin32**. Restore your backup to undo.

**With re_duke's randomizer: crashes or odd behaviour**
First check the randomizer's own setup guide and FAQ (most crashes come from its enemy settings, overlays like
Discord, or missing `X3DAudio1_7.dll`). To tell whose problem it is, try once without our mod: rename `dinput8.dll` to
`dinput8.dll.off` and put `dinput8.dll.pre-archipelago` back as `dinput8.dll`.

**I picked up a treasure or key item and no check was sent**
Update to v0.5.1 or later (earlier versions missed most treasures picked up without a visible pickup screen).
For checks already missed, type `/check <location name>` in the client.

**I picked up ammo/pesetas and no check was sent**
Enemy drops never count, only items placed in the world (before v0.5, drops used up a room's checks, so placed
items picked up afterwards sent nothing). A room's checks also run out once its placed items are collected.
`archipelago.log` shows each pickup as `placed` or `drop`; please send it if a placed item didn't count.

**The client or game warns that re_duke's randomizer moved items**
Its default settings randomize items and doors, which Archipelago can't follow. Type `/rando` in the client, keep
the "Archipelago" profile it loads, and click Generate Seed again (or click **Restore Game** in the randomizer to
remove it).

**"re_duke randomizer: … turned on, which breaks Archipelago's item logic"**
The last seed was generated with doors or items randomized. Type `/rando`, keep the Archipelago profile, and click
Generate Seed again.

---

## Reporting problems

Open an **Issue** on this repository and attach:

- `Bin32\re4_tweaks\archipelago.log` (the most useful file — it lists unmatched pickups and room numbers)
- What you were doing, and the chapter/area

The log also records which objects you destroy in chapter 1's farm and graveyard. If you shoot the blue
medallions and send the log, that lets individual medallions become checks in a future version.

---

## Credits

- Archipelago integration: **Pegz**
- [re4_tweaks](https://github.com/nipkownix/re4_tweaks) by nipkownix, emoose and contributors (the base of the game mod)
- [Archipelago](https://archipelago.gg) and its community
- Item location research: [Evil Resource](https://www.evilresource.com), StrategyWiki, the RE4 modding community
