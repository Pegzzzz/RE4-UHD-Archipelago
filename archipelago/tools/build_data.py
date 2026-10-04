"""Builds apworld/re4uhd/data/*.json from the research datasets.

Run from the repo root:  python tools/build_data.py
The generated JSON is the single source of truth for both the APWorld and the
game mod (the client forwards the location table to the DLL in slot_data).
"""
import json
import re
from collections import Counter, defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TOOLS = ROOT / "tools"
OUT = ROOT / "apworld" / "re4uhd" / "data"

ITEM_IDS = json.load(open(TOOLS / "item_ids.json"))
RESEARCH = json.load(open(TOOLS / "items_research.json"))

CHAPTERS = ["1-1", "1-2", "1-3", "2-1", "2-2", "2-3", "3-1", "3-2", "3-3", "3-4",
            "4-1", "4-2", "4-3", "4-4", "5-1", "5-2", "5-3", "5-4", "Final"]

# --- game item ids -------------------------------------------------------------
_by_name = {}
for it in ITEM_IDS:
    _by_name.setdefault(it["name"].lower(), it["id"])

NAME_OVERRIDES = {
    "hourglass w/ gold decor": 150,
    "mirror w/ pearls & rubies": 145,
    "punisher": 33,
    "handgun": 35,
    "piece of the holy beast, panther": 133,
    "piece of the holy beast, serpent": 134,
    "piece of the holy beast, eagle": 135,
    "moonstone (left half)": 105,
    "storage room card key": 131,
    "waste disposal card key": 146,
    "freezer card key": 132,
    "emergency lock card key": 116,
    "treasure map (village)": 169,
    "tactical vest": 254,
}


def game_id(name: str) -> int:
    key = name.lower()
    if key in NAME_OVERRIDES:
        return NAME_OVERRIDES[key]
    if key in _by_name:
        return _by_name[key]
    raise KeyError(f"no game item id for {name!r}")


# Items whose pickup is done by Ashley or given in a cutscene the game relies on.
# These stay vanilla: they are neither locations nor items in the pool.
VANILLA_KEEP = {"Stone Tablet", "Salazar Family Insignia", "Serpent Ornament",
                "Rocket Launcher (Special)", "Jet-ski Key"}

# Requirements on items that stay vanilla are always satisfied.
def clean_requires(reqs):
    out = []
    for r in reqs:
        opts = [o for o in r.split("|")]
        if any(o in VANILLA_KEEP for o in opts):
            continue
        out.append("|".join(opts))
    return out


def clean_area(area: str) -> str:
    m = re.search(r"\(([^)]*)\)", area)
    if m and m.group(1)[:1].isupper() and not m.group(1).lower().startswith(("night", "day", "inside", "outside")):
        name = m.group(1)
    else:
        name = re.sub(r"\s*\(.*?\)", "", area).split(" - ")[0]
    name = name.replace("[Ashley]", "").strip(" ?")
    return name


def room_int(room: str) -> int:
    return int(room[1:], 16)


locations = []
item_counts = Counter()        # vanilla item name -> count placed in the pool

# ---- world pickups ----------------------------------------------------------------
pickups = []
for e in RESEARCH:
    if e["confidence"] == "low":
        continue  # unverified placement: could be impossible to check
    if e["chapter"] == "3-4":
        continue  # Ashley segment: her pickups are left vanilla
    if e["item"] in VANILLA_KEEP:
        continue
    pickups.append(e)

name_seen = Counter()
for e in pickups:
    if e["item"].startswith("Green/Red/Blue Eye"):
        ids = [185, 186, 187]
        item_names = ["Green Eye", "Red Eye", "Blue Eye"]
        copies = 3
    else:
        ids = [game_id(e["item"])]
        item_names = [e["item"]]
        copies = 1
    for c in range(copies):
        base = f"{e['chapter']} {clean_area(e['area'])}: " + ("Nest Eye" if copies > 1 else e["item"])
        name_seen[base] += 1
        loc = {
            "name": base,
            "kind": "pickup",
            "chapter": e["chapter"],
            "room": room_int(e["room"]),
            "room_confidence": e["room_confidence"],
            "game_items": ids,
            "vanilla": item_names[c % len(item_names)],
            "category": e["category"],
            "requires": clean_requires(e.get("requires") or []),
            "detail": e.get("location_detail", ""),
            "cut": e["obtained"] in ("cutscene", "puzzle"),
        }
        locations.append(loc)
        item_counts[loc["vanilla"]] += 1

# make names unique
dupes = Counter(l["name"] for l in locations)
idx = Counter()
for l in locations:
    if dupes[l["name"]] > 1:
        idx[l["name"]] += 1
        l["name"] = f"{l['name']} #{idx[l['name']]}"

# ---- bosses -----------------------------------------------------------------------
BOSSES = [
    # name, chapter, room, em id, requires
    ("Del Lago", "1-3", 0x10B, 0x2F, []),
    ("El Gigante (Quarry)", "2-1", 0x119, 0x2B, []),
    ("El Gigante (Gorge)", "2-3", 0x11E, 0x2B, []),
    ("Bitores Mendez", "2-3", 0x11F, 0x35, ["Camp Key|Old Key"]),
    ("Verdugo", "4-1", 0x221, 0x2C, []),
    ("El Gigante (Furnace)", "4-2", 0x224, 0x2B, ["Dynamite"]),
    ("Ramon Salazar", "4-4", 0x228, 0x38, []),
    ("U-3", "5-3", 0x31B, 0x32, []),
    ("Jack Krauser", "5-3", 0x31C, 0x39, []),
]
for name, ch, room, em, req in BOSSES:
    locations.append({"name": f"Defeat {name}", "kind": "boss", "chapter": ch, "room": room,
                      "em_id": em, "requires": req})

# ---- merchant purchases ------------------------------------------------------------
MERCHANT = [
    ("Handgun", "1-2"), ("Shotgun", "1-2"), ("Rifle", "1-2"), ("Scope (Rifle)", "1-2"),
    ("TMP", "1-2"), ("Rocket Launcher", "1-2"), ("Attache Case M", "1-2"),
    ("First Aid Spray", "1-2"), ("Treasure Map (Village)", "1-2"), ("Stock (TMP)", "1-3"),
    ("Red9", "2-2"), ("Stock (Red9)", "2-2"), ("Punisher", "2-3"), ("Blacktail", "3-1"),
    ("Broken Butterfly", "3-1"), ("Riot Gun", "3-1"), ("Rifle (semi-auto)", "3-1"),
    ("Scope (semi-auto rifle)", "3-1"), ("Mine Thrower", "3-1"), ("Attache Case L", "3-1"),
    ("Treasure Map (Castle)", "3-1"), ("Attache Case XL", "3-3"), ("Striker", "4-1"),
    ("Killer7", "5-1"), ("Treasure Map (Island)", "5-1"), ("Tactical Vest", "5-1"),
]
for name, ch in MERCHANT:
    locations.append({"name": f"Merchant: Buy {name}", "kind": "merchant", "chapter": ch,
                      "game_items": [game_id(name)], "requires": []})

# ---- blue medallions ----------------------------------------------------------------
locations.append({"name": "Blue Medallions: Merchant Reward", "kind": "medallion_reward", "chapter": "1-3",
                  "game_items": [33], "requires": []})
# Individual medallions (15) are not detectable yet: the mod logs object kills in the farm and
# graveyard so their enemy id can be identified, then they will be added here.

# ---- shooting gallery bottle caps -------------------------------------------------
CAPS = {"A": [220, 221, 222, 223, 224, 240], "B": [225, 226, 227, 228, 229, 241],
        "C": [230, 231, 232, 233, 234, 242], "D": [235, 236, 237, 238, 239, 243]}
GAME_CH = {"A": "3-1", "B": "4-1", "C": "4-2", "D": "5-1"}
names = {it["id"]: it["name"] for it in ITEM_IDS}
for g, caps in CAPS.items():
    for cid in caps:
        locations.append({"name": f"Shooting Gallery {g}: {names[cid]} Cap", "kind": "bottle_cap",
                          "chapter": GAME_CH[g], "game_items": [cid], "requires": []})

# ---- ids ---------------------------------------------------------------------------
for i, l in enumerate(locations):
    l["offset"] = i
assert len({l["name"] for l in locations}) == len(locations)
assert len(locations) <= 256, "the game save stores collected locations in a 256-bit set"

# ---- AP items ---------------------------------------------------------------------
KEY_ITEMS = ["Emblem (Left half)", "Emblem (Right half)", "Insignia Key", "Round Insignia",
             "Camp Key", "Old Key", "False Eye", "Platinum Sword", "Golden Sword",
             "Castle Gate Key", "Prison Key", "Gallery Key", "Goat Ornament",
             "Moonstone (Left half)", "Moonstone (Right half)", "Lion Ornament",
             "King's Grail", "Queen's Grail", "Dynamite", "Key to the Mine",
             "Stone of Sacrifice", "Freezer Card Key", "Waste Disposal Card Key",
             "Storage Room Card Key", "Piece of the Holy Beast, Panther",
             "Piece of the Holy Beast, Eagle", "Piece of the Holy Beast, Serpent",
             "Emergency Lock Card Key"]

USEFUL_WEAPONS = ["Shotgun", "Rocket Launcher", "Broken Butterfly", "Infrared Scope", "Red9",
                  "Blacktail", "Punisher", "Riot Gun", "Striker", "Rifle", "Rifle (semi-auto)",
                  "TMP", "Killer7", "Mine Thrower", "Scope (Rifle)", "Scope (semi-auto rifle)",
                  "Stock (TMP)", "Stock (Red9)", "Tactical Vest", "Yellow Herb"]

FILLER = ["Handgun Ammo", "Shotgun Shells", "Rifle Ammo", "TMP Ammo", "Magnum Ammo",
          "Mine-Darts", "Green Herb", "Red Herb", "First Aid Spray", "Hand Grenade",
          "Incendiary Grenade", "Flash Grenade", "Chicken Egg", "Brown Chicken Egg"]

items = {}
def add_item(name, classification, gid=None, kind="game"):
    if name in items:
        return
    items[name] = {"name": name, "classification": classification, "kind": kind}
    if gid is not None:
        items[name]["game_item"] = gid

for k in KEY_ITEMS:
    add_item(k, "progression", game_id(k))
for w in USEFUL_WEAPONS:
    add_item(w, "useful", game_id(w))
for f in FILLER:
    add_item(f, "filler", game_id(f))
for v in item_counts:
    if v not in items:
        cls = "filler"
        add_item(v, cls, game_id(v))
for amt in (1000, 5000, 10000, 20000):
    add_item(f"{amt} Pesetas", "filler", None, kind="pesetas")
    items[f"{amt} Pesetas"]["amount"] = amt
add_item("Progressive Attache Case", "useful", None, kind="attache_case")
add_item("Victory", "progression", None, kind="event")

for i, name in enumerate(items):
    items[name]["offset"] = i

pool = dict(item_counts)

OUT.mkdir(parents=True, exist_ok=True)
json.dump({"locations": locations}, open(OUT / "locations.json", "w"), indent=1)
json.dump({"items": list(items.values()), "vanilla_pool": pool, "chapters": CHAPTERS},
          open(OUT / "items.json", "w"), indent=1)

kinds = Counter(l["kind"] for l in locations)
print("locations:", len(locations), dict(kinds))
print("vanilla pool:", sum(pool.values()), "distinct", len(pool))
print("items defined:", len(items))
