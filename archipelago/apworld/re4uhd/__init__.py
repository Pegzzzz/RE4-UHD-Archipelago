import zlib
from typing import Any, Callable, ClassVar, Dict, List

import settings

from BaseClasses import CollectionState, Item, ItemClassification, Location, LocationProgressType, Region, Tutorial
from worlds.AutoWorld import WebWorld, World
from worlds.LauncherComponents import Component, Type, components, launch as launch_component

from .data_loader import (CHAPTER_GATES, CHAPTERS, GAME_NAME, ITEM_BASE_ID, ITEM_NAME_TO_ID, ITEMS,
                          ITEMS_BY_NAME, LOCATION_BASE_ID, LOCATION_NAME_TO_ID, LOCATIONS)
from .options import ENEMY_HEALTH_RANGES, OPTION_GROUPS, RE4Options
from .rando_bridge import rando_settings


def launch_client(*args: str) -> None:
    from .client import launch
    launch_component(launch, name="RE4UHDClient", args=args)


components.append(Component("Resident Evil 4 UHD Client", func=launch_client,
                            component_type=Type.CLIENT, game_name=GAME_NAME,
                            supports_uri=True))


class RE4Settings(settings.Group):
    class GameFolder(settings.OptionalUserFolderPath):
        """Resident Evil 4 game folder (the one that contains Bin32). Steam installs are found automatically;
        the client's /setup command fills this in."""
        description = "Resident Evil 4 game folder"

    game_folder: GameFolder = GameFolder("")


class RE4Web(WebWorld):
    theme = "stone"
    option_groups = OPTION_GROUPS
    tutorials = [Tutorial(
        "Multiworld Setup Guide",
        "How to install the RE4 UHD Archipelago mod and connect to a multiworld.",
        "English",
        "setup_en.md",
        "setup/en",
        ["Pegz"],
    )]


class RE4Item(Item):
    game = GAME_NAME


class RE4Location(Location):
    game = GAME_NAME


_CLASSIFICATION = {
    "progression": ItemClassification.progression,
    "useful": ItemClassification.useful,
    "filler": ItemClassification.filler,
}

FILLER_WEIGHTS = {
    "Handgun Ammo": 10, "Shotgun Shells": 7, "Rifle Ammo": 4, "TMP Ammo": 5, "Magnum Ammo": 1,
    "Green Herb": 8, "Red Herb": 4, "First Aid Spray": 3, "Hand Grenade": 4,
    "Incendiary Grenade": 3, "Flash Grenade": 3, "Chicken Egg": 2,
    "1000 Pesetas": 6, "5000 Pesetas": 6, "10000 Pesetas": 3, "20000 Pesetas": 1,
}
EXTRA_USEFUL = ["Red9", "Blacktail", "Punisher", "Riot Gun", "Striker", "Rifle (semi-auto)",
                "TMP", "Killer7", "Mine Thrower", "Scope (Rifle)", "Stock (TMP)", "Tactical Vest",
                "Yellow Herb", "Yellow Herb", "Yellow Herb"]


STARTING_HANDGUNS = ["Red9", "Blacktail", "Punisher"]
STARTING_WEAPONS = STARTING_HANDGUNS + ["Shotgun", "Riot Gun", "Striker", "TMP", "Rifle", "Rifle (semi-auto)",
                                        "Broken Butterfly", "Killer7", "Mine Thrower"]
WEAPON_AMMO = {
    "Red9": "Handgun Ammo", "Blacktail": "Handgun Ammo", "Punisher": "Handgun Ammo",
    "Shotgun": "Shotgun Shells", "Riot Gun": "Shotgun Shells", "Striker": "Shotgun Shells",
    "TMP": "TMP Ammo", "Rifle": "Rifle Ammo", "Rifle (semi-auto)": "Rifle Ammo",
    "Broken Butterfly": "Magnum Ammo", "Killer7": "Magnum Ammo", "Mine Thrower": "Mine-Darts",
}
STARTING_SUPPLIES = ["Handgun Ammo", "Shotgun Shells", "Green Herb", "Red Herb", "Yellow Herb", "First Aid Spray",
                     "Hand Grenade", "Incendiary Grenade", "Flash Grenade"]


def _requirement(req: str, player: int) -> Callable[[CollectionState], bool]:
    options = req.split("|")
    return lambda state: any(state.has(o, player) for o in options)


class RE4World(World):
    """Resident Evil 4 (2005), Steam Ultimate HD edition. Leon S. Kennedy goes to rural Spain
    to rescue the President's daughter. Key items, weapons and treasures are shuffled
    across the multiworld."""

    game = GAME_NAME
    web = RE4Web()
    options_dataclass = RE4Options
    options: RE4Options
    settings_key = "re4uhd_options"
    settings: ClassVar[RE4Settings]
    topology_present = False

    item_name_to_id = ITEM_NAME_TO_ID
    location_name_to_id = LOCATION_NAME_TO_ID

    item_name_groups = {
        "Key Items": {i["name"] for i in ITEMS if i["classification"] == "progression" and i["kind"] == "game"},
        "Pesetas": {i["name"] for i in ITEMS if i["kind"] == "pesetas"},
    }
    location_name_groups = {
        **{f"Chapter {c}": {l["name"] for l in LOCATIONS if l["chapter"] == c}
           for c in CHAPTERS if any(l["chapter"] == c for l in LOCATIONS)},
        "Bosses": {l["name"] for l in LOCATIONS if l["kind"] == "boss"},
        "Merchant": {l["name"] for l in LOCATIONS if l["kind"] == "merchant"},
        "Shooting Gallery": {l["name"] for l in LOCATIONS if l["kind"] == "bottle_cap"},
        "Blue Medallions": {l["name"] for l in LOCATIONS if l["kind"] == "medallion_reward"},
        "Pesetas Pickups": {l["name"] for l in LOCATIONS if l["kind"] == "pesetas"},
    }

    def _location_enabled(self, loc: Dict[str, Any]) -> bool:
        kind = loc["kind"]
        if self.re_duke and self.options.re_duke_merchant and kind in ("merchant", "medallion_reward"):
            return False
        if self.re_duke and self.options.re_duke_enemies and kind == "boss":
            return False
        if kind == "pickup" and loc.get("consumable"):
            return bool(self.options.consumable_checks)
        if kind == "bonus":
            return loc["index"] <= self.options.bonus_treasure_checks.value
        if kind == "pesetas":
            return bool(self.options.pesetas_checks)
        if kind == "boss":
            return bool(self.options.boss_checks)
        if kind == "merchant":
            return bool(self.options.merchant_checks)
        if kind == "bottle_cap":
            return bool(self.options.shooting_gallery_checks)
        return True

    @property
    def merchant_check_only(self) -> bool:
        return bool(self.options.merchant_checks) and not (self.re_duke and self.options.re_duke_merchant) and \
            self.options.merchant_purchases == self.options.merchant_purchases.option_check_only

    @property
    def re_duke(self) -> bool:
        return bool(self.options.re_duke_randomizer or self.options.enemy_randomizer_compat)

    def generate_early(self) -> None:
        self.enabled_locations = [l for l in LOCATIONS if self._location_enabled(l)]

    def create_item(self, name: str) -> RE4Item:
        data = ITEMS_BY_NAME[name]
        if data["kind"] == "event":
            return RE4Item(name, ItemClassification.progression, None, self.player)
        cls = _CLASSIFICATION[data["classification"]]
        return RE4Item(name, cls, ITEM_NAME_TO_ID[name], self.player)

    def get_filler_item_name(self) -> str:
        names = list(FILLER_WEIGHTS)
        return self.random.choices(names, weights=[FILLER_WEIGHTS[n] for n in names])[0]

    def create_regions(self) -> None:
        menu = Region("Menu", self.player, self.multiworld)
        self.multiworld.regions.append(menu)
        previous = menu
        regions: Dict[str, Region] = {}
        for chapter in CHAPTERS:
            region = Region(f"Chapter {chapter}", self.player, self.multiworld)
            self.multiworld.regions.append(region)
            gates = CHAPTER_GATES.get(chapter, [])
            rules = [_requirement(g, self.player) for g in gates]
            previous.connect(region, f"To Chapter {chapter}",
                             (lambda state, rs=rules: all(r(state) for r in rs)) if rules else None)
            regions[chapter] = region
            previous = region

        for loc in self.enabled_locations:
            region = regions[loc["chapter"]]
            location = RE4Location(self.player, loc["name"], LOCATION_NAME_TO_ID[loc["name"]], region)
            reqs = [_requirement(r, self.player) for r in loc.get("requires", [])]
            if reqs:
                location.access_rule = lambda state, rs=reqs: all(r(state) for r in rs)
            if loc.get("excluded"):
                location.progress_type = LocationProgressType.EXCLUDED  # may not be obtainable: filler only
            region.locations.append(location)

        victory = RE4Location(self.player, "Defeat Saddler", None, regions["Final"])
        victory.place_locked_item(self.create_item("Victory"))
        regions["Final"].locations.append(victory)
        self.multiworld.completion_condition[self.player] = lambda state: state.has("Victory", self.player)

    def create_items(self) -> None:
        pool: List[RE4Item] = []
        key_items = self.item_name_groups["Key Items"]
        for loc in self.enabled_locations:
            # merchant stock joins the pool when buying only sends the check
            if loc["kind"] == "merchant" and self.merchant_check_only:
                pool.append(self.create_item(loc["vanilla"]))
                continue
            if loc["kind"] != "pickup":
                continue
            name = loc["vanilla"]
            if not self.options.shuffle_key_items and name in key_items:
                continue  # placed on its vanilla location below
            pool.append(self.create_item(name))

        if not self.options.shuffle_key_items:
            for loc in self.enabled_locations:
                if loc["kind"] == "pickup" and loc["vanilla"] in key_items:
                    location = self.multiworld.get_location(loc["name"], self.player)
                    location.place_locked_item(self.create_item(loc["vanilla"]))

        unfilled = len(self.multiworld.get_unfilled_locations(self.player))
        extra = unfilled - len(pool)
        have = [i.name for i in pool]
        extras: List[str] = ["Progressive Attache Case"] * max(0, 3 - have.count("Progressive Attache Case"))
        extras += [n for n in EXTRA_USEFUL if n not in have or n == "Yellow Herb"]
        for name in extras:
            if extra <= 0:
                break
            pool.append(self.create_item(name))
            extra -= 1
        while extra > 0:
            pool.append(self.create_item(self.get_filler_item_name()))
            extra -= 1
        self.multiworld.itempool += pool
        self._push_starting_inventory()

    def _push_starting_inventory(self) -> None:
        start: List[str] = []
        if self.options.starting_weapon == self.options.starting_weapon.option_random_handgun:
            weapon = self.random.choice(STARTING_HANDGUNS)
        elif self.options.starting_weapon == self.options.starting_weapon.option_random_weapon:
            weapon = self.random.choice(STARTING_WEAPONS)
        else:
            weapon = None
        if weapon:
            start += [weapon, WEAPON_AMMO[weapon], WEAPON_AMMO[weapon]]
        start += [self.random.choice(STARTING_SUPPLIES) for _ in range(self.options.starting_supplies.value)]
        pesetas = self.options.starting_pesetas.value // 1000 * 1000
        for amount in (20000, 10000, 5000, 1000):
            while pesetas >= amount:
                start.append(f"{amount} Pesetas")
                pesetas -= amount
        for name in start:
            self.multiworld.push_precollected(self.create_item(name))

    def fill_slot_data(self) -> Dict[str, Any]:
        locs = []
        for l in self.enabled_locations:
            entry = {"id": LOCATION_NAME_TO_ID[l["name"]], "k": l["kind"]}
            for key in ("room", "em_id"):
                if key in l:
                    entry[key] = l[key]
            if "game_items" in l:
                entry["items"] = l["game_items"]
            if l.get("room_confidence") and l["room_confidence"] != "high":
                entry["loose"] = 1
            if l.get("cut"):
                entry["cut"] = 1
            if l["kind"] in ("bonus", "pesetas"):
                entry["stage"] = l["stage"]
            if l.get("consumable"):
                entry["c"] = 1
            locs.append(entry)
        items = []
        for i in ITEMS:
            if i["kind"] == "event":
                continue
            entry = {"id": ITEM_NAME_TO_ID[i["name"]], "k": i["kind"]}
            if "game_item" in i:
                entry["g"] = i["game_item"]
            if "amount" in i:
                entry["amount"] = i["amount"]
            items.append(entry)
        return {
            "version": 1,
            "death_link": bool(self.options.death_link),
            "location_base": LOCATION_BASE_ID,
            "merchant_check_only": self.merchant_check_only,
            "enemy_health": list(ENEMY_HEALTH_RANGES[self.options.random_enemy_health.value])
            if self.options.random_enemy_health.value in ENEMY_HEALTH_RANGES else None,
            "enemy_randomizer_compat": self.re_duke,
            "re_duke": rando_settings(
                self.options.re_duke_preset.value, bool(self.options.re_duke_enemies),
                bool(self.options.re_duke_enemy_health), bool(self.options.re_duke_merchant),
                bool(self.options.re_duke_starting_loadout),
                # same enemies every time this slot regenerates; kept small for the randomizer's number box
                1 + zlib.crc32(f"{self.multiworld.seed_name}:{self.player}".encode()) % 99,
            ) if self.re_duke else None,
            "locations": locs,
            "items": items,
        }
