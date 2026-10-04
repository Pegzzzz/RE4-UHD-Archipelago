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
        self.assertTrue(all(0 <= i - LOCATION_BASE_ID < 768 for i in ids))
        for item in data["items"]:
            if item["k"] == "game":
                self.assertTrue(0 <= item["g"] < 272, item)

    def test_every_key_item_once(self) -> None:
        pool = Counter(i.name for i in self.multiworld.itempool if i.player == self.player)
        for name in self.world.item_name_groups["Key Items"]:
            self.assertEqual(pool[name], 1, name)

    def test_pickup_rooms_are_real(self) -> None:
        for loc in LOCATIONS:
            if loc["kind"] in ("pickup", "boss"):
                self.assertIn(loc["room"] >> 8, (1, 2, 3), loc["name"])
