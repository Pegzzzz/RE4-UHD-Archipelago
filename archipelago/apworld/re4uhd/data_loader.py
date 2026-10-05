import json
import pkgutil
from typing import Any, Dict, List

GAME_NAME = "Resident Evil 4 UHD"
ITEM_BASE_ID = 7_740_000
LOCATION_BASE_ID = 7_741_000


def _load(name: str) -> Dict[str, Any]:
    raw = pkgutil.get_data(__name__.rsplit(".", 1)[0], f"data/{name}")
    if raw is None:
        raise FileNotFoundError(name)
    return json.loads(raw.decode("utf-8"))


_items = _load("items.json")
_locations = _load("locations.json")

ITEMS: List[Dict[str, Any]] = _items["items"]
VANILLA_POOL: Dict[str, int] = _items["vanilla_pool"]
CHAPTERS: List[str] = _items["chapters"]
LOCATIONS: List[Dict[str, Any]] = _locations["locations"]

ITEMS_BY_NAME: Dict[str, Dict[str, Any]] = {i["name"]: i for i in ITEMS}
ITEM_NAME_TO_ID: Dict[str, int] = {i["name"]: ITEM_BASE_ID + i["offset"] for i in ITEMS if i["kind"] != "event"}
LOCATION_NAME_TO_ID: Dict[str, int] = {l["name"]: LOCATION_BASE_ID + l["offset"] for l in LOCATIONS}

# Key items the game itself requires to move from one chapter to the next.
# Requirements are cumulative: entering a chapter needs every gate listed for it
# and for all chapters before it. "A|B" means either item works.
CHAPTER_GATES: Dict[str, List[str]] = {
    "1-3": ["Emblem (Left half)", "Emblem (Right half)", "Insignia Key"],
    "2-2": ["Round Insignia"],
    "3-1": ["Camp Key|Old Key", "False Eye"],
    "3-2": ["Platinum Sword", "Golden Sword", "Castle Gate Key", "Prison Key"],
    "3-3": ["Gallery Key", "Goat Ornament", "Moonstone (Left half)", "Moonstone (Right half)"],
    "4-2": ["Lion Ornament", "Goat Ornament", "King's Grail", "Queen's Grail"],
    "4-3": ["Dynamite"],
    "4-4": ["Key to the Mine", "Stone of Sacrifice"],
    "5-2": ["Freezer Card Key", "Waste Disposal Card Key", "Storage Room Card Key"],
    "5-4": ["Piece of the Holy Beast, Panther", "Piece of the Holy Beast, Eagle",
            "Piece of the Holy Beast, Serpent"],
    "Final": ["Emergency Lock Card Key"],
}
