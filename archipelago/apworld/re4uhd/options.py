from dataclasses import dataclass

from Options import Choice, DeathLink, DefaultOnToggle, PerGameCommonOptions, Range, Toggle


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


class EnemyRandomizerCompat(Toggle):
    """Turn this on if you also play with re_duke's RE4 Enemy/Merchant Randomizer (moddb.com/mods/re4randomizer).
    That mod changes the Merchant's stock and replaces boss fights, so Merchant checks, boss checks and the
    blue medallion reward are turned off. The goal then triggers when you reach the jet-ski escape."""
    display_name = "Enemy Randomizer Compatibility"


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
    enemy_randomizer_compat: EnemyRandomizerCompat
    death_link: DeathLink
