# Playing Resident Evil 4 UHD in Archipelago — Tutorial

This guide takes you from nothing to playing RE4 in an Archipelago multiworld.
It takes about 15 minutes the first time.

> **Status: early release (v0.1).** Everything is tested against a simulated game, but this is the first public
> build, so expect rough edges. Please report problems (see [Reporting problems](#reporting-problems)).

---

## 1. What you need

| Thing | Where to get it |
|---|---|
| Resident Evil 4 on Steam (the **Ultimate HD Edition**, `bio4.exe`) | Steam |
| Archipelago **0.6.7 or newer** | [Archipelago releases](https://github.com/ArchipelagoMW/Archipelago/releases) — install it like any program |
| This mod's release files | The **Releases** page of this repository (right side of the repo page) |

From the latest release, download these three files:

- `re4uhd.apworld` — teaches Archipelago about RE4
- `RE4-UHD-Archipelago.zip` — the game mod
- `Resident Evil 4 UHD.yaml` — your settings file

---

## 2. Install the APWorld

1. Double-click `re4uhd.apworld`.
2. Archipelago opens and says the world was installed. That's it.

(If double-clicking doesn't work, copy the file into the `custom_worlds` folder of your Archipelago install,
usually `C:\ProgramData\Archipelago\custom_worlds`.)

---

## 3. Install the game mod

1. In Steam, right-click **Resident Evil 4** → **Manage** → **Browse local files**.
2. Open the **Bin32** folder.
3. *(Recommended)* Make a backup copy of the Bin32 folder somewhere safe.
4. Extract everything from `RE4-UHD-Archipelago.zip` into **Bin32**. Say **yes** if Windows asks to replace files.

You should now see `dinput8.dll`, `dinput8.ini` and a `re4_tweaks` folder inside Bin32.

> The mod is built on [re4_tweaks](https://github.com/nipkownix/re4_tweaks), so you also get all of its fixes.
> If you already used re4_tweaks or the HD Project, this replaces their `dinput8.dll`. Everything else keeps working.

**To uninstall:** delete `dinput8.dll` from Bin32 (or restore your backup).

---

## 4. Set up your YAML

The YAML file holds your name and your choices for the randomizer.

1. Open `Resident Evil 4 UHD.yaml` with Notepad.
2. Change `name: Leon` to the name you want in the multiworld (no spaces is easiest).
3. Pick your options:

| Option | What it does | Default |
|---|---|---|
| `shuffle_key_items` | Key items (Insignia Key, False Eye, Card Keys…) can be anywhere in the multiworld. `false` keeps them in their normal spots. | `true` |
| `consumable_checks` | Every ammo box, herb, grenade and spray placed in the world is a check (~450 extra checks). | `true` |
| `merchant_checks` | The first time you buy each Merchant item, you also send a check. You keep what you bought. | `true` |
| `boss_checks` | Beating Del Lago, the El Gigantes, Mendez, Verdugo, Salazar, U-3 and Krauser are checks. | `true` |
| `shooting_gallery_checks` | Each of the 24 bottle caps from the shooting gallery is a check. | `true` |
| `death_link` | When you die, everyone with DeathLink dies too (and vice versa). Experimental. | `false` |

**Goal:** defeat Saddler.

Want a shorter game? Set `consumable_checks: false` — that leaves about 250 checks.

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
