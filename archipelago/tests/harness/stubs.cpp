#include <map>
// Stub definitions for every symbol Archipelago.cpp links against, backed by a simulated game state.
#include <winsock2.h>
#include "dllmain.h"
#include "Game.h"
#include "Patches.h"
#include "ConsoleWnd.h"
#include <imgui.h>
#include <cstring>
#include <cstdarg>
#include "sim.h"

// ---------------------------------------------------------------- globals the module references
std::wstring rootPath = L"";
std::wstring logPath = L"spd.log";
ConsoleOutput con;

const char* EItemId_Names[] = {
#include "item_names.inc"
};

namespace
{
	constexpr int kMaxItems = 64;
	alignas(16) uint8_t gBuf[sizeof(GLOBAL_WK)];
	alignas(16) uint8_t ssBuf[sizeof(SUB_SCREEN)];
	alignas(16) uint8_t mgrBuf[sizeof(cItemMgr)];
	alignas(16) uint8_t plBuf[sizeof(cPlayer)];
	alignas(16) uint8_t emMgrBuf[sizeof(cEmMgr)];
	constexpr int kEms = 16;
	alignas(16) uint8_t emBuf[sizeof(cEm) * kEms];
	cItem items[kMaxItems + 2]; // ForEachItem visits items[1..kMaxItems]

	bool caseFull = false;
	bool deferredOpen = false;
	uint32_t pendingOpenFlag = 0;
	int sndCount = 0;
	std::vector<std::string> gCon, gRender;

	GLOBAL_WK* G() { return reinterpret_cast<GLOBAL_WK*>(gBuf); }
	SUB_SCREEN* SS() { return reinterpret_cast<SUB_SCREEN*>(ssBuf); }
	cItemMgr* MGR() { return reinterpret_cast<cItemMgr*>(mgrBuf); }
	cEmMgr* EMGR() { return reinterpret_cast<cEmMgr*>(emMgrBuf); }
	cEm* EM(int i) { return reinterpret_cast<cEm*>(emBuf + i * sizeof(cEm)); }

	ITEM_INFO Info(uint16_t id)
	{
		ITEM_INFO i{};
		i.id_0 = id;
		auto set = [&](ITEM_TYPE_mb t, uint8_t def, uint16_t max) { i.type_2 = t; i.defNum_3 = def; i.maxNum_4 = max; };
		switch (id)
		{
		case 0: set(ITEM_TYPE_AMMO, 3, 30); break;      // magnum
		case 4: set(ITEM_TYPE_AMMO, 10, 100); break;    // handgun ammo
		case 7: set(ITEM_TYPE_AMMO, 5, 30); break;
		case 24: set(ITEM_TYPE_AMMO, 6, 30); break;
		case 32: set(ITEM_TYPE_AMMO, 50, 250); break;
		case 1: case 2: case 14: set(ITEM_TYPE_GRENADE, 1, 1); break;
		case 5: case 6: case 8: case 9: case 10: case 18: case 19: case 20: case 21: case 22: case 25: case 28: case 168:
			set(ITEM_TYPE_CONSUMABLE, 1, 1); break;
		case 124: case 125: case 126: case 127: case 84: case 85: case 169: set(ITEM_TYPE_TREASURE_MAP, 1, 1); break;
		case 66: case 67: case 68: case 69: case 70: case 170: set(ITEM_TYPE_WEAPON_MOD, 1, 1); break;
		case 86: case 87: case 161: set(ITEM_TYPE_TREASURE, 1, 99); break;
		case 95: case 96: case 97: case 185: case 186: case 187: case 199: case 200: case 201: case 210: case 211: case 212:
			set(ITEM_TYPE_TREASURE_GEM, 1, 1); break;
		case 254: set(ITEM_TYPE_UNK4, 1, 1); break;
		default:
			if ((id >= 33 && id <= 55) || id == 148 || id == 3 || id == 62 || id == 65)
				set(ITEM_TYPE_WEAPON, 0, 1);
			else if (id >= 220 && id <= 243)
				set(ITEM_TYPE_BOTTLECAP, 1, 1);
			else if ((id >= 72 && id <= 80) || (id >= 172 && id <= 183) || (id >= 244 && id <= 253))
				set(ITEM_TYPE_FILE, 1, 1);
			else if (id >= 88 && id <= 104 || id == 119 || (id >= 143 && id <= 159) || (id >= 184 && id <= 219))
				set(ITEM_TYPE_TREASURE, 1, 1);
			else
				set(ITEM_TYPE_KEY_ITEM, 1, 1);
		}
		return i;
	}

	cItem* FreeSlot()
	{
		for (int i = 1; i <= kMaxItems; i++)
			if (!(items[i].be_flag_4 & 1))
				return &items[i];
		return nullptr;
	}

	// cItemMgr::get semantics: stack onto an existing entry if stackable, else new entry
	bool MgrGet(uint16_t id, uint32_t num, uint8_t chr)
	{
		ITEM_INFO info = Info(id);
		if (info.maxNum_4 > 1)
		{
			for (int i = 1; i <= kMaxItems && num; i++)
			{
				cItem& it = items[i];
				if ((it.be_flag_4 & 1) && it.chr_5 == chr && it.id_0 == id && it.num_2 < info.maxNum_4)
				{
					uint32_t room = info.maxNum_4 - it.num_2;
					uint32_t add = num < room ? num : room;
					it.num_2 += uint16_t(add);
					num -= add;
				}
			}
		}
		while (num)
		{
			cItem* it = FreeSlot();
			if (!it)
				return false;
			memset(it, 0, sizeof(cItem));
			it->id_0 = id;
			it->be_flag_4 = 1;
			it->chr_5 = chr;
			if (info.maxNum_4 > 1)
			{
				uint32_t add = num < info.maxNum_4 ? num : info.maxNum_4;
				it->num_2 = uint16_t(add);
				num -= add;
			}
			else
			{
				it->num_2 = 1;
				num--;
			}
		}
		return true;
	}

	bool PutInCase(uint16_t id, uint32_t num)
	{
		if (caseFull)
			return false;
		return MgrGet(id, num, MGR()->m_char_13);
	}

	void OpenSubScreen(uint32_t f)
	{
		if (deferredOpen)
			pendingOpenFlag |= f;
		else
			SS()->open_flag_2C = SS_OPEN_FLAG(SS()->open_flag_2C | f);
	}

	// ---- function-pointer targets
	void __fastcall EraseStub(cItemMgr* mgr, void*, cItem* p)
	{
		if (mgr->m_pWep_C == p)
			mgr->m_pWep_C = nullptr;
		memset(p, 0, sizeof(cItem));
	}
	cItem* __fastcall SearchStub(cItemMgr* mgr, void*, ITEM_ID id)
	{
		for (int i = 1; i <= kMaxItems; i++)
			if ((items[i].be_flag_4 & 1) && items[i].chr_5 == mgr->m_char_13 && items[i].id_0 == id)
				return &items[i];
		return nullptr;
	}
	bool __fastcall GetStub(cItemMgr* mgr, void*, ITEM_ID id, uint16_t num) { return MgrGet(id, num, mgr->m_char_13); }
	BOOL __fastcall SubScrCheckStub(cPlayer*, void*) { return TRUE; }
	void __cdecl ItemInfoStub(ITEM_ID id, ITEM_INFO* info) { *info = Info(id); }
	uint32_t __cdecl SndCallStub(uint16_t, uint16_t, Vec*, uint8_t, uint32_t, cModel*) { sndCount++; return 0; }
	bool __cdecl SubScreenOpenStub(SS_OPEN_FLAG f, SS_ATTR_FLAG) { OpenSubScreen(f); return true; }
}

cItemMgr* ItemMgr = reinterpret_cast<cItemMgr*>(mgrBuf);
SUB_SCREEN* SubScreenWk = reinterpret_cast<SUB_SCREEN*>(ssBuf);

// room save data (placed-item flags), one per room id
static std::map<uint16_t, ROOM_SAVE_DATA> gRoomSaves;
static uint8_t roomDataBuf[sizeof(cRoomData)];
static ROOM_SAVE_DATA* __fastcall FakeGetRoomSave(cRoomData*, void*, uint16_t room)
{
	auto& rs = gRoomSaves[room];
	rs.RoomNo_0 = room;
	return &rs;
}
cRoomData* RoomData = reinterpret_cast<cRoomData*>(roomDataBuf);
cRoomData__getRoomSavePtr_Fn cRoomData__getRoomSavePtr = FakeGetRoomSave;
cItemMgr__erase_Fn cItemMgr__erase = EraseStub;
cItemMgr__search_Fn cItemMgr__search = SearchStub;
cItemMgr__get_Fn cItemMgr__get = GetStub;
cPlayer__subScrCheck_Fn cPlayer__subScrCheck = SubScrCheckStub;
namespace bio4
{
	void(__cdecl* itemInfo)(ITEM_ID id, ITEM_INFO* info) = ItemInfoStub;
	uint32_t(__cdecl* SndCall)(uint16_t blk, uint16_t call_no, Vec* pos, uint8_t id, uint32_t flag, cModel* pMod) = SndCallStub;
	bool(__cdecl* SubScreenOpen)(SS_OPEN_FLAG open_flag, SS_ATTR_FLAG attr_flag) = SubScreenOpenStub;
}

static bool gOptionOpen = false;
GLOBAL_WK* GlobalPtr() { return G(); }
cPlayer* PlayerPtr() { return reinterpret_cast<cPlayer*>(plBuf); }
cEmMgr* EmMgrPtr() { return EMGR(); }
bool OptionOpenFlag() { return gOptionOpen; }

// Mirrors Game.cpp InventoryItemAdd
void InventoryItemAdd(ITEM_ID id, uint32_t count, bool always_show_inv_ui, bool handle_attache_case)
{
	ITEM_INFO info;
	bio4::itemInfo(id, &info);
	bool snd = true;
	if (!bio4::itemShowsInInventory(info.type_2))
	{
		if (!ItemMgr->get(id, count))
			snd = false;
		else if (handle_attache_case && id >= 124 && id <= 127)
			SubScreenWk->board_size_2AA = int8_t(id - 124);
	}
	else if (always_show_inv_ui || !PutInCase(id, count))
	{
		snd = false;
		SubScreenWk->get_item_id_2F6 = id;
		SubScreenWk->get_item_num_2F8 = count;
		bio4::SubScreenOpen(SS_OPEN_PZZL, SS_ATTR_NULL);
	}
	if (snd)
		bio4::SndCall(0, 0x13, 0, 0, 0, 0);
}

// ---------------------------------------------------------------- ImGui stubs
void ImGuiTextBuffer::append(const char* str, const char* str_end)
{
	std::string s = str_end ? std::string(str, str_end) : std::string(str);
	if (s != "\n")
		gCon.push_back(s);
}
ImGuiTextFilter::ImGuiTextFilter(const char*) { InputBuf[0] = 0; CountGrep = 0; }
namespace ImGui
{
	void MemFree(void* p) { free(p); }
	bool Begin(const char*, bool*, ImGuiWindowFlags) { return true; }
	void End() {}
	void SetNextWindowPos(const ImVec2&, ImGuiCond, const ImVec2&) {}
	void SetNextWindowBgAlpha(float) {}
	void TextColored(const ImVec4&, const char* fmt, ...)
	{
		char buf[1024];
		va_list a;
		va_start(a, fmt);
		vsnprintf(buf, sizeof(buf), fmt, a);
		va_end(a);
		gRender.push_back(buf);
	}
}

// ---------------------------------------------------------------- sim API
namespace sim
{
	void setRoomItemFlag(int bit)
	{
		auto& rs = gRoomSaves[GlobalPtr()->curRoomId_4FAC];
		rs.item_flg_8[bit / 32] |= 0x80000000u >> (bit % 32);
	}
	int nextRoomItemBit()
	{
		auto& rs = gRoomSaves[GlobalPtr()->curRoomId_4FAC];
		for (int b = 0; b < 128; b++)
			if (!(rs.item_flg_8[b / 32] & (0x80000000u >> (b % 32))))
				return b;
		return -1;
	}

	void reset(uint16_t room)
	{
		gRoomSaves.clear();
		memset(gBuf, 0, sizeof(gBuf));
		memset(ssBuf, 0, sizeof(ssBuf));
		memset(mgrBuf, 0, sizeof(mgrBuf));
		memset(plBuf, 0, sizeof(plBuf));
		memset(emMgrBuf, 0, sizeof(emMgrBuf));
		memset(emBuf, 0, sizeof(emBuf));
		memset(items, 0, sizeof(items));
		caseFull = false;
		deferredOpen = false;
		pendingOpenFlag = 0;
		gOptionOpen = false;

		GLOBAL_WK* g = G();
		g->Rno0_20 = uint8_t(GLOBAL_WK::Routine0::MainLoop);
		g->curRoomId_4FAC = room;
		g->playerHpCur_4FB4 = 1200;
		g->pl_type_4FC8 = PlayerCharacter::Leon;
		g->goldAmount_4FA8 = 0;

		cItemMgr* m = MGR();
		m->m_pItem_14 = items;
		m->m_array_num_1C = kMaxItems - 1;
		m->m_char_13 = 0;

		EMGR()->m_Array_4 = EM(0);
		EMGR()->m_nArray_8 = kEms;
		EMGR()->m_blockSize_C = sizeof(cEm);

		MgrGet(35, 1, 0); // Handgun
		MgrGet(4, 20, 0);
		equip(35);
	}
	void setRoom(uint16_t r) { G()->curRoomId_4FAC = r; }
	uint16_t room() { return G()->curRoomId_4FAC; }
	void setRoutine(int r) { G()->Rno0_20 = uint8_t(r); }
	void setHp(int hp) { G()->playerHpCur_4FB4 = int16_t(hp); }
	int hp() { return G()->playerHpCur_4FB4; }
	void setPlType(int t) { G()->pl_type_4FC8 = PlayerCharacter(t); }
	void setGold(int v) { G()->goldAmount_4FA8 = v; }
	int gold() { return G()->goldAmount_4FA8; }
	void setBoardSize(int s) { SS()->board_size_2AA = int8_t(s); }
	int boardSize() { return SS()->board_size_2AA; }
	void setOpenFlag(uint32_t f) { SS()->open_flag_2C = SS_OPEN_FLAG(f); }
	uint32_t openFlag() { return SS()->open_flag_2C; }
	void setItemGetFlag(bool b) { SS()->item_get_flag_40 = b; }
	void setStatus(int idx, bool on) { FlagSet(G()->flags_STATUS_0_501C, uint32_t(idx), on); }
	void setChar(uint8_t c) { MGR()->m_char_13 = c; }
	void setCaseFull(bool f) { caseFull = f; }
	void setDeferredSubScreenOpen(bool d) { deferredOpen = d; }
	uint16_t getItemId() { return SS()->get_item_id_2F6; }
	void setGetItem(uint16_t id, uint16_t num) { SS()->get_item_id_2F6 = id; SS()->get_item_num_2F8 = num; }

	void gameAdd(uint16_t id, int num, int chr) { MgrGet(id, uint32_t(num), uint8_t(chr)); }
	int count(uint16_t id, int chr)
	{
		uint8_t c = chr < 0 ? MGR()->m_char_13 : uint8_t(chr);
		int n = 0;
		for (int i = 1; i <= kMaxItems; i++)
			if ((items[i].be_flag_4 & 1) && items[i].chr_5 == c && items[i].id_0 == id)
				n += items[i].num_2 ? items[i].num_2 : 1;
		return n;
	}
	void gameRemoveAll(uint16_t id)
	{
		for (int i = 1; i <= kMaxItems; i++)
			if ((items[i].be_flag_4 & 1) && items[i].id_0 == id)
				EraseStub(MGR(), nullptr, &items[i]);
	}
	void equip(uint16_t id) { MGR()->m_pWep_C = SearchStub(MGR(), nullptr, id); }
	void transferChar(uint16_t id, int fromChr, int toChr)
	{
		for (int i = 1; i <= kMaxItems; i++)
			if ((items[i].be_flag_4 & 1) && items[i].chr_5 == fromChr && items[i].id_0 == id)
				items[i].chr_5 = uint8_t(toChr);
	}
	void placePending()
	{
		MgrGet(SS()->get_item_id_2F6, SS()->get_item_num_2F8 ? SS()->get_item_num_2F8 : 1, MGR()->m_char_13);
		SS()->get_item_id_2F6 = 0;
		SS()->get_item_num_2F8 = 0;
		SS()->open_flag_2C = SS_OPEN_NULL;
	}
	void discardPending()
	{
		SS()->get_item_id_2F6 = 0;
		SS()->get_item_num_2F8 = 0;
		SS()->open_flag_2C = SS_OPEN_NULL;
	}

	uint32_t saveWork(int i) { return G()->save_free_work_5310[i]; }
	void setSaveWork(int i, uint32_t v) { G()->save_free_work_5310[i] = v; }

	void setEm(int idx, uint8_t id, int16_t hp, uint32_t guid, bool valid)
	{
		cEm* e = EM(idx);
		e->be_flag_4 = valid ? 1 : 0;
		e->id_100 = id;
		e->hp_324 = hp;
		e->guid_F8 = guid;
	}

	State save()
	{
		State s;
		s.g.assign(gBuf, gBuf + sizeof(gBuf));
		s.items.assign((uint8_t*)items, (uint8_t*)items + sizeof(items));
		s.mgr.assign(mgrBuf, mgrBuf + sizeof(mgrBuf));
		s.ss.assign(ssBuf, ssBuf + sizeof(ssBuf));
		for (auto& [room, rs] : gRoomSaves)
			s.rooms.insert(s.rooms.end(), (uint8_t*)&rs, (uint8_t*)&rs + sizeof(rs));
		return s;
	}
	void restore(const State& s)
	{
		memcpy(gBuf, s.g.data(), s.g.size());
		memcpy(items, s.items.data(), s.items.size());
		memcpy(mgrBuf, s.mgr.data(), s.mgr.size());
		memcpy(ssBuf, s.ss.data(), s.ss.size());
		gRoomSaves.clear(); // room save data rolls back with the save, like in the game
		for (size_t i = 0; i + sizeof(ROOM_SAVE_DATA) <= s.rooms.size(); i += sizeof(ROOM_SAVE_DATA))
		{
			ROOM_SAVE_DATA rs;
			memcpy(&rs, s.rooms.data() + i, sizeof(rs));
			gRoomSaves[rs.RoomNo_0] = rs;
		}
	}

	void gameFrame()
	{
		if (pendingOpenFlag)
		{
			SS()->open_flag_2C = SS_OPEN_FLAG(SS()->open_flag_2C | pendingOpenFlag);
			pendingOpenFlag = 0;
		}
		gRender.clear();
	}

	std::vector<std::string>& conLines() { return gCon; }
	std::vector<std::string>& renderLines() { return gRender; }
	int sndCalls() { return sndCount; }
}
