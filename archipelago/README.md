# Archipelago development notes

- `tools/build_data.py` turns the research datasets (`tools/*_research.json`) into
  `apworld/re4uhd/data/{locations,items}.json`. Edit the research JSON or the tables in the script, then rerun it.
- `tools/gen_test.sh` generates a seed against a local Archipelago checkout (`AP=/path/to/Archipelago`).
- `tools/build_apworld.py` packages `re4uhd.apworld` (run it from an Archipelago checkout with the world linked in).
- Run the world tests from an Archipelago checkout: `python -m pytest worlds/re4uhd/test`.

## Client <-> game protocol

Newline-delimited JSON over TCP `127.0.0.1:46400` (the game listens). See the docstring at the top of
`apworld/re4uhd/client.py`.

## Behavioral tests for the game module

`tests/harness/build.sh` compiles `dllmain/Archipelago.cpp` against a simulated game (inventory, item-get screen,
Merchant, organize screen, enemies, save rollback) and runs 26 scenarios under wine with a fake client:
`AP=/path/to/Archipelago tests/harness/build.sh`. Needs clang, mingw-w64 (i686), wine32.

## Things to verify in game

- Pickups are detected by diffing Leon's item list while the pickup screen is open. Confirm every location type
  (crates, shot-down treasures, chests, boss drops) triggers a check.
- Room ids marked medium/low confidence in `tools/items_research.json` (look for `Matched ... by stage` in the log).
- The Merchant's free Punisher (blue medallion reward) is told apart from a purchase by "no pesetas spent".
- Blue medallion em id: the log prints `[discovery]` lines in the farm (r103) and graveyard (r108).
- DeathLink kill sets Leon's HP to 0; confirm it triggers the death screen.
- The received-item index lives in `GLOBAL_WK::save_free_work[60..62]`; confirm nothing else writes there.
