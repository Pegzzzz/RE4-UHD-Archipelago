from dataclasses import dataclass

from Options import Choice, DeathLink, DefaultOnToggle, OptionGroup, PerGameCommonOptions, Range, Toggle, Visibility


class ShuffleKeyItems(DefaultOnToggle):
    """Shuffle key items (Insignia Key, False Eye, Crests, Card Keys...) into the multiworld.
    When off, every key item stays where it is in the vanilla game."""
    display_name = "Shuffle Key Items"


class ConsumableChecks(DefaultOnToggle):
    """Ammo, herbs, grenades and sprays placed in the world are checks (about 450 of them).
    In each room, every consumable you pick up takes that room's next spot until they're all collected."""
    display_name = "Consumable Checks"


class MerchantChecks(DefaultOnToggle):
    """The first purchase of each weapon or item the Merchant sells is a check.
    You still receive the item you bought."""
    display_name = "Merchant Checks"


class BossChecks(DefaultOnToggle):
    """Defeating a boss (Del Lago, El Gigantes, Mendez, Verdugo, Salazar, U-3, Krauser) is a check."""
    display_name = "Boss Checks"


class ShootingGalleryChecks(DefaultOnToggle):
    """Each of the 24 bottle caps won at the Merchant's shooting gallery is a check."""
    display_name = "Shooting Gallery Checks"


class StartingWeapon(Choice):
    """Extra weapon Leon starts with, given with two boxes of its ammo.
    vanilla: just the normal Handgun.
    random_handgun: one of Red9, Blacktail or Punisher.
    random_weapon: any weapon (shotguns, rifles, TMP, magnums, Mine Thrower, handguns)."""
    display_name = "Starting Weapon"
    option_vanilla = 0
    option_random_handgun = 1
    option_random_weapon = 2
    default = 0


class StartingSupplies(Range):
    """Number of random supplies (ammo, herbs, grenades, sprays) Leon starts with."""
    display_name = "Starting Supplies"
    range_start = 0
    range_end = 10
    default = 0


class StartingPesetas(Range):
    """Pesetas Leon starts with (rounded down to a multiple of 1000)."""
    display_name = "Starting Pesetas"
    range_start = 0
    range_end = 100000
    default = 0


class ReDukeRandomizer(Toggle):
    """Also use re_duke's RE4 PC Randomizer (Patreon / moddb.com/mods/re4randomizer, installed separately inside
    the game folder) for random enemies, enemy health, Merchant stock/prices/upgrades and starting loadout.
    The RE4 UHD Client's /setup command writes an Archipelago-safe settings profile for it and opens it:
    doors, item and key-item randomization are always turned off there, because Archipelago places the items.
    This turns off Merchant checks, boss checks and the blue medallion reward (that mod changes them);
    the goal also triggers when you reach the jet-ski escape."""
    display_name = "re_duke Randomizer"


class ReDukePreset(Choice):
    """Which of the randomizer's own presets to start from (Hard has more and tougher enemy types)."""
    display_name = "re_duke Randomizer Preset"
    option_default = 0
    option_normal = 1
    option_hard = 2
    default = 0


class ReDukeEnemies(DefaultOnToggle):
    """Randomize enemies (needs re_duke_randomizer)."""
    display_name = "re_duke: Random Enemies"


class ReDukeEnemyHealth(DefaultOnToggle):
    """Randomize enemy health within the preset's ranges (needs re_duke_randomizer)."""
    display_name = "re_duke: Random Enemy Health"


class ReDukeMerchant(DefaultOnToggle):
    """Randomize the Merchant's stock, prices and weapon upgrades (needs re_duke_randomizer)."""
    display_name = "re_duke: Random Merchant"


class ReDukeLoadout(Toggle):
    """Let the randomizer also roll a random starting loadout, on top of this world's starting options
    (needs re_duke_randomizer)."""
    display_name = "re_duke: Random Starting Loadout"


class EnemyRandomizerCompat(Toggle):
    """Deprecated (0.2.0): same as re_duke_randomizer: true."""
    display_name = "Enemy Randomizer Compatibility (deprecated)"
    visibility = Visibility.none


@dataclass
class RE4Options(PerGameCommonOptions):
    shuffle_key_items: ShuffleKeyItems
    consumable_checks: ConsumableChecks
    merchant_checks: MerchantChecks
    boss_checks: BossChecks
    shooting_gallery_checks: ShootingGalleryChecks
    starting_weapon: StartingWeapon
    starting_supplies: StartingSupplies
    starting_pesetas: StartingPesetas
    re_duke_randomizer: ReDukeRandomizer
    re_duke_preset: ReDukePreset
    re_duke_enemies: ReDukeEnemies
    re_duke_enemy_health: ReDukeEnemyHealth
    re_duke_merchant: ReDukeMerchant
    re_duke_starting_loadout: ReDukeLoadout
    enemy_randomizer_compat: EnemyRandomizerCompat
    death_link: DeathLink


OPTION_GROUPS = [
    OptionGroup("Checks", [ShuffleKeyItems, ConsumableChecks, MerchantChecks, BossChecks, ShootingGalleryChecks]),
    OptionGroup("Starting Inventory", [StartingWeapon, StartingSupplies, StartingPesetas]),
    OptionGroup("re_duke Randomizer", [ReDukeRandomizer, ReDukePreset, ReDukeEnemies, ReDukeEnemyHealth,
                                       ReDukeMerchant, ReDukeLoadout]),
]
