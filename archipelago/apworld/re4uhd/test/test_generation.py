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
    options = {"merchant_checks": False, "boss_checks": False, "shooting_gallery_checks": False}


class TestEverything(RE4TestBase):
    options = {"death_link": True}
