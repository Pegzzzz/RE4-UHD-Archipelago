from dataclasses import dataclass

from Options import Choice, DeathLink, DefaultOnToggle, OptionGroup, PerGameCommonOptions, Range, Toggle, Visibility


class ShuffleKeyItems(DefaultOnToggle):
    """Shuffle key items (Insignia Key, False Eye, Crests, Card Keys...) into the multiworld.
    When off, every key item stays where it is in the vanilla game."""
    display_name = "Shuffle Key Items"


class ConsumableChecks(DefaultOnToggle):
    """Ammo, herbs, grenades and sprays placed in the world are checks (about 450 of them).
    In each room, every placed consumable you pick up takes that room's next spot until they're all collected.
    Enemy drops don't count."""
    display_name = "Consumable Checks"


class ConsumableProgression(Toggle):
    """Let ammo/herb/grenade checks hold progression items (key items, card keys...).
    Off (recommended): they can still hold weapons and other useful items, just never something you need to finish.
    How many ammo/herb spots each room has comes from community guides; if a guide counted one too many, that spot
    can never be collected, and with this on, a key item could be stuck there."""
    display_name = "Consumable Progression"


class MerchantChecks(DefaultOnToggle):
    """The first purchase of each weapon or item the Merchant sells is a check.
    You still receive the item you bought."""
    display_name = "Merchant Checks"


class MerchantPurchases(Choice):
    """What happens to the item when a Merchant purchase sends a check (needs merchant_checks).
    check_only: the first purchase of each item only sends the check; the item is taken away when you leave the
    shop, and the Merchant's stock is shuffled into the multiworld instead (attache cases and the tactical vest
    always take effect).
    keep_item: you also keep what you bought.
    Later purchases of the same item are normal."""
    display_name = "Merchant Purchases"
    option_check_only = 0
    option_keep_item = 1
    default = 0


class BonusTreasureChecks(Range):
    """Extra filler-only checks per stage (village, castle, island) for treasures no other check accounts for,
    such as random enemy drops. Each one gives a minor item."""
    display_name = "Bonus Treasure Checks"
    range_start = 0
    range_end = 15
    default = 5


class PesetasChecks(Range):
    """How many placed pesetas pickups per stage (village, castle, island) are checks, 0-50.
    Each placed pesetas pickup (cabinets, tables, crates, bird nests...) takes its stage's next check and you keep
    the money. Enemy and boss drops don't count. Filler-only: these checks give minor items.
    Every stage has well over 25 placed pesetas; the island may have fewer than 50."""
    display_name = "Pesetas Checks"
    range_start = 0
    range_end = 50
    default = 25

    @classmethod
    def from_any(cls, data):
        if isinstance(data, bool):  # 0.5.0 YAMLs: pesetas_checks: true/false
            return cls(cls.default if data else 0)
        if isinstance(data, str) and data.lower() in ("true", "false", "on", "off"):
            return cls(cls.default if data.lower() in ("true", "on") else 0)
        return super().from_any(data)


class EnemyDropChecks(Range):
    """Enemies can drop important items: the items enemies drop (ammo, herbs, grenades, pesetas, also random
    barrel/crate contents) become checks, 0-30 per area (village, castle, island). Each drop you pick up sends the
    area's next drop check, and you keep the drop. These checks can hold anything, including key items.
    0 turns them off."""
    display_name = "Enemy Drop Checks"
    range_start = 0
    range_end = 30
    default = 0


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


class RandomEnemyHealth(Choice):
    """Built in, no other download needed: every enemy spawns with random health.
    off: vanilla health.
    mild: 75% to 150%.
    tough: 100% to 200%.
    wild: 50% to 250%.
    chaos: 25% to 400%.
    Bosses are included. Leave this off if re_duke's randomizer already randomizes enemy health."""
    display_name = "Random Enemy Health"
    option_off = 0
    option_mild = 1
    option_tough = 2
    option_wild = 3
    option_chaos = 4
    default = 0


ENEMY_HEALTH_RANGES = {1: (0.75, 1.5), 2: (1.0, 2.0), 3: (0.5, 2.5), 4: (0.25, 4.0)}


class ReDukeRandomizer(Toggle):
    """Also use re_duke's RE4 PC Randomizer (Patreon / moddb.com/mods/re4randomizer, installed separately inside
    the game folder) for random enemies, enemy health, Merchant stock/prices/upgrades and starting loadout.
    The RE4 UHD Client's /setup command writes an Archipelago-safe settings profile for it and opens it:
    doors, item and key-item randomization are always turned off there, because Archipelago places the items.
    With re_duke_enemies on, boss checks are turned off (random bosses can appear in other rooms); with
    re_duke_merchant on, Merchant checks and the blue medallion reward are turned off. For random enemies only,
    turn re_duke_merchant off. The goal also triggers when you reach the jet-ski escape."""
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
    consumable_progression: ConsumableProgression
    merchant_checks: MerchantChecks
    merchant_purchases: MerchantPurchases
    bonus_treasure_checks: BonusTreasureChecks
    pesetas_checks: PesetasChecks
    enemy_drop_checks: EnemyDropChecks
    boss_checks: BossChecks
    shooting_gallery_checks: ShootingGalleryChecks
    starting_weapon: StartingWeapon
    starting_supplies: StartingSupplies
    starting_pesetas: StartingPesetas
    random_enemy_health: RandomEnemyHealth
    re_duke_randomizer: ReDukeRandomizer
    re_duke_preset: ReDukePreset
    re_duke_enemies: ReDukeEnemies
    re_duke_enemy_health: ReDukeEnemyHealth
    re_duke_merchant: ReDukeMerchant
    re_duke_starting_loadout: ReDukeLoadout
    enemy_randomizer_compat: EnemyRandomizerCompat
    death_link: DeathLink


OPTION_GROUPS = [
    OptionGroup("Checks", [ShuffleKeyItems, ConsumableChecks, ConsumableProgression, PesetasChecks, EnemyDropChecks, MerchantChecks, MerchantPurchases,
                           BonusTreasureChecks, BossChecks, ShootingGalleryChecks]),
    OptionGroup("Starting Inventory", [StartingWeapon, StartingSupplies, StartingPesetas]),
    OptionGroup("Enemies", [RandomEnemyHealth]),
    OptionGroup("re_duke Randomizer", [ReDukeRandomizer, ReDukePreset, ReDukeEnemies, ReDukeEnemyHealth,
                                       ReDukeMerchant, ReDukeLoadout]),
]
