import sys, os, json
HDIR = __import__("os").path.dirname(__import__("os").path.abspath(__file__))
AP = os.environ.get("AP", "/root/AP067")
sys.path.insert(0, AP)
os.environ["SKIP_REQUIREMENTS_UPDATE"]="1"
os.chdir(AP)
from worlds.re4uhd import RE4World
from worlds.re4uhd.data_loader import LOCATIONS, LOCATION_NAME_TO_ID
from BaseClasses import MultiWorld
from worlds.re4uhd.options import RE4Options
import types
# a real (one-player) generation, so slot_data comes from exactly the code players run
from BaseClasses import CollectionState
from Generate import get_seed_name
from worlds import AutoWorld
from worlds.AutoWorld import call_all
from argparse import Namespace
from test.general import gen_steps
mw = MultiWorld(1)
mw.game[1] = RE4World.game
mw.player_name = {1: "Leon"}
mw.set_seed(1)
mw.seed_name = "harness"
args = Namespace()
for name, option in RE4World.options_dataclass.type_hints.items():
    setattr(args, name, {1: option.from_any(option.default)})
args.death_link = {1: RE4World.options_dataclass.type_hints["death_link"].from_any(1)}
mw.set_options(args)
for step in gen_steps:
    call_all(mw, step)
w = mw.worlds[1]
sd = w.fill_slot_data()
out=sys.argv[1]
out = os.path.abspath(out) if os.path.isabs(out) else os.path.join(HDIR, out)
json.dump(sd, open(out,"w"))
names = {v:k for k,v in LOCATION_NAME_TO_ID.items()}
json.dump({str(k):v for k,v in names.items()}, open(out.replace(".json","_names.json"),"w"))
print(len(sd["locations"]), len(sd["items"]))

# id constants for test.cpp, looked up by name so data changes don't break the tests
want = {
    "L_WOODS_SPINEL": "1-1 Woods: Spinel", "L_SHOTGUN": "1-1 Village: Shotgun",
    "L_FARM_SPINEL1": "1-1 Farm: Spinel #1", "L_FARM_SPINEL2": "1-1 Farm: Spinel #2",
    "L_GOLDEN_SWORD": "3-1 Barracks: Golden Sword", "L_DEL_LAGO": "Defeat Del Lago",
    "L_GIGANTE_QUARRY": "Defeat El Gigante (Quarry)", "L_BUY_CASE_M": "Merchant: Buy Attache Case M",
    "L_BUY_RED9": "Merchant: Buy Red9", "L_BUY_PUNISHER": "Merchant: Buy Punisher",
    "L_MEDALLION": "Blue Medallions: Merchant Reward", "L_CAP_HANDGUN": "Shooting Gallery A: Leon w/ handgun Cap",
    "L_SADDLER_SHELLS": "Final Saddler arena: Shotgun Shells",
    "L_CASTLE_BONUS1": "Castle Bonus Treasure 1", "L_CASTLE_BONUS5": "Castle Bonus Treasure 5",
    "L_VILLAGE_PESETAS1": "Village Pesetas 1", "L_VILLAGE_PESETAS2": "Village Pesetas 2",
    "L_ISLAND_PESETAS1": "Island Pesetas 1", "L_ISLAND_PESETAS11": "Island Pesetas 11",
    "L_OLDHOUSE_SPINEL1": "1-1 Old House Road: Spinel #1", "L_OLDHOUSE_SPINEL2": "1-1 Old House Road: Spinel #2",
    "L_INSIGNIA_KEY": "1-2 Chief's House: Insignia Key", "L_BUY_STOCK_TMP": "Merchant: Buy Stock (TMP)",
    "L_EMBLEM_LEFT": "1-2 Valley: Emblem (Left half)", "L_VERDUGO": "Defeat Verdugo",
    "L_BUY_CASE_L": "Merchant: Buy Attache Case L",
}
from worlds.re4uhd.data_loader import LOCATION_BASE_ID
with open(os.path.join(os.path.dirname(os.path.abspath(out)), "ids.inc"), "w") as f:
    lines = [f"{k} = {LOCATION_NAME_TO_ID[v]}" for k, v in want.items()]
    f.write("constexpr int64_t " + ",\n\t".join(lines) + ";\n")
    from worlds.re4uhd.data_loader import ITEM_NAME_TO_ID
    items_want = {"IT_GREEN_HERB": "Green Herb", "IT_RED9": "Red9", "IT_HANDGUN_AMMO": "Handgun Ammo",
                  "IT_GRENADE": "Hand Grenade", "IT_P1000": "1000 Pesetas", "IT_SPRAY": "First Aid Spray"}
    f.write("constexpr int64_t " + ",\n\t".join(f"{k} = {ITEM_NAME_TO_ID[v]}" for k, v in items_want.items()) + ";\n")
    f.write(f"constexpr int OFF_ISLAND_PESETAS11 = {LOCATION_NAME_TO_ID[want['L_ISLAND_PESETAS11']] - LOCATION_BASE_ID};\n")
    f.write(f"constexpr int OFF_SADDLER_SHELLS = {LOCATION_NAME_TO_ID[want['L_SADDLER_SHELLS']] - LOCATION_BASE_ID};\n")
    f.write(f"constexpr int OFF_FARM_SPINEL1 = {LOCATION_NAME_TO_ID[want['L_FARM_SPINEL1']] - LOCATION_BASE_ID}, "
            f"OFF_FARM_SPINEL2 = {LOCATION_NAME_TO_ID[want['L_FARM_SPINEL2']] - LOCATION_BASE_ID};\n")
