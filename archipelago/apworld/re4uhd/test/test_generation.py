from . import RE4TestBase


class TestDefault(RE4TestBase):
    def test_goal_needs_all_chapter_gates(self) -> None:
        self.assertFalse(self.can_reach_location("Defeat Saddler"))
        self.collect_all_but(["Emergency Lock Card Key"])
        self.assertFalse(self.can_reach_location("Defeat Saddler"))
        self.collect_by_name("Emergency Lock Card Key")
        self.assertTrue(self.can_reach_location("Defeat Saddler"))

    def test_either_gondola_key(self) -> None:
        self.collect_all_but(["Camp Key", "Old Key"])
        self.assertFalse(self.can_reach_region("Chapter 3-1"))
        self.collect_by_name("Old Key")
        self.assertTrue(self.can_reach_region("Chapter 3-1"))

    def test_golden_sword_needs_platinum(self) -> None:
        self.collect_all_but(["Platinum Sword"])
        self.assertFalse(self.can_reach_location("3-1 Barracks: Golden Sword"))


class TestVanillaKeys(RE4TestBase):
    options = {"shuffle_key_items": False}

    def test_key_items_on_vanilla_spots(self) -> None:
        loc = self.multiworld.get_location("1-2 Chief's House: Insignia Key", self.player)
        self.assertEqual(loc.item.name, "Insignia Key")


class TestMinimal(RE4TestBase):
    options = {"merchant_checks": False, "boss_checks": False, "shooting_gallery_checks": False,
               "consumable_checks": False}

    def test_no_consumables(self) -> None:
        names = {l.name for l in self.multiworld.get_locations(self.player)}
        self.assertNotIn("1-1 Woods: Handgun Ammo", names)


class TestConsumables(RE4TestBase):
    def test_consumable_gated_by_room(self) -> None:
        self.assertIn("1-1 Woods: Handgun Ammo", {l.name for l in self.multiworld.get_locations(self.player)})
        self.collect_all_but(["Waste Disposal Card Key"])
        gated = [l for l in self.multiworld.get_locations(self.player)
                 if l.name.startswith("5-1") and "Waste Disposal" in l.name]
        for loc in self.multiworld.get_locations(self.player):
            if loc.name.startswith("5-1") and "Waste Disposal Area" in loc.name:
                self.assertFalse(loc.can_reach(self.multiworld.state), loc.name)


class TestEverything(RE4TestBase):
    options = {"death_link": True}
