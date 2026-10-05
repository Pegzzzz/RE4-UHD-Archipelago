# Resident Evil 4 UHD Archipelago Setup Guide

## Requirements

- Resident Evil 4 (Steam "Ultimate HD Edition", `bio4.exe`).
- [Archipelago](https://github.com/ArchipelagoMW/Archipelago/releases) 0.6.7 or newer.
- The release zip of this mod: `re4uhd.apworld`, `RE4-UHD-Archipelago.zip` (game files) and the YAML template.

This mod is a build of [re4_tweaks](https://github.com/nipkownix/re4_tweaks) with an Archipelago module added.
If you already use re4_tweaks or the HD Project, this build replaces their `dinput8.dll`.

## Installing

1. Double-click `re4uhd.apworld`. Archipelago installs it into its `custom_worlds` folder.
2. Open the Resident Evil 4 UHD Client from the Archipelago Launcher and type `/setup`: it finds the game and
   installs the game mod. Or, by hand: open your game folder (Steam: right-click the game > Manage > Browse local files) and go into `Bin32`.
3. Extract `RE4-UHD-Archipelago.zip` there, replacing files if asked. You should now have `Bin32\dinput8.dll`,
   `Bin32\dinput8.ini` and a `Bin32\re4_tweaks` folder.

## Making your YAML

Copy `Resident Evil 4 UHD.yaml` into Archipelago's `Players` folder, change `name:` and the options you want, and
generate as usual (or give it to whoever hosts).

## Playing

1. Open the Archipelago Launcher and click **Resident Evil 4 UHD Client**.
2. Connect the client to the room (for example `archipelago.gg:38281`) and enter your slot name.
3. Start the game and begin a **New Game** on **Normal** or **Professional** for each new seed (Easy cuts rooms with
   checks and isn't supported).
4. Wait for *"Save linked to this Archipelago seed"* in the top-left corner before you move; it also shows the
   connection state, every item sent or received, and how many checks are left in the current area.

The client and the game talk over `127.0.0.1:46400`. Allow it if your firewall asks.

### How checks work

- When Leon takes a shuffled item, it is removed again and the location is sent. The item that location really
  holds arrives a moment later, with a message.
- Items arrive only during normal gameplay (not in menus, cutscenes or the Merchant). If the attache case is full,
  the game opens the usual "organize" screen.
- Merchant checks fire on the first purchase of each item. With `merchant_purchases: check_only` (the default) the
  item is taken back when you leave the shop; it's somewhere in the multiworld. Attache cases and the vest stay.
- Your save remembers how many Archipelago items it has already received, so dying, continuing, or loading an older
  save re-delivers items correctly.

### Missed something before a point of no return?

The village, castle and island each lock behind you. If an item you need is stuck in an area you can no longer
reach, press **F1** to open the re4_tweaks menu, go to the **Trainer** tab and use **Area Jump** to go back.
If the mod missed a check you collected, type `/check <location>` in the client. You can also ask the host to
`!release`/`!collect`.

## Random enemies (re_duke's randomizer)

Install re_duke's RE4 PC Randomizer inside the game folder (moddb.com/mods/re4randomizer), set
`re_duke_randomizer: true` in your YAML, connect the client to the room and type `/setup`. The client writes an
"Archipelago" profile for the randomizer (doors and item randomization off) and opens it; click Generate Seed.

## Troubleshooting

- **"waiting for the RE4 UHD Client"**: the client isn't running or isn't connected to the game. Check the client
  log and your firewall.
- **"this save belongs to another seed"**: you loaded a save from a different multiworld. Load the right save, or
  type `/bindsave` in the client if you really want to use this one (it will receive all items again).
- **Something wasn't sent / a pickup stayed in your inventory**: the mod writes `Bin32\re4_tweaks\archipelago.log`.
  Lines starting with `Unmapped pickup` show the room and item; please report them so the location data can be fixed.
