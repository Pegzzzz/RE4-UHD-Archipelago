// Archipelago multiworld module for RE4 UHD (re4_tweaks fork)
//
// Overview
//  - A background thread hosts a TCP server on 127.0.0.1:46400. The Archipelago client
//    ("Resident Evil 4 UHD Client") connects to it and exchanges newline-delimited JSON.
//  - Everything that touches game memory runs on the main game thread, in Archipelago_Tick(),
//    which is called from the cSceSys::scheduler hook once per frame.
//  - Checks are found by diffing Leon's item list every frame and looking at what the game was
//    doing when an item appeared (item pickup screen, Merchant, shooting gallery), plus boss HP.
//  - The vanilla item from a shuffled location is removed again; Archipelago delivers whatever the
//    location really holds through the normal received-items stream.
//  - The index of the next item to apply is stored inside the game's own save work
//    (GLOBAL_WK::save_free_work), so dying/continuing or loading an older save re-applies items correctly.

#include <winsock2.h>
#include <ws2tcpip.h>

#include "dllmain.h"
#include "Game.h"
#include "Patches.h"
#include "ConsoleWnd.h"
#include "Archipelago.h"

#include <nlohmann/json.hpp>
#include <imgui.h>

#include <atomic>
#include <chrono>
#include <deque>
#include <fstream>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#pragma comment(lib, "ws2_32.lib")

using json = nlohmann::json;

namespace ap
{
	constexpr uint16_t kPort = 46400;
	constexpr int kProtocolVersion = 1;

	// save_free_work slots we use (GLOBAL_WK::save_free_work_5310[64], stored inside the game save)
	constexpr uint32_t kSaveMagic = 0x52345041; // 'AP4R'
	constexpr int kSlotMagic = 60;
	constexpr int kSlotTag = 61;
	constexpr int kSlotIndex = 62;

	constexpr int kPickupWindowFrames = 120; // item may be added shortly after the pickup screen flag drops
	constexpr int kShopWindowFrames = 10;
	constexpr int kGrantCooldownFrames = 20;

	enum class Kind { Pickup, Boss, Merchant, MedallionReward, Medallion, BottleCap, Unknown };

	struct LocationDef
	{
		int64_t id = 0;
		Kind kind = Kind::Unknown;
		int room = -1;
		int em = -1;
		std::vector<int> items;
		bool loose = false; // room id not verified: allow a match anywhere in the same stage
		bool cut = false;   // given by a cutscene/puzzle rather than the pickup screen
	};

	struct ItemDef
	{
		std::string kind;
		int game = -1;
		int amount = 0;
	};

	struct ReceivedItem
	{
		int64_t id = 0;
		std::string name;
		std::string from;
	};

	struct Toast
	{
		std::string text;
		std::chrono::steady_clock::time_point time;
	};

	// ---------------- network state (shared) -------------------------------------------------
	std::mutex sockMutex;
	SOCKET clientSock = INVALID_SOCKET;
	std::mutex inboxMutex;
	std::deque<json> inbox;
	std::atomic<bool> clientConnected{ false };
	std::atomic<bool> clientJustConnected{ false };

	// ---------------- UI state (shared with render thread) ----------------------------------
	std::mutex toastMutex;
	std::deque<Toast> toasts;
	std::atomic<int> uiStatus{ 0 }; // 0 = waiting for client, 1 = connected/no config, 2 = ready, 3 = save mismatch

	// ---------------- main-thread state -------------------------------------------------------
	bool configured = false;
	bool deathLink = false;
	uint32_t saveTag = 0;
	std::vector<LocationDef> locations;
	std::unordered_map<int64_t, ItemDef> itemDefs;
	std::vector<ReceivedItem> received;
	std::unordered_set<int64_t> serverChecked;
	std::unordered_set<int64_t> localChecked;
	std::vector<int64_t> pendingChecks;
	std::unordered_set<int> bossEmIds;

	uint64_t frame = 0;
	bool resetSnapshot = true;
	std::unordered_map<uint16_t, uint32_t> prevInv;
	int prevGold = 0;
	int prevCaseSize = -1;
	uint16_t prevRoom = 0xFFFF;
	uint64_t lastPickupCtxFrame = 0;
	uint64_t lastShopCtxFrame = 0;
	uint64_t lastGrantFrame = 0;
	uint64_t lastKillFrame = 0;
	bool wasDead = false;
	bool pendingKill = false;
	bool goalSent = false;
	bool saveBound = false;

	struct Removal { uint16_t id; uint32_t count; };
	std::vector<Removal> pendingRemovals;
	std::unordered_map<uint16_t, int> expectedGrants; // items we added that may show up a bit later (case-full UI)

	struct TrackedEm { uint32_t guid; uint8_t id; };
	std::unordered_map<uint32_t, TrackedEm> trackedBosses; // key: index in EmMgr

	std::filesystem::path logFile;

	// =============================================================================== logging
	void Log(const std::string& text)
	{
		con.log("[AP] %s", text.c_str());
		try
		{
			std::ofstream f(logFile, std::ios::app);
			auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
			char buf[32];
			std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
			f << buf << "  " << text << "\n";
		}
		catch (...) {}
	}

	void AddToast(const std::string& text)
	{
		std::lock_guard<std::mutex> lock(toastMutex);
		toasts.push_back({ text, std::chrono::steady_clock::now() });
		while (toasts.size() > 8)
			toasts.pop_front();
	}

	// =============================================================================== network
	void Send(const json& msg)
	{
		std::string line = msg.dump() + "\n";
		std::lock_guard<std::mutex> lock(sockMutex);
		if (clientSock == INVALID_SOCKET)
			return;
		const char* data = line.data();
		int left = int(line.size());
		while (left > 0)
		{
			int sent = send(clientSock, data, left, 0);
			if (sent <= 0)
				break;
			data += sent;
			left -= sent;
		}
	}

	void SendLog(const std::string& text)
	{
		Log(text);
		Send({ {"cmd", "log"}, {"text", text} });
	}

	void NetThread()
	{
		WSADATA wsa;
		if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
		{
			Log("WSAStartup failed");
			return;
		}

		SOCKET listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (listenSock == INVALID_SOCKET)
		{
			Log("socket() failed");
			return;
		}

		sockaddr_in addr = {};
		addr.sin_family = AF_INET;
		addr.sin_port = htons(kPort);
		inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

		if (bind(listenSock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR || listen(listenSock, 1) == SOCKET_ERROR)
		{
			Log("Could not listen on 127.0.0.1:46400 (is another copy of the game running?)");
			closesocket(listenSock);
			return;
		}
		Log("Listening for the Archipelago client on 127.0.0.1:46400");

		while (true)
		{
			SOCKET s = accept(listenSock, nullptr, nullptr);
			if (s == INVALID_SOCKET)
			{
				Sleep(500);
				continue;
			}

			BOOL nodelay = TRUE;
			setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char*)&nodelay, sizeof(nodelay));

			{
				std::lock_guard<std::mutex> lock(sockMutex);
				if (clientSock != INVALID_SOCKET)
					closesocket(clientSock);
				clientSock = s;
			}
			clientConnected = true;
			clientJustConnected = true;
			uiStatus = 1;
			Log("Archipelago client connected");

			std::string buffer;
			char chunk[4096];
			while (true)
			{
				int n = recv(s, chunk, sizeof(chunk), 0);
				if (n <= 0)
					break;
				buffer.append(chunk, n);
				size_t pos;
				while ((pos = buffer.find('\n')) != std::string::npos)
				{
					std::string line = buffer.substr(0, pos);
					buffer.erase(0, pos + 1);
					if (line.empty())
						continue;
					try
					{
						json msg = json::parse(line);
						std::lock_guard<std::mutex> lock(inboxMutex);
						inbox.push_back(std::move(msg));
					}
					catch (...)
					{
						Log("Ignored malformed message from client");
					}
				}
			}

			{
				std::lock_guard<std::mutex> lock(sockMutex);
				if (clientSock == s)
				{
					closesocket(clientSock);
					clientSock = INVALID_SOCKET;
				}
			}
			clientConnected = false;
			uiStatus = 0;
			Log("Archipelago client disconnected");
		}
	}

	// =============================================================================== game helpers
	bool Status(Flags_STATUS flag)
	{
		GLOBAL_WK* g = GlobalPtr();
		return g && FlagIsSet(g->flags_STATUS_0_501C, uint32_t(flag));
	}

	bool InMainLoop()
	{
		GLOBAL_WK* g = GlobalPtr();
		return g && ItemMgr && SubScreenWk && g->Rno0_20 == uint8_t(GLOBAL_WK::Routine0::MainLoop);
	}

	bool IsLeon()
	{
		GLOBAL_WK* g = GlobalPtr();
		return g && (g->pl_type_4FC8 == PlayerCharacter::Leon || g->pl_type_4FC8 == PlayerCharacter::LeonAshley);
	}

	bool PickupContext()
	{
		return (SubScreenWk->open_flag_2C & SS_OPEN_ITEM) != 0 || SubScreenWk->item_get_flag_40 || Status(Flags_STATUS::STA_ITEM_GET);
	}

	bool ShopContext()
	{
		return (SubScreenWk->open_flag_2C & SS_OPEN_SHOP) != 0 || Status(Flags_STATUS::STA_INTO_SHOP);
	}

	// Safe moment to change Leon's inventory: normal gameplay, no menus, no cutscenes
	bool SafeForInventory()
	{
		GLOBAL_WK* g = GlobalPtr();
		if (!InMainLoop() || !IsLeon())
			return false;
		if (SubScreenWk->open_flag_2C != SS_OPEN_NULL || SubScreenWk->item_get_flag_40)
			return false;
		if (g->playerHpCur_4FB4 <= 0)
			return false;
		return !Status(Flags_STATUS::STA_ITEM_GET) && !Status(Flags_STATUS::STA_INTO_SHOP) &&
			!Status(Flags_STATUS::STA_SUB_SCRN) && !Status(Flags_STATUS::STA_SSCRN_REQUEST) &&
			!Status(Flags_STATUS::STA_EVENT) && !Status(Flags_STATUS::STA_MOVIE_ON) &&
			!Status(Flags_STATUS::STA_MOVIE2_ON) && !Status(Flags_STATUS::STA_DIEDEMO) &&
			!Status(Flags_STATUS::STA_NOW_LOADING) && !Status(Flags_STATUS::STA_SHOOTING);
	}

	template <typename Fn>
	void ForEachItem(Fn fn)
	{
		cItem* itmPtr = ItemMgr->m_pItem_14;
		int count = ItemMgr->m_array_num_1C;
		for (int i = 0; i <= count; i++)
		{
			itmPtr++; // same iteration as re4_tweaks' item manager (first entry is skipped)
			if ((itmPtr->be_flag_4 & 1) == 0)
				continue;
			if (itmPtr->chr_5 != ItemMgr->m_char_13)
				continue;
			if (!fn(itmPtr))
				return;
		}
	}

	std::unordered_map<uint16_t, uint32_t> TakeSnapshot()
	{
		std::unordered_map<uint16_t, uint32_t> inv;
		ForEachItem([&](cItem* item) {
			uint16_t num = item->num_2 ? item->num_2 : 1;
			inv[uint16_t(item->id_0)] += num;
			return true;
		});
		return inv;
	}

	void RemoveItem(uint16_t id, uint32_t count)
	{
		while (count > 0)
		{
			cItem* found = nullptr;
			ForEachItem([&](cItem* item) {
				if (uint16_t(item->id_0) == id)
				{
					found = item;
					return false;
				}
				return true;
			});
			if (!found)
				return;
			if (found == ItemMgr->m_pWep_C)
			{
				Log("Not removing currently equipped weapon " + std::to_string(id));
				return;
			}
			uint16_t num = found->num_2 ? found->num_2 : 1;
			if (num > count)
			{
				found->num_2 = uint16_t(num - count);
				count = 0;
			}
			else
			{
				ItemMgr->erase(found);
				count -= num;
			}
		}
	}

	const char* ItemName(int id)
	{
		if (id >= 0 && id < 272 && EItemId_Names[id])
			return EItemId_Names[id];
		return "?";
	}

	// =============================================================================== save binding
	uint32_t* SaveWork()
	{
		GLOBAL_WK* g = GlobalPtr();
		return g ? g->save_free_work_5310 : nullptr;
	}

	uint32_t AppliedIndex()
	{
		uint32_t* w = SaveWork();
		return w ? w[kSlotIndex] : 0;
	}

	void SetAppliedIndex(uint32_t index)
	{
		if (uint32_t* w = SaveWork())
			w[kSlotIndex] = index;
	}

	void BindSave()
	{
		uint32_t* w = SaveWork();
		if (!w)
			return;
		if (w[kSlotMagic] != 0 || w[kSlotTag] != 0 || w[kSlotIndex] != 0)
			Log("Save work before binding: " + std::to_string(w[kSlotMagic]) + " " + std::to_string(w[kSlotTag]) + " " + std::to_string(w[kSlotIndex]));
		w[kSlotMagic] = kSaveMagic;
		w[kSlotTag] = saveTag;
		w[kSlotIndex] = 0;
		Log("Save linked to this Archipelago seed");
		AddToast("Save linked to this Archipelago seed");
	}

	bool CheckSaveBinding()
	{
		uint32_t* w = SaveWork();
		if (!w)
			return false;
		if (w[kSlotMagic] != kSaveMagic)
			BindSave();
		bool ok = w[kSlotTag] == saveTag;
		if (ok != saveBound)
		{
			saveBound = ok;
			if (!ok)
			{
				Log("Loaded save belongs to a different seed");
				AddToast("This save belongs to a different Archipelago seed! Type /bindsave in the client to relink it.");
			}
		}
		uiStatus = ok ? 2 : 3;
		return ok;
	}

	// =============================================================================== checks
	bool IsChecked(int64_t id)
	{
		return serverChecked.count(id) || localChecked.count(id);
	}

	void MarkChecked(const LocationDef& loc, const std::string& why)
	{
		if (IsChecked(loc.id))
			return;
		localChecked.insert(loc.id);
		pendingChecks.push_back(loc.id);
		Log("Check " + std::to_string(loc.id) + " (" + why + ")");
	}

	void FlushChecks()
	{
		if (pendingChecks.empty() || !clientConnected)
			return;
		Send({ {"cmd", "check"}, {"locations", pendingChecks} });
		pendingChecks.clear();
	}

	bool HasItem(const LocationDef& loc, int id)
	{
		for (int i : loc.items)
			if (i == id)
				return true;
		return false;
	}

	// Returns true if the item came from a shuffled location (and must be removed)
	bool HandlePickup(uint16_t id, uint16_t room, bool onlyCutscene)
	{
		const LocationDef* checkedMatch = nullptr;
		// exact room, not yet checked
		for (auto& loc : locations)
		{
			if (loc.kind != Kind::Pickup || loc.room != room || !HasItem(loc, id))
				continue;
			if (onlyCutscene && !loc.cut)
				continue;
			if (!IsChecked(loc.id))
			{
				MarkChecked(loc, std::string("picked up ") + ItemName(id));
				return true;
			}
			checkedMatch = &loc;
		}
		if (checkedMatch)
			return true; // picked up again (reloaded save): still remove the vanilla item
		// room not verified: same stage
		for (auto& loc : locations)
		{
			if (loc.kind != Kind::Pickup || !loc.loose || (loc.room >> 8) != (room >> 8) || !HasItem(loc, id))
				continue;
			if (onlyCutscene && !loc.cut)
				continue;
			if (!IsChecked(loc.id))
			{
				SendLog("Matched " + std::string(ItemName(id)) + " in room r" + [&] { char b[8]; sprintf_s(b, "%x", room); return std::string(b); }() +
					" to location " + std::to_string(loc.id) + " by stage (room data needs fixing)");
				MarkChecked(loc, std::string("picked up ") + ItemName(id));
				return true;
			}
		}
		if (!onlyCutscene)
		{
			char b[160];
			sprintf_s(b, "Unmapped pickup: %s (id %d) in room r%x", ItemName(id), id, room);
			SendLog(b);
		}
		return false;
	}

	void HandleNewItem(uint16_t id, uint32_t count, int goldDelta)
	{
		GLOBAL_WK* g = GlobalPtr();
		uint16_t room = g->curRoomId_4FAC;

		// 1. items we granted ourselves (delayed by the case-full screen)
		auto exp = expectedGrants.find(id);
		if (exp != expectedGrants.end() && exp->second > 0)
		{
			int take = std::min<int>(exp->second, int(count));
			exp->second -= take;
			count -= take;
			if (exp->second <= 0)
				expectedGrants.erase(exp);
			if (count == 0)
				return;
		}

		// 2. bottle caps
		if (id >= 220 && id <= 243)
		{
			for (auto& loc : locations)
				if (loc.kind == Kind::BottleCap && HasItem(loc, id))
					MarkChecked(loc, std::string("won cap ") + ItemName(id));
			return;
		}

		bool shop = (frame - lastShopCtxFrame) <= kShopWindowFrames;
		bool pickup = (frame - lastPickupCtxFrame) <= kPickupWindowFrames;

		// 3. Merchant
		if (shop)
		{
			if (id == 33 && goldDelta >= 0) // Punisher handed over for free: blue medallion reward
			{
				for (auto& loc : locations)
					if (loc.kind == Kind::MedallionReward)
						MarkChecked(loc, "medallion reward");
				return;
			}
			for (auto& loc : locations)
				if (loc.kind == Kind::Merchant && HasItem(loc, id))
					MarkChecked(loc, std::string("bought ") + ItemName(id));
			return;
		}

		// 4. world pickups
		bool remove = false;
		uint32_t removeCount = 0;
		for (uint32_t i = 0; i < count; i++)
		{
			if (HandlePickup(id, room, !pickup))
			{
				remove = true;
				removeCount++;
			}
		}
		if (remove)
			pendingRemovals.push_back({ id, removeCount });
	}

	void DiffInventory()
	{
		GLOBAL_WK* g = GlobalPtr();
		auto cur = TakeSnapshot();
		int gold = g->goldAmount_4FA8;
		int caseSize = SubScreenWk->board_size_2AA;

		if (resetSnapshot)
		{
			prevInv = std::move(cur);
			prevGold = gold;
			prevCaseSize = caseSize;
			resetSnapshot = false;
			return;
		}

		int goldDelta = gold - prevGold;
		for (auto& [id, num] : cur)
		{
			uint32_t before = 0;
			auto it = prevInv.find(id);
			if (it != prevInv.end())
				before = it->second;
			if (num > before)
				HandleNewItem(id, num - before, goldDelta);
		}

		// Attache case bought from the Merchant (case size changes instead of an item appearing)
		if (caseSize > prevCaseSize && prevCaseSize >= 0 && (frame - lastShopCtxFrame) <= kShopWindowFrames)
		{
			int caseItem = 124 + caseSize;
			for (auto& loc : locations)
				if (loc.kind == Kind::Merchant && HasItem(loc, caseItem))
					MarkChecked(loc, std::string("bought ") + ItemName(caseItem));
		}

		prevInv = TakeSnapshot();
		prevGold = gold;
		prevCaseSize = caseSize;
	}

	void ProcessRemovals()
	{
		if (pendingRemovals.empty() || !SafeForInventory())
			return;
		for (auto& r : pendingRemovals)
			RemoveItem(r.id, r.count);
		pendingRemovals.clear();
		prevInv = TakeSnapshot();
	}

	// =============================================================================== bosses
	void DefeatedBoss(uint8_t emId, uint16_t room)
	{
		char b[96];
		sprintf_s(b, "Boss defeated: em %02X in room r%x", emId, room);
		Log(b);

		if (emId == 0x31 || emId == 0x3F) // Saddler
		{
			if (!goalSent)
			{
				goalSent = true;
				Send({ {"cmd", "goal"} });
				AddToast("Saddler defeated - goal complete!");
			}
			return;
		}

		const LocationDef* fallback = nullptr;
		for (auto& loc : locations)
		{
			if (loc.kind != Kind::Boss || loc.em != emId || IsChecked(loc.id))
				continue;
			if (loc.room == room)
			{
				MarkChecked(loc, "boss");
				return;
			}
			if (!fallback)
				fallback = &loc;
		}
		if (fallback)
		{
			SendLog("Boss matched by type only, room data may be off");
			MarkChecked(*fallback, "boss");
		}
	}

	void TrackBosses()
	{
		cEmMgr* mgr = EmMgrPtr();
		GLOBAL_WK* g = GlobalPtr();
		if (!mgr || !mgr->m_Array_4)
			return;
		uint16_t room = g->curRoomId_4FAC;

		// tracked bosses that died or vanished this frame
		for (auto it = trackedBosses.begin(); it != trackedBosses.end();)
		{
			cEm* em = mgr->get(it->first);
			bool gone = !em->IsValid() || em->guid_F8 != it->second.guid;
			bool dead = !gone && em->hp_324 <= 0;
			if ((gone || dead) && g->playerHpCur_4FB4 > 0)
			{
				DefeatedBoss(it->second.id, room);
				it = trackedBosses.erase(it);
			}
			else if (gone)
				it = trackedBosses.erase(it);
			else
				++it;
		}

		// start tracking live bosses
		for (uint32_t i = 0; i < mgr->m_nArray_8; i++)
		{
			cEm* em = mgr->get(i);
			if (!em->IsValid() || !bossEmIds.count(em->id_100) || em->hp_324 <= 0)
				continue;
			if (!trackedBosses.count(i))
				trackedBosses[i] = { em->guid_F8, em->id_100 };
		}
	}

	// Research aid: in the farm and graveyard (blue medallion rooms), log every object/enemy that dies or
	// vanishes so the medallions' em id can be identified from a play session.
	std::unordered_map<uint32_t, TrackedEm> discoveryEms;
	void DiscoveryLog()
	{
		cEmMgr* mgr = EmMgrPtr();
		GLOBAL_WK* g = GlobalPtr();
		if (!mgr || !mgr->m_Array_4)
			return;
		uint16_t room = g->curRoomId_4FAC;
		if (room != 0x103 && room != 0x108)
		{
			discoveryEms.clear();
			return;
		}
		for (auto it = discoveryEms.begin(); it != discoveryEms.end();)
		{
			cEm* em = mgr->get(it->first);
			bool gone = !em->IsValid() || em->guid_F8 != it->second.guid;
			if (gone || em->hp_324 <= 0)
			{
				char b[128];
				sprintf_s(b, "[discovery] r%x: em %02X type %d %s", room, it->second.id, gone ? -1 : int(em->type_101), gone ? "vanished" : "died");
				Log(b);
				it = discoveryEms.erase(it);
			}
			else
				++it;
		}
		for (uint32_t i = 0; i < mgr->m_nArray_8; i++)
		{
			cEm* em = mgr->get(i);
			if (em->IsValid() && em->hp_324 > 0 && !discoveryEms.count(i))
				discoveryEms[i] = { em->guid_F8, em->id_100 };
		}
	}

	// =============================================================================== received items
	void ApplyItem(const ReceivedItem& item)
	{
		GLOBAL_WK* g = GlobalPtr();
		auto defIt = itemDefs.find(item.id);
		std::string label = item.name.empty() ? std::to_string(item.id) : item.name;
		if (defIt == itemDefs.end())
		{
			SendLog("Unknown item id " + std::to_string(item.id));
			return;
		}
		const ItemDef& def = defIt->second;

		if (def.kind == "pesetas")
		{
			g->goldAmount_4FA8 = std::min(g->goldAmount_4FA8 + def.amount, 999999);
			prevGold = g->goldAmount_4FA8;
			bio4::SndCall(0, 0x10, 0, 0, 0, 0);
		}
		else if (def.kind == "attache_case")
		{
			int next = SubScreenWk->board_size_2AA + 1;
			if (next <= 3)
			{
				InventoryItemAdd(ITEM_ID(124 + next), 1, false, true);
				prevCaseSize = SubScreenWk->board_size_2AA;
			}
			else
			{
				g->goldAmount_4FA8 = std::min(g->goldAmount_4FA8 + 10000, 999999);
				prevGold = g->goldAmount_4FA8;
			}
		}
		else if (def.kind == "game" && def.game >= 0)
		{
			ITEM_INFO info;
			bio4::itemInfo(ITEM_ID(def.game), &info);
			uint32_t count = info.defNum_3 ? info.defNum_3 : 1;
			auto before = TakeSnapshot();
			InventoryItemAdd(ITEM_ID(def.game), count, false, true);
			auto after = TakeSnapshot();
			uint32_t b = before.count(uint16_t(def.game)) ? before[uint16_t(def.game)] : 0;
			uint32_t a = after.count(uint16_t(def.game)) ? after[uint16_t(def.game)] : 0;
			if (a <= b)
				expectedGrants[uint16_t(def.game)] += int(count); // went to the "case full" screen
		}

		prevInv = TakeSnapshot();
		AddToast(item.from.empty() ? ("Received " + label) : ("Received " + label + " from " + item.from));
	}

	void ApplyReceivedItems()
	{
		if (!SafeForInventory() || frame - lastGrantFrame < kGrantCooldownFrames)
			return;
		uint32_t index = AppliedIndex();
		if (index >= received.size())
			return;
		ApplyItem(received[index]);
		SetAppliedIndex(index + 1);
		lastGrantFrame = frame;
		Send({ {"cmd", "received"}, {"received", index + 1} });
	}

	// =============================================================================== death link
	void UpdateDeath()
	{
		GLOBAL_WK* g = GlobalPtr();
		bool dead = g->playerHpCur_4FB4 <= 0 || Status(Flags_STATUS::STA_DIEDEMO);
		if (dead && !wasDead)
		{
			if (deathLink && frame - lastKillFrame > 600)
				Send({ {"cmd", "death"} });
		}
		wasDead = dead;

		if (pendingKill && SafeForInventory())
		{
			pendingKill = false;
			lastKillFrame = frame;
			g->playerHpCur_4FB4 = 0;
		}
	}

	// =============================================================================== messages
	Kind ParseKind(const std::string& k)
	{
		if (k == "pickup") return Kind::Pickup;
		if (k == "boss") return Kind::Boss;
		if (k == "merchant") return Kind::Merchant;
		if (k == "medallion_reward") return Kind::MedallionReward;
		if (k == "medallion") return Kind::Medallion;
		if (k == "bottle_cap") return Kind::BottleCap;
		return Kind::Unknown;
	}

	void HandleConfig(const json& msg)
	{
		const json& sd = msg.value("slot_data", json::object());
		locations.clear();
		itemDefs.clear();
		bossEmIds = { 0x31, 0x3F };
		for (auto& l : sd.value("locations", json::array()))
		{
			LocationDef d;
			d.id = l.value("id", int64_t(0));
			d.kind = ParseKind(l.value("k", std::string()));
			d.room = l.value("room", -1);
			d.em = l.value("em_id", -1);
			d.loose = l.value("loose", 0) != 0;
			d.cut = l.value("cut", 0) != 0;
			for (auto& i : l.value("items", json::array()))
				d.items.push_back(i.get<int>());
			if (d.kind == Kind::Boss && d.em >= 0)
				bossEmIds.insert(d.em);
			locations.push_back(std::move(d));
		}
		for (auto& i : sd.value("items", json::array()))
		{
			ItemDef d;
			d.kind = i.value("k", std::string());
			d.game = i.value("g", -1);
			d.amount = i.value("amount", 0);
			itemDefs[i.value("id", int64_t(0))] = d;
		}
		deathLink = sd.value("death_link", false);
		uint32_t newTag = msg.value("save_tag", uint32_t(0));
		if (newTag != saveTag)
		{
			saveTag = newTag;
			saveBound = false;
			goalSent = false;
			localChecked.clear();
		}
		configured = true;
		Log("Configured: " + std::to_string(locations.size()) + " locations, slot " + msg.value("slot", std::string()));
		AddToast("Archipelago: connected as " + msg.value("slot", std::string()));
	}

	void ProcessInbox()
	{
		std::deque<json> msgs;
		{
			std::lock_guard<std::mutex> lock(inboxMutex);
			msgs.swap(inbox);
		}

		for (auto& msg : msgs)
		{
			std::string cmd = msg.value("cmd", std::string());
			try
			{
				if (cmd == "config")
					HandleConfig(msg);
				else if (cmd == "items")
				{
					received.clear();
					for (auto& it : msg.value("items", json::array()))
						received.push_back({ it.value("id", int64_t(0)), it.value("name", std::string()), it.value("from", std::string()) });
				}
				else if (cmd == "checked")
				{
					for (auto& id : msg.value("locations", json::array()))
						serverChecked.insert(id.get<int64_t>());
				}
				else if (cmd == "message")
					AddToast(msg.value("text", std::string()));
				else if (cmd == "kill")
				{
					if (deathLink)
					{
						pendingKill = true;
						AddToast("DeathLink: " + msg.value("cause", std::string("someone died")));
					}
				}
				else if (cmd == "bind_save")
				{
					if (configured && InMainLoop())
					{
						BindSave();
						saveBound = false;
					}
				}
			}
			catch (const std::exception& e)
			{
				Log(std::string("Error handling message '") + cmd + "': " + e.what());
			}
		}
	}

	void SendHello()
	{
		uint32_t* w = SaveWork();
		json hello = { {"cmd", "hello"}, {"version", kProtocolVersion},
			{"save_tag", w && w[kSlotMagic] == kSaveMagic ? w[kSlotTag] : 0},
			{"received", w && w[kSlotMagic] == kSaveMagic ? int(w[kSlotIndex]) : -1} };
		Send(hello);

		// re-send checks the server may not have seen (sent while the client was away)
		std::vector<int64_t> resend;
		for (auto id : localChecked)
			if (!serverChecked.count(id))
				resend.push_back(id);
		if (!resend.empty())
			Send({ {"cmd", "check"}, {"locations", resend} });
	}

	void Tick()
	{
		frame++;
		ProcessInbox();

		if (clientJustConnected.exchange(false))
			SendHello();

		if (!configured || !InMainLoop())
		{
			resetSnapshot = true;
			pendingRemovals.clear();
			trackedBosses.clear();
			discoveryEms.clear();
			return;
		}

		GLOBAL_WK* g = GlobalPtr();
		if (g->curRoomId_4FAC != prevRoom)
		{
			prevRoom = g->curRoomId_4FAC;
			trackedBosses.clear();
			if (prevRoom == 0x333 && !goalSent) // reached the jet-ski escape
			{
				goalSent = true;
				Send({ {"cmd", "goal"} });
			}
		}

		if (!CheckSaveBinding())
		{
			resetSnapshot = true;
			return;
		}

		if (!IsLeon())
		{
			resetSnapshot = true; // Ashley's segment: her pickups stay vanilla
			return;
		}

		if (PickupContext())
			lastPickupCtxFrame = frame;
		if (ShopContext())
			lastShopCtxFrame = frame;

		DiffInventory();
		ProcessRemovals();
		TrackBosses();
		DiscoveryLog();
		UpdateDeath();
		ApplyReceivedItems();
		FlushChecks();
	}
}

// =================================================================================== public API
void Archipelago_Tick()
{
	ap::Tick();
}

void Archipelago_Render()
{
	using namespace std::chrono;
	auto now = steady_clock::now();

	std::vector<std::pair<std::string, float>> lines;
	{
		std::lock_guard<std::mutex> lock(ap::toastMutex);
		while (!ap::toasts.empty() && now - ap::toasts.front().time > seconds(8))
			ap::toasts.pop_front();
		for (auto& t : ap::toasts)
		{
			float age = duration<float>(now - t.time).count();
			float alpha = age > 6.0f ? 1.0f - (age - 6.0f) / 2.0f : 1.0f;
			lines.emplace_back(t.text, alpha);
		}
	}

	int status = ap::uiStatus;
	bool inGame = GlobalPtr() && GlobalPtr()->Rno0_20 == uint8_t(GLOBAL_WK::Routine0::MainLoop);
	bool showStatus = inGame && status != 2;
	if (lines.empty() && !showStatus)
		return;

	ImGui::SetNextWindowPos(ImVec2(16, 16), ImGuiCond_Always);
	ImGui::SetNextWindowBgAlpha(0.45f);
	ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs |
		ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
	if (ImGui::Begin("##archipelago", nullptr, flags))
	{
		if (showStatus)
		{
			const char* text = status == 0 ? "Archipelago: waiting for the RE4 UHD Client" :
				status == 1 ? "Archipelago: client connected, waiting for server" :
				"Archipelago: this save belongs to another seed (/bindsave)";
			ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "%s", text);
		}
		for (auto& [text, alpha] : lines)
			ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, alpha), "%s", text.c_str());
	}
	ImGui::End();
}

void re4t::init::Archipelago()
{
	ap::logFile = std::filesystem::path(rootPath) / "re4_tweaks" / "archipelago.log";
	try
	{
		std::filesystem::create_directories(ap::logFile.parent_path());
	}
	catch (...) {}

	std::thread(ap::NetThread).detach();
	spd::log()->info("Archipelago module initialized");
}
