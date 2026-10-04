# RE4 (2005/UHD) fixed item placements: research notes

Dataset: `items_research.json` has 201 entries for Leon's main game, Normal mode.
Extra fields: `location_detail` (where the item is in the area, in Evil Resource's wording) and `notes` (conflicts and caveats).
`requires` uses `"A|B"` for either/or (Camp Key or Old Key).

## Sources
- Evil Resource item, treasure and area pages (main source for placement and use). Examples: https://www.evilresource.com/resident-evil-4/items/<slug>, /treasures/<slug>, /weaponry/<slug>, /maps/<region>/<section>/<area>
- StrategyWiki chapter walkthroughs (used for chapter assignment and order): https://strategywiki.org/wiki/Resident_Evil_4/Chapter_X-Y (Final_Chapter returned 403)
- Fandom wiki (gem PC-vs-GameCube swaps, Broken Butterfly, Final Chapter): https://residentevil.fandom.com/wiki/Purple_Gem, /Green_Gem, /Red_Gem, /Final_Chapter
- Room-id meanings: ModDB "Resident Evil 4 Area's List": https://www.moddb.com/games/resident-evil-4/tutorials/resident-evil-4-areas-list
- I could not reach GameFAQs, IGN or Wikipedia (proxy 403 or paywall).

## Scope decisions
- **There are no small keys in RE4 (2005).** There are no locked drawers either. Nothing in the dataset uses the small_key category.
- **No weapons or attachments other than these are free pickups.** The free ones are the Shotgun (1-1), Rocket Launcher (3-3 glass case), Broken Butterfly (4-1 Treasure Chamber), Infrared Scope (5-1 Freezer) and the scripted Rocket Launcher (Special) (Final). Evil Resource lists these as merchant-only: Rifle, Riot Gun, Striker, TMP, Red9, Punisher, Mine Thrower and the semi-auto Rifle. The Punisher comes from the Blue Medallion side quest or a later purchase. Every Attache Case, every scope and stock, and the Treasure Map are bought from the Merchant.
- I excluded pesetas, ammo, herbs, grenades and files. The Alert Order and other files are optional and were left out.
- I excluded random enemy drops. I kept some fixed enemy drops and marked them `boss_drop`:
  - Bella Sisters, Mendez, Verdugo, Krauser, Iron Maiden
  - Leader Zealots
  - the Novistador nest eyes
  - 8 Spinels from Castle Ganados in the Hall of Rites. Evil Resource calls these fixed. You may want to drop them.
- The combined items (Hexagonal Emblem, Blue Moonstone, Salazar Family Crown, etc.) are crafted, not placed, so they are not entries. Each `unlocks` field says what the halves combine into.

## Main uncertainties
1. **Ruby from the Village Square Dr. Salvador (1-1):** Evil Resource's Ruby page says he drops a Ruby. StrategyWiki 1-1 and Evil Resource's Pesetas (large) page both say 10,000 PTAS. Marked low. The Ruby from the Dr. Salvador on the 1-3 south path is confirmed by both sources.
2. **Red Gem in a camp barrel (2-3):** StrategyWiki and Fandom list it. Evil Resource doesn't list it at all. It may be a random barrel drop. Marked low.
3. **Gem placement depends on version.** On PC/UHD the Audience Hall has the Purple Gem (3-1) and the Main Gallery Hallway statue has the Green Gem (4-1). GameCube has them the other way round. I recorded the PC/UHD placement.
4. **Chapter tags that may be off:**
   - The Village night Tower and Town Hall lantern Spinels: Evil Resource says 2-1, StrategyWiki says 2-2. I used 2-2.
   - The Instruction Hall Spinel: Evil Resource's area page says the items are available in 4-1. I put it in 3-3.
   - The Outside Fountain treasures: 3-2 is inferred.
   - Broken Butterfly and Elegant Perfume Bottle: Fandom says 3-1. Evil Resource, StrategyWiki and SuperCheats all say you get them after the Ashley segment, which is 4-1 backtracking. I used 4-1.
5. **Room ids I'm least sure of** (`room_confidence` low or medium):
   - The rooms.txt labels don't match ModDB's list for r21a and r221. rooms.txt calls r221 "Novistador Boss" while ModDB says it's the Verdugo area. ModDB calls r21A "Underground Mines". I mapped r21a to the Sand Ruins Dwelling and Treasure Chamber.
   - Castle connecting corridors: Connecting Passage, Dragon Hall Access, the Merchant Room by the Tower.
   - Island rooms: the Stairway Passage Brass Pocket Watch, the Green Stone of Judgement (r315 or r312) and the stairwell Emerald in 5-3.
   - Small sub-areas such as Shed 3, Homestead Ruins Shed and the Town Hall back room, which could be in r101 or r102.
   - Check these against the game's actual item tables (ITM/ETS data per room) if you can.
6. **Crow Spinel (1-1):** this is an enemy drop, but Evil Resource lists it as fixed.
7. **Novistador nest eyes in the Ballroom:** up to 8 eyes in random colours. This is one entry here.
8. **Professional mode** wasn't checked separately. Evil Resource lists the same placements for nearly everything.

## Progression summary (key item -> gate)
- Emblem L+R (1-2, r104) -> Valley blue gate -> r107 -> Chief's Manor r105
- Insignia Key (r105) -> Village metal-door house (r101) -> Underground Passage r102 -> Church area r108
- Round Insignia (2-1, r10c) -> Church door -> Ashley (r117)
- Camp Key (r11d) OR Old Key (r11e) -> Gondola area (r10f)
- False Eye (Mendez, r11f) -> gate to the Castle
- Platinum, then Golden Sword (r207) -> Barracks exit
- Castle Gate Key (r203) -> Audience Hall r201
- Prison Key (r201) -> Prison -> Water Hall r208
- Gallery Key (Zealot, r209) -> Gallery -> Goat Ornament
- Moonstone L+R (r20b) -> Courtyard Lounge
- Stone Tablet -> Salazar Family Insignia -> Serpent Ornament (3-4, Ashley)
- Lion Ornament (r222) + Goat + Serpent -> Audience Hall relief (r201) -> Trolley Two
- King's Grail (r216) + Queen's Grail (r212) -> Main Gallery statues (r211) -> Ballroom
- Dynamite (r223) -> boulder -> El Gigante arena r224
- Key to the Mine (4-3) -> sand mine minecart
- Stone of Sacrifice -> lift to the Cathedral
- Freezer Card Key (r307) -> Freezer (r308) -> reprogram into Waste Disposal Card Key -> r30b
- Storage Room Card Key (Iron Maiden, r309) -> Ashley's room r30c
- Holy Beast Panther + Eagle + Serpent (r31c) -> fortress exit
- Emergency Lock Card Key (r327) -> emergency lock -> exit door
- Jet-ski Key (Ada, Final) -> jet ski escape (r333)
