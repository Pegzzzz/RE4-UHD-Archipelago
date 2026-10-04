// Simulated game state for the Archipelago module harness (plain types only, no SDK headers)
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace sim
{
	// SS_OPEN_FLAG values
	enum : uint32_t { SS_NULL = 0, SS_NORMAL = 1, SS_PZZL = 4, SS_SHOP = 0x10, SS_ITEM = 0x80, SS_CAP = 0x100 };
	// Flags_STATUS indices
	enum : int { STA_MOVIE_ON = 3, STA_DIEDEMO = 11, STA_SUB_SCRN = 13, STA_SHOOTING = 14, STA_EVENT = 19,
		STA_ITEM_GET = 62, STA_SSCRN_REQUEST = 69, STA_INTO_SHOP = 77, STA_NOW_LOADING = 114 };
	enum : int { R_INIT = 0, R_STAGEINIT = 1, R_ROOMINIT = 2, R_MAINLOOP = 3 };
	enum : int { PL_LEON = 0, PL_ASHLEY = 1 };

	void reset(uint16_t room);           // new game: zeroed GLOBAL_WK, Leon, MainLoop, Handgun + ammo
	void setRoom(uint16_t room);
	uint16_t room();
	void setRoutine(int r);
	void setHp(int hp);
	int hp();
	void setPlType(int t);
	void setGold(int g);
	int gold();
	void setBoardSize(int s);
	int boardSize();
	void setOpenFlag(uint32_t f);
	uint32_t openFlag();
	void setItemGetFlag(bool b);
	void setStatus(int idx, bool on);
	void setChar(uint8_t c);              // ItemMgr->m_char_13
	void setCaseFull(bool full);          // PutInCase always fails
	void setDeferredSubScreenOpen(bool d);// SubScreenOpen takes effect on the next game frame
	uint16_t getItemId();
	void setGetItem(uint16_t id, uint16_t num);
	void setRoomItemFlag(int bit);            // the game marks a placed item in the current room as taken                 // SubScreenWk->get_item_id_2F6

	// Inventory
	void gameAdd(uint16_t id, int num, int chr = 0);   // game-side add (stacks like cItemMgr::get)
	int count(uint16_t id, int chr = -1);             // -1: current char
	void gameRemoveAll(uint16_t id);
	void equip(uint16_t id);
	void transferChar(uint16_t id, int fromChr, int toChr);
	void placePending();                  // player places the item waiting in the organize screen; screen closes
	void discardPending();                // player leaves the waiting item behind; screen closes

	// Save work
	uint32_t saveWork(int i);
	void setSaveWork(int i, uint32_t v);

	// Ems
	void setEm(int idx, uint8_t id, int16_t hp, uint32_t guid, bool valid);

	// Snapshots (GLOBAL_WK + item array + item manager + sub screen)
	struct State { std::vector<uint8_t> g, items, mgr, ss; };
	State save();
	void restore(const State& s);

	void gameFrame();                     // per-frame game-side processing (deferred screen opens)

	std::vector<std::string>& conLines();     // [AP] console lines (module Log())
	std::vector<std::string>& renderLines();  // ImGui text drawn by Archipelago_Render this frame
	int sndCalls();
}
