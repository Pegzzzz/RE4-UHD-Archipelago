# RE4 (2005) structure research: notes, uncertainties and sources

## Access caveats
- I couldn't reach residentevil.fandom.com (most pages returned 402), GameFAQs, Wikipedia or GitHub search. curl is blocked by the proxy for all of these sites, so every fetch went through WebFetch.
- Main sources:
  - evilresource.com, used for prices, drops and per-location contents.
  - StrategyWiki chapter pages, used for chapter flow, points of no return and the shooting gallery.
  - ModDB "Resident Evil 4 Area's List" and residentevilmodding.boards.net, used for room-id descriptions.
  - A Steam guide and TrueAchievements, used for bottle caps.
- WebFetch summarizes pages, so the quotes are close paraphrases. Check the exact numbers again before relying on them.

## Room-id mapping (biggest risk)
- The rooms.txt labels (they match the strings in roomInfo.dat) are community jump-menu labels. Several of them are misleading:
  - r104 "Quarry": ModDB says this is the room where Leon wakes up with Luis. evilresource puts that in the Valley Storage Building, so r104 is most likely the Valley, home of the first merchant (Valley Storage Yard, 1-2).
  - r109/r119 "Colosseum": the Quarry clearing where El Gigante appears in 2-1 (ModDB: "El Gigante location").
  - r107 "Water Processing": the Sewage Treatment Plant (ModDB: "zone after using hexagon pieces").
  - r10e "Canal - Merchant": inferred to be the underground dock / Quarry Merchant Hideout. ModDB calls it the "El Gigante save point", and the hideout connects to the Quarry and by boat to the Waterfall Cave Dock. This is not directly confirmed.
  - r221 "Novistador Boss": the Verdugo arena (ModDB). r229 "Spike Pit" is the Pit before Verdugo, and r220 is the Mine Entrance merchant.
- Castle Bailey 1/2 has the weakest mapping. ModDB's descriptions don't match the rooms.txt labels well (r204, r209, r20a, r20c, r206). I couldn't pin down room ids for these:
  - Hall of Rites (first shooting gallery)
  - Display Room (Garrador cage)
  - Connecting Passage (first Attache Case XL merchant)
  - Royal suite (second shooting gallery)
  - Instruction Hall
- **Recommendation:** check room ids in-game with the re4_tweaks Area Jump / debug "current room" display. re4_tweaks also exposes `globals->chapter_4F9A`, so chapter boundaries can be logged at runtime.

## Chapter structure
- The chapter order and end events are reliable (StrategyWiki plus fandom summaries). The per-chapter room lists are approximate.
- Points of no return, as confirmed by sources:
  - Bridge at the start (1-1).
  - Church cutscene in 2-1: it closes off the lake, swamp and holding area.
  - Boarded gates at night (2-2).
  - Raising the bridge at the end of 2-3 ends the Village.
  - Drawbridge to the village closes in 3-1.
  - Clock tower lever in 4-1: no return to the rest of the castle.
  - Locked door in 4-3.
  - Entering the tower in 4-4.
  - Ancient Palace in 5-3: last shooting gallery is lost.
  - Service elevator in 5-2.
  - Elevator to the docks in the Final Chapter.
- Ashley:
  - Playable only in 3-4: Servants' quarters/Study (r20d) and the armor storeroom (r20e). ModDB calls these "Ashley crawling area 1/2".
  - She escorts Leon from the end of 2-1 to the end of 3-1, from 3-4/4-1 to r213 (Novistador nest), and from 5-1's end (r30c) to r316.
  - In 5-2 she drives the bulldozer, but the player still controls Leon.
- Fandom ends 3-3 at the merchant, while StrategyWiki ends it at Luis' death (r206). r206 is "Luis death scene" in ModDB.

## Bosses
- No reward source found:
  - Del Lago and Verdugo pesetas (Verdugo drops the Crown Jewel, per evilresource).
  - Bella Sisters.
  - Krauser's knife fight (a QTE).
- Not certain which of these are mandatory:
  - Prison Garrador in 3-1.
  - The second Melting Furnace El Gigante in 4-2. StrategyWiki suggests killing one is enough.
  - The Iron Maidens in r310.
- El Gigante in 2-3 (Gorge/right path) and the Bella Sisters (left path) are alternative routes, so both are optional, and the player can do both.

## Requests
- RE4 (2005) has **one** Merchant request: the 15 blue medallions (Farm 7 / Cemetery 8, according to the in-game note).
  - Reward: 10 or more gets a free Punisher; all 15 adds Firepower Lv.2.
  - Requests like "Catch the big fish" and "Find the rat", extra medallion sets, and spinel rewards all come from the 2023 remake.
- evilresource counts by sub-location (Farm Yard 5, Farm Exit 2, Graveyard 3, Church Courtyard 1, Church Backyard 1, Wooden Walkway 3) and says 15 in total. One Wooden Walkway page says "4". I'm trusting the in-game note's 7/8 split.
- Medallion deadline: StrategyWiki says you claim the Punisher at the 1-3 underground-dock merchant. The medallions probably disappear after the church cutscene in 2-1, but I haven't verified that.

## Shooting gallery
- Five venues: 3-1, 4-1, 4-2, 5-1 and 5-3. Games unlock in this order: A (3-1), B (4-1), C (4-2), D (5-1).
- Bottle caps: 24 in total, 6 per game.
  - Normal cap: 3,000+ points.
  - Rare cap: 4,000+ points, or every target hit without hitting Ashley.
  - Cash for completing a row: A 15,000, B 25,000, C 35,000, D 50,000 (StrategyWiki).
- The in-game "Target Practice" file text says 1,000 / 3,500 instead. That file may describe a different system or a version difference; this conflict is unresolved.
- Two steps run on the r22c minigame room: turning guns in to the merchant, and the achievement.

## Merchant stock
- Prices and first-available chapters come from the individual evilresource item pages (high confidence).
- Red9: evilresource says 2-1 at the Underground Passage, while fandom mentions buying it in the 2-2 caves. Both are the r102/r112 underground passage.
- Not found:
  - Matilda stock price.
  - Starting attache case size.
  - Upgrade-tier unlock chapters (StrategyWiki/evilresource have per-weapon upgrade tables).
  - Sell prices, apart from a few.
- The Infrared Scope isn't sold; it's found in the Freezer (r308).
- Post-game-only items: Matilda, Chicago Typewriter, Infinite Launcher, Handcannon.

## Sources
- https://www.evilresource.com/resident-evil-4 (weaponry/*, equipment/*, items/*, treasures/pesetas-large, treasures/pesetas-medium, miscellaneous-objects/*, enemies/*, files/*, maps/*-contents)
- https://strategywiki.org/wiki/Resident_Evil_4/Chapter_1-1 … Chapter_5-4, /Final_chapter, /Side_quests
- https://residentevil.fandom.com/wiki/Chapter_X-Y_(Resident_Evil_4) (only the 1-1, 1-3, 2-1, 2-2, 2-3, 3-2, 3-3 and 3-4 pages could be read)
- https://www.moddb.com/games/resident-evil-4/tutorials/resident-evil-4-areas-list
- https://residentevilmodding.boards.net/thread/6661/list-areas-game-files
- https://steamcommunity.com/sharedfiles/filedetails/?id=287562785
- https://www.trueachievements.com/a154429/what-are-they-worth-achievement
- https://github.com/nipkownix/re4_tweaks (SDK/room_jmp.h, SDK/merchant.h): roomInfo.dat struct and merchant data structs
