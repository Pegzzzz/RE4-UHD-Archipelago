from dataclasses import dataclass

from Options import DeathLink, DefaultOnToggle, PerGameCommonOptions


class ShuffleKeyItems(DefaultOnToggle):
    """Shuffle key items (Insignia Key, False Eye, Crests, Card Keys...) into the multiworld.
    When off, every key item stays where it is in the vanilla game."""
    display_name = "Shuffle Key Items"


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


@dataclass
class RE4Options(PerGameCommonOptions):
    shuffle_key_items: ShuffleKeyItems
    merchant_checks: MerchantChecks
    boss_checks: BossChecks
    shooting_gallery_checks: ShootingGalleryChecks
    death_link: DeathLink
