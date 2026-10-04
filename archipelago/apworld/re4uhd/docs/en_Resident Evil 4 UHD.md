# Resident Evil 4 UHD

## What does randomization do to this game?

Key items, the weapons lying around the world, every fixed treasure pickup and (optionally) every placed ammo,
herb, grenade and spray are shuffled into the multiworld, about 750 locations in total. When Leon takes one of those items, it is removed again and its location is sent as a check;
whatever item that location holds (for you or another player) is delivered by Archipelago.

Optional checks: defeating bosses, the first purchase of each Merchant item (by default the purchase only sends
the check and the item comes from the multiworld), the 24 shooting gallery bottle caps, the Merchant's blue
medallion reward, placed pesetas pickups (you keep the money), and bonus checks for treasures nothing else
counts (like random enemy drops). Random enemy health is built in (`random_enemy_health`).

## What is the goal?

Defeat Osmund Saddler.

## Which items stay vanilla?

Items Ashley picks up during her solo segment (chapter 3-4), the Rocket Launcher (Special) Ada throws you
for the final fight, and the Jet-ski Key. Pesetas you pick up are kept (placed ones also send a check).

## How do ammo and herb checks work?

Each room has as many consumable checks as the game places ammo/herbs/grenades/sprays there. Any placed
consumable you pick up in that room takes the room's next check until they're all collected. Enemy drops never
count: the game mod uses the game's own "item taken" room flags to tell placed items from drops.

## What does another player's item look like in my game?

You see the original item's pickup screen, then it disappears and a message shows what you found.
