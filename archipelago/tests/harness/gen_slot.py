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
w = RE4World.__new__(RE4World)
class O: pass
o = O()
for k in ("shuffle_key_items","consumable_checks","merchant_checks","boss_checks","shooting_gallery_checks"): setattr(o,k,1)
o.death_link=1
w.options=o
w.generate_early()
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
}
from worlds.re4uhd.data_loader import LOCATION_BASE_ID
with open(os.path.join(os.path.dirname(os.path.abspath(out)), "ids.inc"), "w") as f:
    lines = [f"{k} = {LOCATION_NAME_TO_ID[v]}" for k, v in want.items()]
    f.write("constexpr int64_t " + ",\n\t".join(lines) + ";\n")
    f.write(f"constexpr int OFF_FARM_SPINEL1 = {LOCATION_NAME_TO_ID[want['L_FARM_SPINEL1']] - LOCATION_BASE_ID}, "
            f"OFF_FARM_SPINEL2 = {LOCATION_NAME_TO_ID[want['L_FARM_SPINEL2']] - LOCATION_BASE_ID};\n")
