from collections import Counter

from . import RE4TestBase
from .. import STARTING_HANDGUNS, STARTING_WEAPONS, WEAPON_AMMO
from ..data_loader import CHAPTER_GATES, CHAPTERS, ITEMS_BY_NAME, LOCATION_BASE_ID, LOCATIONS


def _precollected(test: RE4TestBase) -> Counter:
    return Counter(i.name for i in test.multiworld.precollected_items[test.player])


class TestRandomHandgun(RE4TestBase):
    options = {"starting_weapon": "random_handgun"}

    def test_handgun_and_ammo(self) -> None:
        start = _precollected(self)
        weapons = [w for w in STARTING_HANDGUNS if start[w]]
        self.assertEqual(len(weapons), 1, start)
        self.assertEqual(start["Handgun Ammo"], 2)


class TestRandomWeapon(RE4TestBase):
    options = {"starting_weapon": "random_weapon", "starting_supplies": 5, "starting_pesetas": 37500}

    def test_weapon_supplies_pesetas(self) -> None:
        start = _precollected(self)
        weapons = [w for w in STARTING_WEAPONS if start[w]]
        self.assertEqual(len(weapons), 1, start)
        self.assertGreaterEqual(start[WEAPON_AMMO[weapons[0]]], 2)
        pesetas = sum(int(n.split()[0]) * c for n, c in start.items() if n.endswith("Pesetas"))
        self.assertEqual(pesetas, 37000)
        others = sum(start.values()) - 3 - sum(c for n, c in start.items() if n.endswith("Pesetas"))
        self.assertEqual(others, 5)


class TestVanillaStart(RE4TestBase):
    def test_nothing_extra(self) -> None:
        self.assertEqual(sum(_precollected(self).values()), 0)


class TestEnemyRandomizerCompat(RE4TestBase):
    options = {"enemy_randomizer_compat": True}

    def test_conflicting_checks_removed(self) -> None:
        names = {l.name for l in self.multiworld.get_locations(self.player)}
        self.assertFalse(any(n.startswith("Merchant:") for n in names))
        self.assertFalse(any(n.startswith("Defeat ") and n != "Defeat Saddler" for n in names))
        self.assertNotIn("Blue Medallions: Merchant Reward", names)
        self.assertTrue(any(n.startswith("Shooting Gallery") for n in names))
        self.assertTrue(self.world.fill_slot_data()["enemy_randomizer_compat"])


class TestChapterGates(RE4TestBase):
    """Every chapter gate really blocks the chapter, and the gate item alone is enough to open it."""

    def test_each_gate(self) -> None:
        for chapter, gates in CHAPTER_GATES.items():
            for gate in gates:
                options = gate.split("|")
                with self.subTest(chapter=chapter, gate=gate):
                    self.multiworld.state = self.multiworld.get_all_state(False)
                    for opt in options:
                        while self.multiworld.state.has(opt, self.player):
                            self.multiworld.state.remove(self.get_item_by_name(opt))
                    self.assertFalse(self.can_reach_region(f"Chapter {chapter}"),
                                     f"Chapter {chapter} reachable without {gate}")


class TestData(RE4TestBase):
    def test_requirements_name_real_items(self) -> None:
        for loc in LOCATIONS:
            for req in loc.get("requires", []):
                for opt in req.split("|"):
                    self.assertIn(opt, ITEMS_BY_NAME, f"{loc['name']} requires unknown item {opt}")

    def test_chapters_known(self) -> None:
        for loc in LOCATIONS:
            self.assertIn(loc["chapter"], CHAPTERS, loc["name"])

    def test_slot_data_fits_the_save(self) -> None:
        data = self.world.fill_slot_data()
        ids = [l["id"] for l in data["locations"]]
        self.assertEqual(len(ids), len(set(ids)))
        self.assertTrue(all(0 <= i - LOCATION_BASE_ID < 1024 for i in ids))
        for item in data["items"]:
            if item["k"] == "game":
                self.assertTrue(0 <= item["g"] < 272, item)

    def test_every_key_item_once(self) -> None:
        from .. import LOCKED_VANILLA
        pool = Counter(i.name for i in self.multiworld.itempool if i.player == self.player)
        for name in self.world.item_name_groups["Key Items"]:
            self.assertEqual(pool[name], 0 if name in LOCKED_VANILLA else 1, name)
        for name in LOCKED_VANILLA:  # placed on their own spots instead
            locs = [l for l in self.multiworld.get_locations(self.player) if l.item and l.item.name == name]
            self.assertEqual(len(locs), 1, name)
            self.assertTrue(locs[0].locked, name)

    def test_pickup_rooms_are_real(self) -> None:
        for loc in LOCATIONS:
            if loc["kind"] in ("pickup", "boss"):
                self.assertIn(loc["room"] >> 8, (1, 2, 3), loc["name"])


class TestMerchantCheckOnly(RE4TestBase):
    """Default: buying only sends the check, and the Merchant's stock is in the item pool instead."""

    def test_stock_in_pool(self) -> None:
        self.assertTrue(self.world.fill_slot_data()["merchant_check_only"])
        pool = Counter(i.name for i in self.multiworld.itempool if i.player == self.player)
        for name in ("Rifle", "Handgun", "Treasure Map (Village)", "Treasure Map (Castle)", "Treasure Map (Island)",
                     "Striker", "Killer7", "Scope (semi-auto rifle)"):
            self.assertGreaterEqual(pool[name], 1, name)
        self.assertEqual(pool["Progressive Attache Case"], 3)

    def test_one_item_per_location(self) -> None:
        mine = [i for i in self.multiworld.itempool if i.player == self.player]
        locs = [l for l in self.multiworld.get_locations(self.player) if l.address is not None and not l.locked]
        self.assertEqual(len(mine), len(locs))


class TestMerchantKeepItem(RE4TestBase):
    options = {"merchant_purchases": "keep_item"}

    def test_flag(self) -> None:
        self.assertFalse(self.world.fill_slot_data()["merchant_check_only"])


class TestCheckOnlyOffWithReDuke(RE4TestBase):
    options = {"re_duke_randomizer": True}

    def test_flag(self) -> None:
        self.assertFalse(self.world.fill_slot_data()["merchant_check_only"])


class TestBonusTreasure(RE4TestBase):
    options = {"bonus_treasure_checks": 3}

    def _fill(self) -> None:
        from Fill import distribute_items_restrictive
        from worlds.AutoWorld import call_all
        call_all(self.multiworld, "pre_fill")
        distribute_items_restrictive(self.multiworld)

    def test_bonus_locations(self) -> None:
        from BaseClasses import LocationProgressType
        self._fill()
        bonus = [l for l in self.multiworld.get_locations(self.player) if "Bonus Treasure" in l.name]
        self.assertEqual(len(bonus), 9)
        for loc in bonus:
            self.assertEqual(loc.progress_type, LocationProgressType.EXCLUDED, loc.name)
            self.assertFalse(loc.item.advancement, f"{loc.name} holds progression")
        stages = {l["stage"] for l in self.world.fill_slot_data()["locations"] if l["k"] == "bonus"}
        self.assertEqual(stages, {1, 2, 3})

    def test_unverified_are_filler_only(self) -> None:
        self._fill()
        for loc in self.multiworld.get_locations(self.player):
            if loc.name.endswith("(unverified)"):
                self.assertFalse(loc.item.advancement, loc.name)


class TestNoBonus(RE4TestBase):
    options = {"bonus_treasure_checks": 0}

    def test_none(self) -> None:
        self.assertFalse(any("Bonus Treasure" in l.name for l in self.multiworld.get_locations(self.player)))


class TestPesetas(RE4TestBase):
    def test_pesetas_locations(self) -> None:
        from BaseClasses import LocationProgressType
        from Fill import distribute_items_restrictive
        from worlds.AutoWorld import call_all
        call_all(self.multiworld, "pre_fill")
        distribute_items_restrictive(self.multiworld)
        pesetas = [l for l in self.multiworld.get_locations(self.player) if "Pesetas " in l.name]
        self.assertEqual(len(pesetas), 75)
        for loc in pesetas:
            self.assertEqual(loc.progress_type, LocationProgressType.EXCLUDED, loc.name)
            self.assertFalse(loc.item.advancement, f"{loc.name} holds progression")
        slot = [l for l in self.world.fill_slot_data()["locations"] if l["k"] == "pesetas"]
        counts = Counter(l["stage"] for l in slot)
        self.assertEqual(counts, {1: 25, 2: 25, 3: 25})

    def test_chapters_follow_stage(self) -> None:
        for loc in LOCATIONS:
            if loc["kind"] == "pesetas":
                stage = {"1": 1, "2": 1, "3": 2, "4": 2, "5": 3, "F": 3}[loc["chapter"][0]]
                self.assertEqual(stage, loc["stage"], loc["name"])


class TestNoPesetas(RE4TestBase):
    options = {"pesetas_checks": 0}

    def test_none(self) -> None:
        self.assertFalse(any("Pesetas " in l.name for l in self.multiworld.get_locations(self.player)))


class TestReDukeEnemiesOnly(RE4TestBase):
    options = {"re_duke_randomizer": True, "re_duke_merchant": False}

    def test_merchant_kept_bosses_off(self) -> None:
        names = {l.name for l in self.multiworld.get_locations(self.player)}
        self.assertTrue(any(n.startswith("Merchant:") for n in names))
        self.assertIn("Blue Medallions: Merchant Reward", names)
        self.assertFalse(any(n.startswith("Defeat ") and n != "Defeat Saddler" for n in names))
        data = self.world.fill_slot_data()
        self.assertTrue(data["merchant_check_only"])
        self.assertEqual(data["re_duke"]["overrides"]["randomizeMerchantStockCheckBox"], "0")
        self.assertEqual(data["re_duke"]["overrides"]["randomizeEnemiesCheckBox"], "1")


class TestReDukeMerchantOnly(RE4TestBase):
    options = {"re_duke_randomizer": True, "re_duke_enemies": False}

    def test_bosses_kept_merchant_off(self) -> None:
        names = {l.name for l in self.multiworld.get_locations(self.player)}
        self.assertFalse(any(n.startswith("Merchant:") for n in names))
        self.assertTrue(any(n.startswith("Defeat ") and n != "Defeat Saddler" for n in names))


class TestRandomEnemyHealth(RE4TestBase):
    options = {"random_enemy_health": "wild"}

    def test_range_in_slot_data(self) -> None:
        self.assertEqual(self.world.fill_slot_data()["enemy_health"], [0.5, 2.5])


class TestEnemyHealthOff(RE4TestBase):
    def test_off(self) -> None:
        self.assertIsNone(self.world.fill_slot_data()["enemy_health"])


class TestPesetasOldYaml(RE4TestBase):
    options = {"pesetas_checks": True}  # 0.5.0 YAMLs used a toggle

    def test_true_means_default(self) -> None:
        self.assertEqual(self.world.options.pesetas_checks.value, 25)


class TestMaxPesetas(RE4TestBase):
    options = {"pesetas_checks": 50}

    def test_fifty_each(self) -> None:
        slot = [l for l in self.world.fill_slot_data()["locations"] if l["k"] == "pesetas"]
        self.assertEqual(Counter(l["stage"] for l in slot), {1: 50, 2: 50, 3: 50})


class TestMerchantKeptNotInPool(RE4TestBase):
    def test_vest_and_stocks_not_duplicated(self) -> None:
        pool = Counter(i.name for i in self.multiworld.itempool if i.player == self.player)
        for name in ("Tactical Vest", "Stock (TMP)", "Stock (Red9)"):
            self.assertEqual(pool[name], 0, name)
        self.assertGreaterEqual(pool["Red9"], 1)


class TestFillerOnlyChecks(RE4TestBase):
    def test_bosses_handgun_special_caps(self) -> None:
        from BaseClasses import LocationProgressType
        for loc in self.multiworld.get_locations(self.player):
            if loc.name.startswith("Defeat ") and loc.address is not None or loc.name == "Merchant: Buy Handgun" or \
                    loc.name.endswith(("Ada Wong Cap", "Bella Sisters Cap", "Don Pedro Cap", "J.J Cap")):
                self.assertEqual(loc.progress_type, LocationProgressType.EXCLUDED, loc.name)


class TestKeepLocations(RE4TestBase):
    def test_holy_beast_pieces_kept(self) -> None:
        from ..data_loader import LOCATION_NAME_TO_ID
        data = self.world.fill_slot_data()
        for name in ("5-3 Pillared Platform: Piece of the Holy Beast, Panther",
                     "5-3 Tower Roof: Piece of the Holy Beast, Eagle", "5-3 Tower Roof: Piece of the Holy Beast, Serpent"):
            self.assertIn(LOCATION_NAME_TO_ID[name], data["keep_locations"], name)
        for entry in data["locations"]:
            if entry.get("keep"):
                self.assertFalse(entry.get("c"), "consumable spots never keep (they're type-agnostic)")


class TestVanillaKeyItemsKept(RE4TestBase):
    options = {"shuffle_key_items": False}

    def test_keep(self) -> None:
        from ..data_loader import LOCATION_NAME_TO_ID
        keep = set(self.world.fill_slot_data()["keep_locations"])
        self.assertIn(LOCATION_NAME_TO_ID["1-2 Chief's House: Insignia Key"], keep)


class TestLogicAudit(RE4TestBase):
    """0.5.3: requirements and filler-only checks from the logic audit."""

    def test_requirements(self) -> None:
        by_name = {l["name"]: l for l in LOCATIONS}
        for name in ("4-1 Pit: Crown", "4-1 Waterway Passage 1: Spinel"):
            self.assertTrue({"Lion Ornament", "King's Grail", "Queen's Grail"} <= set(by_name[name]["requires"]), name)
        self.assertTrue({"Gallery Key", "Goat Ornament"} <= set(by_name["3-2 Hedge Maze: Moonstone (Right half)"]["requires"]))
        self.assertIn("Prison Key", by_name["Shooting Gallery A: Leon w/ handgun Cap"]["requires"])
        self.assertIn("Queen's Grail", by_name["Shooting Gallery B: Don Jose Cap"]["requires"])
        self.assertIn("Infrared Scope", by_name["5-1 Laboratory Control Center: Storage Room Card Key"]["requires"])
        self.assertIn("Gallery Key", CHAPTER_GATES["3-3"])
        self.assertEqual(ITEMS_BY_NAME["Infrared Scope"]["classification"], "progression")
        self.assertIn(137, by_name["1-1 Farm: Pearl Pendant"]["game_items"])
        self.assertIn(138, by_name["1-2 Chief's House: Brass Pocket Watch"]["game_items"])

    def test_filler_only(self) -> None:
        from BaseClasses import LocationProgressType
        for loc in self.multiworld.get_locations(self.player):
            if loc.name.startswith("Merchant:") or loc.name in ("Blue Medallions: Merchant Reward", "1-1 Woods: Spinel",
                                                                "4-1 Treasure Chamber: Broken Butterfly",
                                                                "2-1 Colosseum: Spinel #1"):
                self.assertEqual(loc.progress_type, LocationProgressType.EXCLUDED, loc.name)
