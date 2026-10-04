# Playing Resident Evil 4 UHD in Archipelago — Tutorial

This guide takes you from nothing to playing RE4 in an Archipelago multiworld.
It takes about 15 minutes the first time.

> **Status: early release (v0.3).** Everything is tested against a simulated game, but this is the first public
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
re_duke's Patreon), for random enemies, enemy health and Merchant. See [section 3b](#3b-optional-re_dukes-randomizer-random-enemies).

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
- Merchant checks, boss checks and the blue medallion reward are turned off, because the randomizer changes those.
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
| `consumable_checks` | Every ammo box, herb, grenade and spray placed in the world is a check (~450 extra checks). | `true` |
| `merchant_checks` | The first time you buy each Merchant item, you also send a check. You keep what you bought. | `true` |
| `boss_checks` | Beating Del Lago, the El Gigantes, Mendez, Verdugo, Salazar, U-3 and Krauser are checks. | `true` |
| `shooting_gallery_checks` | Each of the 24 bottle caps from the shooting gallery is a check. | `true` |
| `starting_weapon` | An extra weapon at the start, with 2 boxes of its ammo: `vanilla` (none), `random_handgun` (Red9, Blacktail or Punisher) or `random_weapon` (any). | `vanilla` |
| `starting_supplies` | 0–10 random supplies (ammo, herbs, grenades, sprays) at the start. | `0` |
| `starting_pesetas` | 0–100000 pesetas at the start. | `0` |
| `re_duke_randomizer` | Use re_duke's randomizer for random enemies (see [3b](#3b-optional-re_dukes-randomizer-random-enemies)). Turns off Merchant/boss checks. | `false` |
| `re_duke_preset` | The randomizer preset to start from: `default`, `normal` or `hard`. | `default` |
| `re_duke_enemies` | Random enemies. | `true` |
| `re_duke_enemy_health` | Random enemy health. | `true` |
| `re_duke_merchant` | Random Merchant stock, prices and weapon upgrades. | `true` |
| `re_duke_starting_loadout` | Let the randomizer also roll a random starting loadout. | `false` |
| `death_link` | When you die, everyone with DeathLink dies too (and vice versa). Experimental. | `false` |

**Goal:** defeat Saddler.

Want a shorter game? Set `consumable_checks: false` — that leaves about 250 checks.

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

1. Start **Resident Evil 4** and choose **New Game**. Use a fresh New Game for every new multiworld.
2. Open the **Archipelago Launcher** → **Resident Evil 4 UHD Client**.
3. In the client, type the room address in the top bar (for example `archipelago.gg:38281`) and press **Connect**.
   Enter your YAML name when asked.
4. Look at the top-left corner of the game: you should see *"Archipelago: connected as …"*.

That's it — play the game.

> If Windows Firewall asks about the game or the client, allow it. They talk to each other on your own PC
> (`127.0.0.1`, port `46400`), nothing is opened to the internet.

---

## 7. What happens in-game

- **Picking up a shuffled item:** you see the normal pickup screen, then the item disappears and a message says
  what you found and for whom. The real item (yours or someone else's) is sent through Archipelago.
- **Receiving items:** they appear in your case with a message like *"Received Insignia Key from Alex"*.
  Items are only delivered during normal gameplay, not during menus, cutscenes or while shopping.
  If your attache case is full, the normal "organize" screen opens.
- **Ammo and herbs:** each room has as many checks as the game places ammo/herbs/grenades there. Whatever
  consumable you pick up in that room uses the next one. When a room's checks are used up, everything you find
  there is yours to keep.
- **Merchant:** the first purchase of each item sends a check, and you still get the item.
- **Dying / continuing / loading:** safe. Your save remembers what it has already received and collected.
- **Ashley's solo section (3-4):** her pickups are normal — nothing is randomized there.

### Stuck behind a door?

RE4 is linear: if a key item you need hasn't arrived yet, you have to wait for someone to find it. Use the
client's `!hint` command to see where your items are.

### Missed something before a point of no return?

The village, castle and island lock behind you. If something you need is stuck in an area you can't reach anymore:

1. Press **F1** in game to open the re4_tweaks menu.
2. Go to the **Trainer** tab → **Area Jump**, and jump back to the area.

You can also ask the host to use `!release` / `!collect`.

---

## 8. Client commands

Type these in the Resident Evil 4 UHD Client:

| Command | What it does |
|---|---|
| `/setup` | Installs the game mod and, if your YAML uses it, sets up re_duke's randomizer. `/setup "<game folder>"` if the game isn't found |
| `/rando` | Writes the Archipelago profile for re_duke's randomizer and opens it |
| `/game` | Shows whether the game mod is connected |
| `/bindsave` | Links the save you have loaded to this multiworld (see below) |
| `!hint <item>` | Asks where an item is |
| `/received` | Lists the items you've received |

---

## 9. Troubleshooting

**"Archipelago: waiting for the RE4 UHD Client"**
The client isn't running or isn't connected. Open the client, connect to the room, and check that your firewall
didn't block it.

**"This save isn't linked to Archipelago"**
You loaded an old save. Start a **New Game**, or, if this really is the right save, type `/bindsave` in the client.

**"This save belongs to a different Archipelago seed"**
You loaded a save from another multiworld. Load the right save. `/bindsave` relinks it to the current one
(it will receive all items again).

**An item stayed in my inventory / a check wasn't sent**
Some room data comes from community guides and may be off. Please report it (below) — the mod writes everything
it couldn't match to a log file.

**The game crashes on start after installing**
Make sure you're on the Steam UHD version and that you extracted into **Bin32**. Restore your backup to undo.

**With re_duke's randomizer: crashes or odd behaviour**
First check the randomizer's own setup guide and FAQ (most crashes come from its enemy settings, overlays like
Discord, or missing `X3DAudio1_7.dll`). To tell whose problem it is, try once without our mod: rename `dinput8.dll` to
`dinput8.dll.off` and put `dinput8.dll.pre-archipelago` back as `dinput8.dll`.

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
