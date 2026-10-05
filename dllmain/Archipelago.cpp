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
//  - Per-save state lives inside the game's own save work (GLOBAL_WK::save_free_work), so dying,
//    continuing or loading an older save keeps items and checks consistent:
//      [28..59] bitset of locations collected in this save (by location offset, 1024 bits)
//      [60] magic  [61] seed tag  [62] index of the next received item to apply  [63] flags (bit0 = goal)

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
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <map>
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
	constexpr int kProtocolVersion = 2; // 2: checks/goal carry the seed tag, received items carry their location
	constexpr const char* kModVersion = "0.5.5"; // the APWorld release this build belongs to (checked by the client)

	constexpr uint32_t kSaveMagic = 0x52345041; // 'AP4R'
	constexpr int kSlotBits = 28;   // 32 slots (28..59) = 1024 location bits
	constexpr int kBitSlots = 32;
	constexpr int kSlotMagic = 60;
	constexpr int kSlotTag = 61;
	constexpr int kSlotIndex = 62;
	constexpr int kSlotFlags = 63;
	constexpr uint32_t kFlagGoal = 1;

	constexpr int kPickupWindowFrames = 120;
	constexpr int kShopWindowFrames = 30; // after the shop menu closes (organize screen, reward handed over)
	constexpr int kGrantCooldownFrames = 20;
	constexpr int kGrantGiveUpFrames = 90; // case-full screen closed without the item: player discarded it
	// Placed (map) items set a bit in the room's save data when taken; enemy drops don't. A supply pickup counts
	// as placed when such a bit flips up to kFlagBeforeFrames before or kFlagAfterFrames after it.
	constexpr int kFlagBeforeFrames = 120;
	constexpr int kFlagAfterFrames = 90;
	constexpr int kLegacyAfterUnflagged = 8;
	constexpr int kFlagSettleFrames = 15;    // a flip is only handed out once this old (lets nearby pickups register)
	constexpr int kDecideFrames = kFlagAfterFrames + kFlagSettleFrames + 5; // a pickup without a flag by then is a drop
	constexpr int kPrevRoomWatchFrames = 300; // keep watching the room just left (flags land ~1s after a pickup)

	constexpr uint16_t kRoomStart = 0x100;
	constexpr uint16_t kRoomOpening = 0x120;
	constexpr uint16_t kRoomSaddler = 0x332;
	constexpr uint16_t kRoomJetski = 0x333;

	enum class Kind { Pickup, Boss, Merchant, MedallionReward, BottleCap, Bonus, Pesetas, Drop, Unknown };

	struct LocationDef
	{
		int64_t id = 0;
		int offset = -1;   // bit index in the save bitset
		Kind kind = Kind::Unknown;
		int room = -1;
		int em = -1;
		std::vector<int> items;
		bool loose = false; // room id not verified: allow a match anywhere in the same stage
		bool cut = false;   // given by a cutscene/puzzle rather than the pickup screen
		bool consumable = false; // ammo/herb/grenade spot: matched by room, any consumable counts
		int stage = 0;           // bonus treasure / pesetas / enemy drops: 1 village, 2 castle, 3 island
		bool keep = false;       // holds its own vanilla item for this player: send the check, leave the item
		bool excluded = false;   // filler-only / may not exist: left out of the "checks here" counter
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
		int64_t loc = -1;  // where it was found
		bool own = false;  // found in this player's own world
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

	// ---------------- UI / log state (shared) -----------------------------------------------
	std::mutex toastMutex;
	std::deque<Toast> toasts;
	std::atomic<int> uiStatus{ 0 }; // 0 waiting for client, 1 waiting for server, 2 ready, 3 save mismatch, 4 save not linked, 5 easy
	std::atomic<int> uiRoomChecks{ 0 };   // item checks not yet collected in the current room (game thread computes)
	std::atomic<int> uiRoomSupplies{ 0 }; // ammo/herb checks not yet collected in the current room
	std::mutex logMutex;
	std::deque<std::string> consoleQueue; // lines for con.log, flushed on the main thread
	std::filesystem::path logFile;
	std::filesystem::path configFile;
	std::filesystem::path learnedFile; // what the module learned about this game install (room flags work)

	// ---------------- main-thread state -------------------------------------------------------
	bool configured = false;
	bool configFromDisk = false; // last seed's config, read at startup: never auto-links a new game to it
	bool deathLink = false;
	bool merchantCheckOnly = false; // first purchase only sends the check; the bought item is taken back
	float enemyHpMin = 0.0f, enemyHpMax = 0.0f; // random enemy health multiplier range (0 = off)
	uint32_t saveTag = 0;
	int64_t locationBase = 0;
	std::vector<LocationDef> locations;
	std::unordered_map<int64_t, ItemDef> itemDefs;
	std::vector<ReceivedItem> received;
	std::unordered_set<int64_t> serverChecked;
	std::vector<int64_t> pendingChecks;
	std::unordered_set<int> bossEmIds;

	uint64_t frame = 0;
	bool resetSnapshot = true;
	std::unordered_map<uint16_t, uint32_t> prevInv;
	std::unordered_set<cItem*> prevPtrs;
	int prevGold = 0;
	int prevCaseSize = -1;
	uint16_t prevRoom = 0xFFFF;
	uint64_t lastPickupCtxFrame = 0;
	uint64_t lastShopCtxFrame = 0;
	uint64_t lastGrantFrame = 0;
	uint64_t lastKillFrame = 0;
	bool wasDead = false;
	bool loggedContinue = true;
	bool pendingKill = false;
	bool goalReported = false;
	int saveState = 0; // 0 unknown, 1 bound to this seed, 2 other seed, 3 not linked

	struct Removal { uint16_t id; uint32_t count; std::vector<cItem*> fresh; };
	std::vector<Removal> pendingRemovals;

	// A received item that went to the "case full" screen and hasn't appeared in the inventory yet
	struct PendingGrant { bool active = false; uint16_t id = 0; uint32_t index = 0; uint64_t closedSince = 0; };
	PendingGrant pendingGrant;

	struct TrackedEm { uint32_t guid; uint8_t id; int16_t lastHp = 0; int16_t maxHp = 0; bool dying = false; };
	std::unordered_map<uint32_t, TrackedEm> trackedBosses; // key: index in EmMgr
	std::unordered_map<uint32_t, TrackedEm> discoveryEms;
	uint32_t warnedUnknownIndex = UINT32_MAX;

	// Diagnostics: the game marks placed (map) items as taken in the room's save data. Logging those flips next to
	// each pickup tells us whether placed items and enemy drops can be told apart.
	struct RoomFlagSnapshot { uint16_t room = 0xFFFF; uint32_t item[4] = {}; uint32_t find[4] = {}; uint16_t etc[64] = {}; };
	RoomFlagSnapshot roomFlags;
	RoomFlagSnapshot prevRoomFlags; // the room just left, watched for kPrevRoomWatchFrames
	uint64_t prevRoomUntil = 0;
	uint64_t lastItemFlagFrame = 0;

	// Supply pickups (ammo/herbs/grenades and pesetas) wait here until we know whether they were placed items
	// (a room item flag flipped) or enemy/random drops (no flag), which never count.
	enum class FlagMode { Unknown, Flags, Legacy };
	FlagMode flagMode = FlagMode::Unknown;
	struct Flip { uint64_t frame; uint16_t room; int bit; };
	std::deque<Flip> unclaimedFlips; // item flag flips not yet matched to a pickup
	// Hidden items (in a barrel/crate, or knocked down like an embedded Spinel) set their "found" flag when they
	// appear; the pickup itself may come much later with no flag of its own. Each one is a credit for its room
	// that one later unflagged pickup there can use.
	struct Reveal { uint64_t frame; bool container; }; // container: popped out of a barrel/crate (both flags at once)
	std::map<uint16_t, std::map<int, Reveal>> revealed; // room -> bit -> when it appeared
	uint64_t roomEnteredFrame = 0;
	uint64_t lastAnyPickupFrame = 0; // any item appearing in the inventory
	// treasures/key items whose own room flag never turned up (frame, room): a flag that comes later is probably
	// theirs (drops aren't listed: they never get a flag)
	std::deque<std::pair<uint64_t, uint16_t>> flaglessPickups;
	uint64_t lastCombineFrame = 0; // a treasure/key item went away (combined in the inventory screen)
	bool itemDecreasedNow = false;  // some item went away in this frame's diff (a sale at the Merchant)
	std::unordered_set<uint16_t> soldToMerchant; // sold this session: buying it back isn't a check-only purchase
	uint64_t lastCaseGrowFrame = 0;
	uint64_t lastCaseItemFrame = 0; // a case item (125-127) showed up: the case growing around then is the same purchase
	uint64_t lastMerchantNearFrame = 0; // STA_INTO_SHOP: set the whole time Leon is near a Merchant, not just in the menu
	uint32_t refusedIndex = UINT32_MAX; // received item the game refused: retried every few seconds
	uint64_t refusedFrame = 0;
	int refusedTries = 0;
	bool warnedEquipped = false;
	std::chrono::steady_clock::time_point deliveryWaitSince{}; // items waiting and not deliverable since then
	bool deliveryWaitLogged = false;
	struct SupplyPickup
	{
		bool gold = false;
		uint16_t id = 0;     // item id (consumables)
		uint32_t count = 0;  // item count, or pesetas amount
		bool screen = false; // came through the pickup screen
		std::vector<cItem*> fresh;
		uint16_t room = 0;
		uint64_t frame = 0;
		bool other = false;   // any other pickup: only here so it can claim its own flag flip
		bool matched = false; // a flag flip was assigned to it
	};
	std::vector<SupplyPickup> pendingSupplies;
	std::vector<SupplyPickup> undecidedSupplies; // unflagged while we don't know yet whether flags work

	// =============================================================================== logging
	// Safe from any thread: writes the file directly, console lines are queued for the main thread.
	void Log(const std::string& text)
	{
		std::lock_guard<std::mutex> lock(logMutex);
		consoleQueue.push_back(text);
		while (consoleQueue.size() > 64)
			consoleQueue.pop_front();
		try
		{
			std::ofstream f(logFile, std::ios::app);
			auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
			std::tm tmNow{};
			localtime_s(&tmNow, &now);
			char buf[32];
			std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmNow);
			f << buf << "  " << text << "\n";
		}
		catch (...) {}
	}

	void FlushConsole()
	{
		std::deque<std::string> lines;
		{
			std::lock_guard<std::mutex> lock(logMutex);
			lines.swap(consoleQueue);
		}
		for (auto& l : lines)
			con.log("[AP] %s", l.c_str());
	}

	void AddToast(const std::string& text)
	{
		std::lock_guard<std::mutex> lock(toastMutex);
		toasts.push_back({ text, std::chrono::steady_clock::now() });
		while (toasts.size() > 8)
			toasts.pop_front();
	}

	std::string Hex(int v)
	{
		char b[16];
		sprintf_s(b, "%x", v);
		return b;
	}

	// =============================================================================== network
	// Messages to the client are queued and written by SenderThread, so the game thread never waits on the socket
	std::mutex outboxMutex;
	std::condition_variable outboxCv;
	std::deque<std::string> outbox;

	void Send(const json& msg)
	{
		if (!clientConnected)
			return;
		std::string line;
		try
		{
			// invalid UTF-8 (a name, a log line) must never throw on the game thread
			line = msg.dump(-1, ' ', false, json::error_handler_t::replace) + "\n";
		}
		catch (...)
		{
			return;
		}
		{
			std::lock_guard<std::mutex> lock(outboxMutex);
			if (outbox.size() > 4000)
				outbox.pop_front(); // client not reading at all: drop the oldest (checks are resent from the save)
			outbox.push_back(std::move(line));
		}
		outboxCv.notify_one();
	}

	void SenderThread()
	{
		while (true)
		{
			std::string line;
			{
				std::unique_lock<std::mutex> lock(outboxMutex);
				outboxCv.wait(lock, [] { return !outbox.empty(); });
				line = std::move(outbox.front());
				outbox.pop_front();
			}
			std::lock_guard<std::mutex> lock(sockMutex);
			if (clientSock == INVALID_SOCKET)
				continue;
			const char* data = line.data();
			int left = int(line.size());
			while (left > 0)
			{
				int sent = send(clientSock, data, left, 0);
				if (sent <= 0)
				{
					// a half-written line would corrupt the stream: drop the connection, the client reconnects and
					// everything is resent from the save
					shutdown(clientSock, SD_BOTH);
					break;
				}
				data += sent;
				left -= sent;
			}
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
			DWORD sendTimeout = 2000; // never block the game thread for long if the client stops reading
			setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char*)&sendTimeout, sizeof(sendTimeout));

			{
				std::lock_guard<std::mutex> lock(outboxMutex);
				outbox.clear(); // meant for the previous connection
			}
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
				if (buffer.size() > 16 * 1024 * 1024)
					break; // garbage, drop the connection
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
						if (!msg.is_object())
							continue;
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

	bool IsMainGame()
	{
		GLOBAL_WK* g = GlobalPtr();
		if (!g || g->curRoomId_4FAC >= 0x400) // 0x4xx Mercenaries, 0x5xx Separate Ways
			return false;
		// Assignment Ada reuses island rooms: only Leon's (and Ashley's) story counts
		PlayerCharacter pc = g->pl_type_4FC8;
		return pc == PlayerCharacter::Leon || pc == PlayerCharacter::Ashley || pc == PlayerCharacter::LeonAshley;
	}

	// Easy (and the internal "Amateur") cut whole rooms that hold checks: the Castle Gate Key room, the hedge maze
	// with the Moonstones, the King's Grail room, the clock tower
	bool IsEasyMode()
	{
		GLOBAL_WK* g = GlobalPtr();
		int d = g ? int(g->gameDifficulty_847C) : 0;
		return d >= int(GameDifficulty::VeryEasy) && d < int(GameDifficulty::Medium);
	}

	bool IsLeon()
	{
		GLOBAL_WK* g = GlobalPtr();
		return g && (g->pl_type_4FC8 == PlayerCharacter::Leon || g->pl_type_4FC8 == PlayerCharacter::LeonAshley);
	}

	bool PickupContext()
	{
		if ((SubScreenWk->open_flag_2C & SS_OPEN_ITEM) != 0 || SubScreenWk->item_get_flag_40 || Status(Flags_STATUS::STA_ITEM_GET))
			return true;
		// a picked-up item going through the "organize" screen because the case is full
		return (SubScreenWk->open_flag_2C & SS_OPEN_PZZL) != 0 && SubScreenWk->get_item_id_2F6 != 0 && !pendingGrant.active;
	}

	// The Merchant's menu. (STA_INTO_SHOP stays set the whole time Leon is in a Merchant's area, so on its own it
	// can't tell a purchase from a pickup next to the Merchant.)
	bool ShopContext()
	{
		return (SubScreenWk->open_flag_2C & SS_OPEN_SHOP) != 0;
	}

	// Safe moment to change Leon's inventory: same gate as the re4_tweaks trainer, plus no cutscenes
	bool SafeForInventory()
	{
		GLOBAL_WK* g = GlobalPtr();
		if (!InMainLoop() || !IsLeon() || !IsMainGame())
			return false;
		cPlayer* pl = PlayerPtr();
		if (!pl || !pl->subScrCheck() || OptionOpenFlag())
			return false;
		if (SubScreenWk->open_flag_2C != SS_OPEN_NULL || SubScreenWk->item_get_flag_40)
			return false;
		if (g->playerHpCur_4FB4 <= 0)
			return false;
		return !Status(Flags_STATUS::STA_ITEM_GET) &&
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

	// How many of an item one inventory entry stands for: its stack size for stackable items, otherwise 1
	// (a weapon's num field isn't a count, so it must never look like several weapons)
	uint16_t EntryCount(const cItem* item)
	{
		static int16_t maxNum[272];
		static bool init = false;
		if (!init)
		{
			for (auto& m : maxNum)
				m = -1;
			init = true;
		}
		int id = int(item->id_0);
		if (id < 0 || id >= 272)
			return 1;
		if (maxNum[id] < 0)
		{
			ITEM_INFO info;
			bio4::itemInfo(ITEM_ID(id), &info);
			maxNum[id] = int16_t(info.maxNum_4);
		}
		if (maxNum[id] <= 1)
			return 1;
		return item->num_2 ? item->num_2 : 1;
	}

	void TakeSnapshot(std::unordered_map<uint16_t, uint32_t>& inv, std::unordered_set<cItem*>& ptrs)
	{
		inv.clear();
		ptrs.clear();
		ForEachItem([&](cItem* item) {
			uint16_t num = EntryCount(item);
			inv[uint16_t(item->id_0)] += num;
			ptrs.insert(item);
			return true;
		});
	}

	void Resnapshot()
	{
		TakeSnapshot(prevInv, prevPtrs);
	}

	uint32_t CountOf(uint16_t id)
	{
		uint32_t total = 0;
		ForEachItem([&](cItem* item) {
			if (uint16_t(item->id_0) == id)
				total += EntryCount(item);
			return true;
		});
		return total;
	}

	// Remove `count` of an item, taking the copies that just appeared first, never the equipped weapon
	// Returns how many couldn't be removed because the only copies left are the equipped weapon
	uint32_t RemoveItem(const Removal& r)
	{
		uint32_t count = r.count;
		auto stillValid = [&](cItem* p) {
			bool ok = false;
			ForEachItem([&](cItem* item) {
				if (item == p)
				{
					ok = uint16_t(item->id_0) == r.id;
					return false;
				}
				return true;
			});
			return ok;
		};

		std::vector<cItem*> order;
		for (cItem* p : r.fresh)
			if (stillValid(p))
				order.push_back(p);
		ForEachItem([&](cItem* item) {
			if (uint16_t(item->id_0) == r.id && std::find(order.begin(), order.end(), item) == order.end())
				order.push_back(item);
			return true;
		});

		bool equippedLeft = false;
		for (cItem* item : order)
		{
			if (count == 0)
				break;
			if (item == ItemMgr->m_pWep_C)
			{
				equippedLeft = true;
				continue;
			}
			uint16_t num = EntryCount(item);
			if (num > count)
			{
				item->num_2 = uint16_t(num - count);
				count = 0;
			}
			else
			{
				ItemMgr->erase(item);
				count -= num;
			}
		}
		if (count > 0 && equippedLeft)
			return count;
		if (count > 0)
			Log("Could not remove " + std::to_string(count) + " of item " + std::to_string(r.id));
		return 0;
	}

	const char* ItemName(int id)
	{
		if (id >= 0 && id < 272 && EItemId_Names[id])
			return EItemId_Names[id];
		return "?";
	}

	bool IsTreasureType(int id)
	{
		ITEM_INFO info;
		bio4::itemInfo(ITEM_ID(id), &info);
		return info.type_2 == ITEM_TYPE_TREASURE || info.type_2 == ITEM_TYPE_TREASURE_GEM;
	}

	bool IsInterestingType(int id)
	{
		ITEM_INFO info;
		bio4::itemInfo(ITEM_ID(id), &info);
		switch (info.type_2)
		{
		case ITEM_TYPE_WEAPON:
		case ITEM_TYPE_TREASURE:
		case ITEM_TYPE_KEY_ITEM:
		case ITEM_TYPE_WEAPON_MOD:
		case ITEM_TYPE_TREASURE_GEM:
		case ITEM_TYPE_IMPORTANT:
			return true;
		default:
			return false;
		}
	}

	bool IsConsumableType(int id)
	{
		ITEM_INFO info;
		bio4::itemInfo(ITEM_ID(id), &info);
		return info.type_2 == ITEM_TYPE_AMMO || info.type_2 == ITEM_TYPE_GRENADE || info.type_2 == ITEM_TYPE_CONSUMABLE;
	}

	// =============================================================================== save state
	uint32_t* SaveWork()
	{
		GLOBAL_WK* g = GlobalPtr();
		return g ? g->save_free_work_5310 : nullptr;
	}

	bool SaveBit(int offset)
	{
		uint32_t* w = SaveWork();
		if (!w || offset < 0 || offset >= kBitSlots * 32)
			return false;
		return (w[kSlotBits + offset / 32] >> (offset % 32)) & 1;
	}

	void SetSaveBit(int offset)
	{
		uint32_t* w = SaveWork();
		if (!w || offset < 0 || offset >= kBitSlots * 32)
			return;
		w[kSlotBits + offset / 32] |= (1u << (offset % 32));
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
		bool dirty = false;
		for (int i = kSlotBits; i < 64; i++)
			dirty |= w[i] != 0;
		if (dirty)
			Log("Save work before binding was not empty (slots 28-63); overwriting");
		for (int i = kSlotBits; i < 64; i++)
			w[i] = 0;
		w[kSlotMagic] = kSaveMagic;
		w[kSlotTag] = saveTag;
		Log("Save linked to this Archipelago seed");
		AddToast("Save linked to this Archipelago seed");
	}

	// Returns true when the loaded save belongs to the connected seed
	bool CheckSaveBinding()
	{
		uint32_t* w = SaveWork();
		GLOBAL_WK* g = GlobalPtr();
		if (!w || !g)
			return false;

		int state;
		if (w[kSlotMagic] != kSaveMagic)
		{
			// only a brand new game is linked automatically
			// (not with the last session's config from disk: the new game may be for a new seed)
			bool freshGame = (g->curRoomId_4FAC == kRoomStart || g->curRoomId_4FAC == kRoomOpening);
			if (freshGame && IsEasyMode())
				state = 5;
			else if (freshGame && !configFromDisk)
			{
				BindSave();
				Log("Difficulty " + std::to_string(int(g->gameDifficulty_847C)));
				state = 1;
			}
			else
				state = 3;
		}
		else
			state = w[kSlotTag] == saveTag ? 1 : 2;

		if (state != saveState)
		{
			saveState = state;
			if (state == 2)
			{
				Log("Loaded save belongs to a different seed");
				AddToast(configFromDisk
					? "Waiting for the Archipelago client to connect to your room..."
					: "This save belongs to a different Archipelago seed! Load the right save, or /bindsave to relink it.");
			}
			else if (state == 5)
			{
				Log("New game on Easy: not linked");
				AddToast("Easy cuts rooms that hold checks. Start a New Game on Normal or Professional.");
			}
			else if (state == 1 && IsEasyMode())
				AddToast("Warning: this save is on Easy, which cuts rooms that hold checks.");
			else if (state == 3)
			{
				Log("Loaded save is not linked to Archipelago");
				AddToast(configFromDisk && (g->curRoomId_4FAC == kRoomStart || g->curRoomId_4FAC == kRoomOpening)
					? "New game: connect the Archipelago client to link it to your multiworld."
					: "This save isn't linked to Archipelago. Start a New Game, or type /bindsave in the client.");
			}
		}
		uiStatus = !clientConnected ? 0 : configFromDisk ? 1 : (state == 1 ? 2 : state == 2 ? 3 : state == 5 ? 5 : 4);
		return state == 1;
	}

	// =============================================================================== checks
	void SendCheck(const LocationDef& loc, const std::string& why)
	{
		SetSaveBit(loc.offset);
		if (serverChecked.count(loc.id))
			return;
		if (std::find(pendingChecks.begin(), pendingChecks.end(), loc.id) == pendingChecks.end())
			pendingChecks.push_back(loc.id);
		Log("Check " + std::to_string(loc.id) + " (" + why + ")");
	}

	// For one-shot events (merchant/boss/cap) "already done" means done in this save or on the server
	bool EventDone(const LocationDef& loc)
	{
		return SaveBit(loc.offset) || serverChecked.count(loc.id);
	}

	// Checks and the goal carry the save's seed tag so the client can drop anything meant for another seed.
	// Nothing goes out under the last session's config (it may be another seed): the save remembers it all and
	// it's resent once the client sends the live config.
	void FlushChecks()
	{
		if (pendingChecks.empty() || !clientConnected || configFromDisk)
			return;
		Send({ {"cmd", "check"}, {"locations", pendingChecks}, {"tag", saveTag} });
		pendingChecks.clear();
	}

	void ReportGoal()
	{
		if (uint32_t* w = SaveWork())
			w[kSlotFlags] |= kFlagGoal;
		if (!goalReported)
		{
			goalReported = true;
			AddToast("Goal complete!");
		}
		if (!configFromDisk)
			Send({ {"cmd", "goal"}, {"tag", saveTag} });
	}

	bool HasItem(const LocationDef& loc, int id)
	{
		for (int i : loc.items)
			if (i == id)
				return true;
		return false;
	}

	// Matches a picked-up item to a location. Locations already collected *in this save* are skipped,
	// so re-collecting after a death maps to the same location again instead of farming the next one.
	// Returns true if the item came from a shuffled location and must be removed.
	// Matches a picked-up item to a location, most specific first:
	//   1. a location for this item in this room
	//   2. a location for this item anywhere in the same stage (guide room data can be wrong)
	//   3. treasures only: the stage's next bonus treasure check (random drops, data gaps)
	// Locations already collected *in this save* are skipped, and the bits roll back with the save, so
	// re-collecting after a death maps to the same location again instead of farming new ones.
	// Returns true if the item came from a check (and must be removed).
	bool HandlePickup(uint16_t id, uint16_t room, bool onlyCutscene)
	{
		// 0 no match, 1 matched (remove the item), 2 matched a location that keeps its vanilla item
		auto pass = [&](bool stageWide) -> int {
			for (auto& loc : locations)
			{
				if (loc.kind != Kind::Pickup || loc.consumable || !HasItem(loc, id) || SaveBit(loc.offset))
					continue;
				if (onlyCutscene && !loc.cut)
					continue;
				if (stageWide ? ((loc.room >> 8) != (room >> 8) || loc.room == room) : loc.room != room)
					continue;
				if (stageWide)
					SendLog("Matched " + std::string(ItemName(id)) + " in room r" + Hex(room) + " to location " +
						std::to_string(loc.id) + " (room r" + Hex(loc.room) + " in the data) by stage");
				SendCheck(loc, std::string("picked up ") + ItemName(id));
				return loc.keep ? 2 : 1;
			}
			return 0;
		};

		int matched = pass(false);
		if (!matched)
			matched = pass(true);
		if (matched)
			return matched == 1;

		if (!onlyCutscene && IsTreasureType(id))
		{
			for (auto& loc : locations)
			{
				if (loc.kind != Kind::Bonus || loc.stage != (room >> 8) || SaveBit(loc.offset))
					continue;
				SendLog("Bonus treasure: " + std::string(ItemName(id)) + " in room r" + Hex(room));
				SendCheck(loc, std::string("bonus treasure ") + ItemName(id));
				return true;
			}
		}

		if (!onlyCutscene && IsInterestingType(id))
			SendLog("Unmapped pickup: " + std::string(ItemName(id)) + " (id " + std::to_string(id) + ") in room r" + Hex(room));
		return false;
	}

	// Ammo/herbs/grenades: a room has N consumable spots; any consumable picked up there takes the next one.
	// (Container contents and enemy drops can differ from the guide, so the item type isn't matched.)
	bool HandleConsumablePickup(uint16_t id, uint16_t room)
	{
		// If this room has spots of its own, only those count. A room with none at all is probably one whose
		// id the guides got wrong, so it may take an unverified spot from the same stage.
		bool roomHasSpots = false;
		for (auto& loc : locations)
			if (loc.kind == Kind::Pickup && loc.consumable && loc.room == room)
				roomHasSpots = true;

		for (int loosePass = 0; loosePass < (roomHasSpots ? 1 : 2); loosePass++)
		{
			for (auto& loc : locations)
			{
				if (loc.kind != Kind::Pickup || !loc.consumable || SaveBit(loc.offset))
					continue;
				if (loosePass ? (!loc.loose || (loc.room >> 8) != (room >> 8)) : loc.room != room)
					continue;
				if (loosePass)
					SendLog("Consumable in room r" + Hex(room) + " matched location " + std::to_string(loc.id) + " by stage");
				SendCheck(loc, std::string("picked up ") + ItemName(id));
				return true;
			}
		}
		return false; // every spot here already collected: keep the item
	}

	// Pesetas: each stage has a pool of pesetas checks; every placed pesetas pickup takes the next one.
	// The pesetas themselves are kept.
	bool HandleGoldPickup(uint32_t amount, uint16_t room)
	{
		for (auto& loc : locations)
		{
			if (loc.kind != Kind::Pesetas || loc.stage != (room >> 8) || SaveBit(loc.offset))
				continue;
			SendCheck(loc, "picked up " + std::to_string(amount) + " pesetas");
			return true;
		}
		return false;
	}

	// Returns false if no check was left for it
	bool ClaimSupply(const SupplyPickup& p, bool removeItem)
	{
		if (p.gold)
			return HandleGoldPickup(p.count, p.room);
		if (!HandleConsumablePickup(p.id, p.room))
			return false;
		if (removeItem)
			pendingRemovals.push_back({ p.id, p.count, p.fresh });
		return true;
	}

	// Enemy drop checks (enemy_drop_checks): every drop takes its stage's next drop check; Leon keeps the drop
	bool HandleDropPickup(const SupplyPickup& p)
	{
		for (auto& loc : locations)
		{
			if (loc.kind != Kind::Drop || loc.stage != (p.room >> 8) || SaveBit(loc.offset))
				continue;
			SendCheck(loc, "enemy drop: " + (p.gold ? std::to_string(p.count) + " pesetas" : std::string(ItemName(p.id))));
			return true;
		}
		return false;
	}

	std::string SupplyName(const SupplyPickup& p)
	{
		return p.gold ? std::to_string(p.count) + " pesetas" : std::string(ItemName(p.id)) + " x" + std::to_string(p.count);
	}

	// A room item flag turned on with no item showing up in the inventory: the game merged a key item Leon already
	// holds (his copy came from the multiworld). Counts for an unchecked key-item location in this room.
	void SilentTake(uint16_t room, uint64_t flipFrame)
	{
		GLOBAL_WK* g = GlobalPtr();
		if (!IsLeon() || !g)
			return;
		// a pickup in this room shortly before had no flag of its own: this is most likely its late flag
		// (e.g. a long look at a treasure's description)
		for (auto& [pf, proom] : flaglessPickups)
			if (proom == room && pf <= flipFrame && flipFrame - pf < 900)
				return;
		for (auto& loc : locations)
		{
			if (loc.kind != Kind::Pickup || loc.consumable || loc.room != room || SaveBit(loc.offset))
				continue;
			for (int id : loc.items)
			{
				ITEM_INFO info;
				bio4::itemInfo(ITEM_ID(id), &info);
				if ((info.type_2 == ITEM_TYPE_KEY_ITEM || info.type_2 == ITEM_TYPE_IMPORTANT) && CountOf(uint16_t(id)) > 0)
				{
					SendLog(std::string("Room flag without a new item: counted as ") + ItemName(id) + " (already held)");
					SendCheck(loc, std::string("took ") + ItemName(id) + " (already held)");
					return;
				}
			}
		}
	}

	void RememberFlagless(const SupplyPickup& p)
	{
		flaglessPickups.push_back({ p.frame, p.room });
		while (flaglessPickups.size() > 32)
			flaglessPickups.pop_front();
	}

	void SaveLearnedState()
	{
		try
		{
			std::ofstream f(learnedFile, std::ios::trunc);
			f << json({ {"room_flags", flagMode == FlagMode::Flags} }).dump();
		}
		catch (...) {}
	}

	bool TakeRevealCredit(uint16_t room, int* bitOut);
	bool CheckCasePurchase(uint16_t caseItem);

	void ResolveSupplies()
	{
		// 1. give each flag flip (once it has settled a little) to the nearest pickup in time
		for (auto f = unclaimedFlips.begin(); f != unclaimedFlips.end();)
		{
			if (frame - f->frame < kFlagSettleFrames)
			{
				++f;
				continue;
			}
			SupplyPickup* best = nullptr;
			uint64_t bestDist = UINT64_MAX;
			for (auto& p : pendingSupplies)
			{
				if (p.matched || p.room != f->room)
					continue;
				uint64_t ff = f->frame;
				bool inWindow = ff >= p.frame ? (ff - p.frame <= kFlagAfterFrames) : (p.frame - ff <= kFlagBeforeFrames);
				uint64_t dist = ff >= p.frame ? ff - p.frame : p.frame - ff;
				if (inWindow && dist < bestDist)
				{
					best = &p;
					bestDist = dist;
				}
			}
			if (best)
			{
				best->matched = true;
				revealed[f->room].erase(f->bit); // a hidden item picked up right away: its credit is used
				f = unclaimedFlips.erase(f);
			}
			else if (frame - f->frame > kFlagBeforeFrames + kDecideFrames)
			{
				// nobody picked anything up near it (Ashley, a cutscene item, a barrel's contents appearing...),
				// or the game took an item without the inventory growing: a key item Leon already holds
				if (!revealed[f->room].count(f->bit))
					SilentTake(f->room, f->frame);
				f = unclaimedFlips.erase(f);
			}
			else
				++f;
		}

		// 2. decide
		for (auto it = pendingSupplies.begin(); it != pendingSupplies.end();)
		{
			SupplyPickup& p = *it;
			if (p.other)
			{
				if (!p.matched && frame - p.frame > kDecideFrames)
					RememberFlagless(p);
				if (p.matched || frame - p.frame > kDecideFrames)
					it = pendingSupplies.erase(it);
				else
					++it;
				continue;
			}
			if (p.matched)
			{
				if (flagMode != FlagMode::Flags)
				{
					Log("Placed items are recognized by their room flags: enemy drops won't take placed-item checks");
					flagMode = FlagMode::Flags;
					for (auto& u : undecidedSupplies) // those were drops
						HandleDropPickup(u);
					undecidedSupplies.clear();
					SaveLearnedState();
				}
				Log("[pickup] placed " + SupplyName(p) + " in r" + Hex(p.room));
				ClaimSupply(p, true);
				it = pendingSupplies.erase(it);
				continue;
			}
			if (frame - p.frame <= kDecideFrames)
			{
				++it;
				continue;
			}
			// no flag of its own: an item that appeared earlier from a barrel/crate (uses that credit) ...
			int hiddenBit = -1;
			if (TakeRevealCredit(p.room, &hiddenBit))
			{
				Log("[pickup] " + SupplyName(p) + " in r" + Hex(p.room) + " matches hidden item " + std::to_string(hiddenBit));
				p.matched = true;
				continue; // decided as placed on the next pass
			}
			// ... or an enemy drop / random container drop
			switch (flagMode)
			{
			case FlagMode::Flags:
				if (!HandleDropPickup(p))
					Log("[pickup] drop " + SupplyName(p) + " in r" + Hex(p.room) + " (not a check)");
				break;
			case FlagMode::Legacy:
				if (!ClaimSupply(p, true))
					HandleDropPickup(p);
				break;
			case FlagMode::Unknown:
				undecidedSupplies.push_back(p);
				if (int(undecidedSupplies.size()) >= kLegacyAfterUnflagged)
				{
					// This game never flagged a supply pickup: fall back to counting every pickup (as before 0.5)
					Log("No room flags seen for " + std::to_string(undecidedSupplies.size()) +
						" pickups: counting every ammo/herb/pesetas pickup instead");
					flagMode = FlagMode::Legacy;
					for (auto& u : undecidedSupplies)
						if (!ClaimSupply(u, false)) // already in the inventory for a while: the player keeps these
							HandleDropPickup(u);
					undecidedSupplies.clear();
				}
				break;
			}
			it = pendingSupplies.erase(it);
		}
	}

	// Forget the room snapshots (after a load, a death or a seed change the save data underneath is different)
	void ResetRoomWatch()
	{
		roomFlags.room = 0xFFFF;
		prevRoomFlags.room = 0xFFFF;
		prevRoomUntil = 0;
		unclaimedFlips.clear();
	}

	// Reports flags that turned on since the snapshot and updates it
	void DiffRoomFlags(RoomFlagSnapshot& snap, ROOM_SAVE_DATA* rs)
	{
		auto diff = [&](bool itemFlag, const uint32_t* now, uint32_t* before) {
			for (int w = 0; w < 4; w++)
			{
				uint32_t added = now[w] & ~before[w];
				for (int b = 0; b < 32 && added; b++)
					if (added & (0x80000000u >> b))
					{
						int bit = w * 32 + b;
						Log(std::string("[roomflag] r") + Hex(snap.room) + (itemFlag ? " item_flg" : " item_find") +
							" bit " + std::to_string(bit));
						if (itemFlag)
						{
							lastItemFlagFrame = frame;
							unclaimedFlips.push_back({ frame, snap.room, bit });
							// taken flag well after the found flag: that hidden item is being picked up now
							auto& credits = revealed[snap.room];
							auto c = credits.find(bit);
							if (c != credits.end() && frame - c->second.frame > 30)
								credits.erase(c);
						}
						else
						{
							// item_flg already set (it's processed first): a container's contents popping out;
							// otherwise a knocked-down item whose taken flag comes at the pickup
							bool container = (snap.item[w] & (0x80000000u >> b)) != 0;
							revealed[snap.room][bit] = { frame, container };
						}
					}
				before[w] = now[w];
			}
		};
		diff(true, rs->item_flg_8, snap.item);
		diff(false, rs->item_find_flg_18, snap.find);
		for (int i = 0; i < 64; i++)
			if (rs->EtcModelFlg[i] != snap.etc[i])
			{
				Log(std::string("[roomflag] r") + Hex(snap.room) + " etcmodel " + std::to_string(i) + " = " + Hex(rs->EtcModelFlg[i]));
				snap.etc[i] = rs->EtcModelFlg[i];
			}
	}

	void TakeRoomSnapshot(RoomFlagSnapshot& snap, uint16_t room, ROOM_SAVE_DATA* rs)
	{
		snap.room = room;
		memcpy(snap.item, rs->item_flg_8, sizeof(snap.item));
		memcpy(snap.find, rs->item_find_flg_18, sizeof(snap.find));
		memcpy(snap.etc, rs->EtcModelFlg, sizeof(snap.etc));
	}

	void WatchRoomFlags()
	{
		GLOBAL_WK* g = GlobalPtr();
		if (!RoomData)
			return;
		// the room just left: a pickup's flag can land a moment after walking through the door
		if (prevRoomFlags.room != 0xFFFF && frame <= prevRoomUntil)
		{
			if (ROOM_SAVE_DATA* prs = RoomData->getRoomSavePtr(prevRoomFlags.room))
				DiffRoomFlags(prevRoomFlags, prs);
		}
		ROOM_SAVE_DATA* rs = RoomData->getRoomSavePtr(g->curRoomId_4FAC);
		if (!rs)
			return;
		if (roomFlags.room != g->curRoomId_4FAC)
		{
			if (roomFlags.room != 0xFFFF)
			{
				prevRoomFlags = roomFlags;
				prevRoomUntil = frame + kPrevRoomWatchFrames;
			}
			TakeRoomSnapshot(roomFlags, g->curRoomId_4FAC, rs);
			roomEnteredFrame = frame;
			return;
		}
		DiffRoomFlags(roomFlags, rs);
	}

	// A hidden item that appeared in `room` and hasn't been picked up yet (its found flag is still set)
	bool TakeRevealCredit(uint16_t room, int* bitOut)
	{
		auto& credits = revealed[room];
		ROOM_SAVE_DATA* rs = RoomData ? RoomData->getRoomSavePtr(room) : nullptr;
		GLOBAL_WK* g = GlobalPtr();
		while (!credits.empty())
		{
			auto oldest = std::min_element(credits.begin(), credits.end(),
				[](const auto& a, const auto& b) { return a.second.frame < b.second.frame; });
			int bit = oldest->first;
			Reveal r = oldest->second;
			credits.erase(oldest);
			uint32_t mask = 0x80000000u >> (bit % 32);
			// gone after a death/reload rolled the room back: not a credit any more
			if (rs && !(rs->item_find_flg_18[bit / 32] & mask))
				continue;
			// a container's contents only count during the visit they appeared in (left behind, they're gone);
			// a knocked-down item only until its taken flag is set
			if (r.container && !(g && g->curRoomId_4FAC == room && r.frame >= roomEnteredFrame))
				continue;
			if (!r.container && rs && (rs->item_flg_8[bit / 32] & mask))
				continue;
			*bitOut = bit;
			return true;
		}
		return false;
	}

	void HandleNewItem(uint16_t id, uint32_t count, int goldDelta, const std::vector<cItem*>& fresh)
	{
		GLOBAL_WK* g = GlobalPtr();
		uint16_t room = g->curRoomId_4FAC;
		// Merchant: the menu was open just now, or the item cost money (only purchases take pesetas), or the free
		// Punisher next to the Merchant (blue medallion reward). Anything else is a pickup, even right next to him.
		bool menu = lastShopCtxFrame && (frame - lastShopCtxFrame) <= kShopWindowFrames;
		bool nearMerchant = lastMerchantNearFrame && (frame - lastMerchantNearFrame) <= kShopWindowFrames;
		bool reward = id == 33 && goldDelta == 0 && (menu || nearMerchant);
		bool shop = menu || goldDelta < 0 || reward;
		bool pickup = lastPickupCtxFrame && (frame - lastPickupCtxFrame) <= kPickupWindowFrames;
		{
			bool flagged = lastItemFlagFrame && (frame - lastItemFlagFrame) <= kPickupWindowFrames;
			Log(std::string("[pickup] ") + ItemName(id) + " x" + std::to_string(count) + " r" + Hex(room) +
				(shop ? " shop" : pickup ? " pickup-screen" : " no-screen") + (flagged ? " placed-item-flag" : "") +
				(goldDelta ? " gold " + std::to_string(goldDelta) : ""));
		}

		lastAnyPickupFrame = frame;

		// 1. a received item that went through the "case full" screen
		if (pendingGrant.active && pendingGrant.id == id)
		{
			SetAppliedIndex(pendingGrant.index + 1);
			Send({ {"cmd", "received"}, {"received", pendingGrant.index + 1} });
			pendingGrant = {};
			return;
		}

		// 2. bottle caps
		if (id >= 220 && id <= 243)
		{
			for (auto& loc : locations)
				if (loc.kind == Kind::BottleCap && HasItem(loc, id) && !EventDone(loc))
					SendCheck(loc, std::string("won cap ") + ItemName(id));
			return;
		}

		// 3. Merchant. Like pickups, "first time" means first time in this save: after a death or an older save the
		//    purchase sends the (already known) check again and the item is taken back again.
		if (shop)
		{
			bool checked = false;
			if (reward)
			{
				for (auto& loc : locations)
					if (loc.kind == Kind::MedallionReward && !SaveBit(loc.offset))
					{
						SendCheck(loc, "medallion reward");
						checked = true;
					}
			}
			else if (id >= 125 && id <= 127)
			{
				bool sizeJustGrew = lastCaseGrowFrame && frame - lastCaseGrowFrame <= kShopWindowFrames;
				lastCaseItemFrame = frame; // the case size grows with the same purchase: one check, not two
				checked = sizeJustGrew ? false : CheckCasePurchase(id);
			}
			else
			{
				for (auto& loc : locations)
					if (loc.kind == Kind::Merchant && HasItem(loc, id) && !SaveBit(loc.offset))
					{
						SendCheck(loc, std::string("bought ") + ItemName(id));
						checked = true;
						break;
					}
			}
			// buying back something Leon just sold here (e.g. to free a weapon's purchase check): he keeps it
			bool rebuy = soldToMerchant.erase(id) > 0;
			// check-only mode: the item itself is shuffled into the multiworld, so take this copy back once the
			// shop closes. Kept: the tactical vest (changes Leon's costume on purchase) and the stocks (attach to
			// the gun right away, so there's no item left to take).
			bool keeps = id == uint16_t(EItemId::Assault_Jacket) || id == uint16_t(EItemId::Stock_Mauser) ||
				id == uint16_t(EItemId::Stock_Styer) || (id >= 124 && id <= 127); // attache cases always take effect
			if (checked && merchantCheckOnly && !keeps && !rebuy)
			{
				pendingRemovals.push_back({ id, 1, fresh });
				AddToast(std::string("Merchant check: ") + ItemName(id) + " goes to the multiworld");
			}
			return;
		}

		// 4. consumables (one pickup = one event, whatever the amount). Decided once we know whether it was a
		//    placed item or an enemy drop, see ResolveSupplies.
		if (IsConsumableType(id))
		{
			if (pickup)
			{
				SupplyPickup p;
				p.id = id;
				p.count = count;
				p.screen = true;
				p.fresh = fresh;
				p.room = room;
				p.frame = frame;
				pendingSupplies.push_back(std::move(p));
			}
			return;
		}

		// 5. world pickups (or cutscene/puzzle rewards). Treasures and key items usually reach the case before
		//    their pickup screen opens, so the screen isn't required; a treasure or key item that just went away
		//    means this one was combined from others in the inventory screen.
		bool combined = lastCombineFrame && (frame - lastCombineFrame) <= 5;
		if (!combined)
		{
			SupplyPickup other; // lets this pickup claim its own room flag, so a drop picked up next to it can't
			other.other = true;
			other.id = id;
			other.room = room;
			other.frame = frame;
			pendingSupplies.push_back(std::move(other));
		}
		uint32_t removeCount = 0;
		for (uint32_t i = 0; i < count; i++)
			if (HandlePickup(id, room, combined))
				removeCount++;
		if (removeCount)
			pendingRemovals.push_back({ id, removeCount, fresh });
	}

	// The Merchant only sells the next case size, and received Progressive Attache Cases skip sizes: any case
	// purchase takes the first case location not yet checked
	bool CheckCasePurchase(uint16_t caseItem)
	{
		LocationDef* best = nullptr;
		int bestItem = 999;
		for (auto& loc : locations)
		{
			if (loc.kind != Kind::Merchant || SaveBit(loc.offset))
				continue;
			for (int i : loc.items)
				if (i >= 125 && i <= 127 && i < bestItem)
				{
					best = &loc;
					bestItem = i;
				}
		}
		if (!best)
			return false;
		SendCheck(*best, std::string("bought ") + ItemName(caseItem));
		return true;
	}

	void DiffInventory()
	{
		GLOBAL_WK* g = GlobalPtr();
		std::unordered_map<uint16_t, uint32_t> cur;
		std::unordered_set<cItem*> curPtrs;
		TakeSnapshot(cur, curPtrs);
		int gold = g->goldAmount_4FA8;
		int caseSize = SubScreenWk->board_size_2AA;

		if (resetSnapshot)
		{
			prevInv = std::move(cur);
			prevPtrs = std::move(curPtrs);
			prevGold = gold;
			prevCaseSize = caseSize;
			resetSnapshot = false;
			return;
		}

		itemDecreasedNow = false;
		for (auto& [id, num] : prevInv)
		{
			auto c = cur.find(id);
			if (c == cur.end() || c->second < num)
			{
				itemDecreasedNow = true;
				if (IsInterestingType(id))
				{
					lastCombineFrame = frame;
					if (lastShopCtxFrame && frame - lastShopCtxFrame <= kShopWindowFrames)
						soldToMerchant.insert(id);
				}
			}
		}

		int goldDelta = gold - prevGold;
		bool menu = lastShopCtxFrame && (frame - lastShopCtxFrame) <= kShopWindowFrames;
		bool nearMerchant = lastMerchantNearFrame && (frame - lastMerchantNearFrame) <= kShopWindowFrames;
		bool sale = menu || (nearMerchant && itemDecreasedNow);
		if (goldDelta > 0 && !sale)
		{
			SupplyPickup p;
			p.gold = true;
			p.count = uint32_t(goldDelta);
			p.room = g->curRoomId_4FAC;
			p.frame = frame;
			pendingSupplies.push_back(std::move(p));
		}
		for (auto& [id, num] : cur)
		{
			uint32_t before = 0;
			auto it = prevInv.find(id);
			if (it != prevInv.end())
				before = it->second;
			if (num <= before)
				continue;
			std::vector<cItem*> fresh;
			for (cItem* p : curPtrs)
				if (!prevPtrs.count(p) && uint16_t(p->id_0) == id)
					fresh.push_back(p);
			HandleNewItem(id, num - before, goldDelta, fresh);
		}

		// Attache case bought from the Merchant (case size changes instead of an item appearing)
		// (the item path runs first in this same function, so a same-frame case item is already recorded)
		bool caseItemJustNow = lastCaseItemFrame && frame - lastCaseItemFrame <= kShopWindowFrames;
		if (caseSize > prevCaseSize && prevCaseSize >= 0 && (menu || nearMerchant || goldDelta < 0) && !caseItemJustNow)
		{
			lastCaseGrowFrame = frame;
			CheckCasePurchase(uint16_t(124 + caseSize));
		}

		prevInv = std::move(cur);
		prevPtrs = std::move(curPtrs);
		prevGold = gold;
		prevCaseSize = caseSize;
	}

	void ProcessRemovals()
	{
		if (pendingRemovals.empty() || !SafeForInventory())
			return;
		std::vector<Removal> retry;
		for (auto& r : pendingRemovals)
			if (uint32_t left = RemoveItem(r))
				retry.push_back({ r.id, left, {} });
		pendingRemovals = std::move(retry);
		if (!pendingRemovals.empty() && !warnedEquipped)
		{
			warnedEquipped = true;
			AddToast(std::string("Switch away from the ") + ItemName(pendingRemovals[0].id) +
				": it was a check and goes to the multiworld");
		}
		if (pendingRemovals.empty())
			warnedEquipped = false;
		Resnapshot();
	}

	// =============================================================================== bosses
	void DefeatedBoss(uint8_t emId, uint16_t room)
	{
		Log("Boss defeated: em " + Hex(emId) + " in room r" + Hex(room));

		if (emId == 0x31 || emId == 0x3F) // Saddler: only counts in his arena
		{
			if (room == kRoomSaddler)
				ReportGoal();
			return;
		}

		const LocationDef* fallback = nullptr;
		for (auto& loc : locations)
		{
			if (loc.kind != Kind::Boss || loc.em != emId || EventDone(loc))
				continue;
			if (loc.room == room)
			{
				SendCheck(loc, "boss");
				return;
			}
			if (!fallback && (loc.room >> 8) == (room >> 8))
				fallback = &loc;
		}
		if (fallback)
		{
			SendLog("Boss em " + Hex(emId) + " in room r" + Hex(room) + " matched by type, room data may be off");
			SendCheck(*fallback, "boss");
		}
	}

	// Before boss tracking is dropped (room change, cutscene/load): a boss last seen dead or nearly dead counts
	void FinalBossScan(uint16_t room)
	{
		GLOBAL_WK* g = GlobalPtr();
		cEmMgr* mgr = EmMgrPtr();
		bool alive = g && g->playerHpCur_4FB4 > 0 && !Status(Flags_STATUS::STA_DIEDEMO);
		if (alive && configured)
		{
			for (auto& [idx, t] : trackedBosses)
			{
				int hp = t.lastHp;
				bool dying = t.dying;
				if (mgr && mgr->m_Array_4 && idx < mgr->m_nArray_8)
				{
					cEm* em = mgr->get(idx);
					if (em->IsValid() && em->guid_F8 == t.guid)
					{
						hp = em->hp_324;
						dying |= em->r_no_0_FC == uint8_t(cEm::Routine0::Die);
					}
				}
				if (hp <= 0 || dying)
				{
					Log("Boss em " + Hex(t.id) + " at " + std::to_string(hp) + " HP when tracking stopped");
					DefeatedBoss(t.id, room);
				}
			}
		}
		trackedBosses.clear();
	}

	void TrackBosses()
	{
		cEmMgr* mgr = EmMgrPtr();
		GLOBAL_WK* g = GlobalPtr();
		if (!mgr || !mgr->m_Array_4)
			return;
		uint16_t room = g->curRoomId_4FAC;

		// A boss counts only when it is seen with HP <= 0 while still valid (not when it is despawned)
		for (auto it = trackedBosses.begin(); it != trackedBosses.end();)
		{
			if (it->first >= mgr->m_nArray_8)
			{
				it = trackedBosses.erase(it);
				continue;
			}
			cEm* em = mgr->get(it->first);
			bool gone = !em->IsValid() || em->guid_F8 != it->second.guid;
			bool alive = g->playerHpCur_4FB4 > 0 && !Status(Flags_STATUS::STA_DIEDEMO);
			if (gone)
			{
				// removed by its death cutscene before HP 0 was seen: count it if it was in its dying routine
				const TrackedEm& t = it->second;
				if (alive && (t.dying || t.lastHp <= 0))
				{
					Log("Boss em " + Hex(t.id) + " vanished while dying (" + std::to_string(t.lastHp) + "/" +
						std::to_string(t.maxHp) + " HP)");
					DefeatedBoss(t.id, room);
				}
				it = trackedBosses.erase(it);
			}
			else if (em->hp_324 <= 0 && alive)
			{
				DefeatedBoss(it->second.id, room);
				it = trackedBosses.erase(it);
			}
			else
			{
				it->second.lastHp = em->hp_324;
				if (em->r_no_0_FC == uint8_t(cEm::Routine0::Die) && !it->second.dying)
				{
					it->second.dying = true;
					Log("Boss em " + Hex(it->second.id) + " entered its dying routine at " + std::to_string(em->hp_324) + " HP");
				}
				++it;
			}
		}

		for (uint32_t i = 0; i < mgr->m_nArray_8; i++)
		{
			cEm* em = mgr->get(i);
			if (!em->IsValid() || !bossEmIds.count(em->id_100) || em->hp_324 <= 0)
				continue;
			if (!trackedBosses.count(i))
				trackedBosses[i] = { em->guid_F8, em->id_100, em->hp_324, em->hp_max_326 };
		}
	}

	// Research aid: in the farm and graveyard (blue medallion rooms), log every object/enemy that dies or
	// vanishes so the medallions' em id can be identified from a play session.
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
			cEm* em = it->first < mgr->m_nArray_8 ? mgr->get(it->first) : nullptr;
			bool gone = !em || !em->IsValid() || em->guid_F8 != it->second.guid;
			if (gone || em->hp_324 <= 0)
			{
				Log("[discovery] r" + Hex(room) + ": em " + Hex(it->second.id) + " type " +
					std::to_string(gone ? -1 : int(em->type_101)) + (gone ? " vanished" : " died"));
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
	// Returns false if the item is waiting in the "case full" screen (index advances once it lands)
	bool ApplyItem(const ReceivedItem& item, uint32_t index)
	{
		GLOBAL_WK* g = GlobalPtr();
		auto defIt = itemDefs.find(item.id);
		std::string label = item.name.empty() ? std::to_string(item.id) : item.name;
		std::string toast = item.from.empty() ? ("Received " + label) : ("Received " + label + " from " + item.from);
		if (defIt == itemDefs.end())
		{
			if (warnedUnknownIndex != index)
			{
				warnedUnknownIndex = index;
				SendLog("Unknown item id " + std::to_string(item.id) + " - the mod and the APWorld versions don't match");
				AddToast("Archipelago: unknown item received. Update the mod and the APWorld to the same release.");
			}
			return false; // hold it rather than lose it
		}
		const ItemDef& def = defIt->second;
		bool done = true;

		if (def.kind == "pesetas")
		{
			g->goldAmount_4FA8 = std::min(g->goldAmount_4FA8 + def.amount, 999999);
			bio4::SndCall(0, 0x10, 0, 0, 0, 0);
		}
		else if (def.kind == "attache_case")
		{
			int next = SubScreenWk->board_size_2AA + 1;
			if (next <= 3)
				InventoryItemAdd(ITEM_ID(124 + next), 1, false, true);
			else
				g->goldAmount_4FA8 = std::min(g->goldAmount_4FA8 + 10000, 999999);
		}
		else if (def.kind == "game" && def.game >= 0)
		{
			ITEM_INFO info;
			bio4::itemInfo(ITEM_ID(def.game), &info);
			uint32_t count = (info.maxNum_4 <= 1 || info.defNum_3 == 0) ? 1 : info.defNum_3;
			uint16_t gid = uint16_t(def.game);
			uint32_t before = CountOf(gid);
			InventoryItemAdd(ITEM_ID(def.game), count, false, true);
			if (CountOf(gid) <= before)
			{
				// InventoryItemAdd sets get_item_id right away; the organize screen may open a frame later
				if (SubScreenWk->get_item_id_2F6 == gid || (SubScreenWk->open_flag_2C & SS_OPEN_PZZL))
				{
					// case full: the game shows the organize screen and adds the item when the player places it
					pendingGrant = { true, gid, index, 0 };
					done = false;
				}
				else if ((info.type_2 == ITEM_TYPE_KEY_ITEM || info.type_2 == ITEM_TYPE_IMPORTANT) && before == 0)
				{
					// never lose a key item: try again in a few seconds (a key item Leon already holds can't stack,
					// so that one counts as delivered)
					if (refusedIndex != index)
					{
						refusedTries = 0;
						SendLog("Key item " + label + " could not be added yet; retrying");
						AddToast("Couldn't add " + label + " yet, trying again...");
					}
					refusedIndex = index;
					refusedFrame = frame;
					Resnapshot();
					if (++refusedTries < 30)
						return false;
					SendLog("Gave up adding " + label + " after 30 tries. Make room, then ask the host to type: /send <your name> " + label);
					AddToast("Couldn't add " + label + ". Make room, then ask the host to /send it to you again.");
				}
				else
					SendLog("Item " + label + " could not be added (game refused it)");
			}
		}

		prevGold = g->goldAmount_4FA8;
		prevCaseSize = SubScreenWk->board_size_2AA;
		Resnapshot();
		AddToast(toast);
		Log("[received] #" + std::to_string(index) + " " + label + (done ? "" : " (organize screen)"));
		return done;
	}

	// Items are waiting but the game isn't in a state where the inventory can be touched: after 15 s of that,
	// write down why once (to find any game state that blocks delivery for too long)
	void LogDeliveryBlocked()
	{
		if (AppliedIndex() >= received.size())
		{
			deliveryWaitSince = {};
			return;
		}
		auto now = std::chrono::steady_clock::now();
		if (deliveryWaitSince == std::chrono::steady_clock::time_point{})
			deliveryWaitSince = now;
		if (deliveryWaitLogged || now - deliveryWaitSince < std::chrono::seconds(15))
			return;
		deliveryWaitLogged = true;
		GLOBAL_WK* g = GlobalPtr();
		std::string why;
		auto add = [&](bool on, const char* name) { if (on) why += std::string(" ") + name; };
		add(!InMainLoop(), "not-main-loop");
		add(!IsLeon(), "not-leon");
		add(OptionOpenFlag(), "options");
		add(SubScreenWk && SubScreenWk->open_flag_2C != SS_OPEN_NULL, ("screen=" + Hex(SubScreenWk ? int(SubScreenWk->open_flag_2C) : 0)).c_str());
		add(SubScreenWk && SubScreenWk->item_get_flag_40, "item-get");
		add(g && g->playerHpCur_4FB4 <= 0, "dead");
		add(Status(Flags_STATUS::STA_ITEM_GET), "STA_ITEM_GET");
		add(Status(Flags_STATUS::STA_SUB_SCRN), "STA_SUB_SCRN");
		add(Status(Flags_STATUS::STA_SSCRN_REQUEST), "STA_SSCRN_REQUEST");
		add(Status(Flags_STATUS::STA_EVENT), "STA_EVENT");
		add(Status(Flags_STATUS::STA_MOVIE_ON), "STA_MOVIE_ON");
		add(Status(Flags_STATUS::STA_MOVIE2_ON), "STA_MOVIE2_ON");
		add(Status(Flags_STATUS::STA_DIEDEMO), "STA_DIEDEMO");
		add(Status(Flags_STATUS::STA_NOW_LOADING), "STA_NOW_LOADING");
		add(Status(Flags_STATUS::STA_SHOOTING), "STA_SHOOTING");
		cPlayer* pl = PlayerPtr();
		add(pl && !pl->subScrCheck(), "player-busy");
		Log("[delivery] items waiting for 15 s in r" + Hex(g ? g->curRoomId_4FAC : 0) + ", blocked by:" + (why.empty() ? " ?" : why));
	}

	void ApplyReceivedItems()
	{
		if (pendingGrant.active)
		{
			// organize screen closed but the item never landed: the player left it behind
			if (SafeForInventory())
			{
				if (!pendingGrant.closedSince)
					pendingGrant.closedSince = frame;
				else if (frame - pendingGrant.closedSince > kGrantGiveUpFrames)
				{
					Log("Received item was left behind in the organize screen");
					SetAppliedIndex(pendingGrant.index + 1);
					Send({ {"cmd", "received"}, {"received", pendingGrant.index + 1} });
					pendingGrant = {};
				}
			}
			else
				pendingGrant.closedSince = 0;
			return;
		}

		if (!SafeForInventory())
		{
			LogDeliveryBlocked();
			return;
		}
		deliveryWaitSince = {};
		deliveryWaitLogged = false;
		if (frame - lastGrantFrame < kGrantCooldownFrames)
			return;
		uint32_t index = AppliedIndex();
		if (index >= received.size())
			return;
		if (index == refusedIndex && frame - refusedFrame < 300)
			return;
		// found at a location that holds its own vanilla item: Leon kept that copy, so nothing to deliver
		{
			const ReceivedItem& r = received[index];
			if (r.own)
				for (auto& loc : locations)
					if (loc.keep && loc.id == r.loc)
					{
						SetAppliedIndex(index + 1);
						Send({ {"cmd", "received"}, {"received", index + 1} });
						return;
					}
		}
		lastGrantFrame = frame;
		if (ApplyItem(received[index], index))
		{
			SetAppliedIndex(index + 1);
			Send({ {"cmd", "received"}, {"received", index + 1} });
		}
	}

	// =============================================================================== death link
	void UpdateDeath()
	{
		GLOBAL_WK* g = GlobalPtr();
		bool dead = g->playerHpCur_4FB4 <= 0 || Status(Flags_STATUS::STA_DIEDEMO);
		if (dead && !wasDead)
		{
			Log("[death] received index " + std::to_string(AppliedIndex()) + ", gold " + std::to_string(g->goldAmount_4FA8));
			loggedContinue = false;
			pendingRemovals.clear(); // the game is about to roll back to the last checkpoint
			pendingGrant = {};
			pendingSupplies.clear();
			undecidedSupplies.clear();
			ResetRoomWatch();
			if (deathLink && frame - lastKillFrame > 600)
				Send({ {"cmd", "death"} });
		}
		wasDead = dead;
		if (!dead && !loggedContinue)
		{
			// the first frame back after a death: shows whether the checkpoint restored the save work too
			loggedContinue = true;
			Log("[continue] received index " + std::to_string(AppliedIndex()) + ", gold " + std::to_string(g->goldAmount_4FA8));
		}

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
		if (k == "bottle_cap") return Kind::BottleCap;
		if (k == "bonus") return Kind::Bonus;
		if (k == "pesetas") return Kind::Pesetas;
		if (k == "drop") return Kind::Drop;
		return Kind::Unknown;
	}

	void ResetSessionState()
	{
		serverChecked.clear();
		pendingChecks.clear();
		pendingRemovals.clear();
		received.clear();
		trackedBosses.clear();
		pendingGrant = {};
		pendingSupplies.clear();
		undecidedSupplies.clear();
		revealed.clear();
		ResetRoomWatch();
		goalReported = false;
		saveState = 0;
		resetSnapshot = true;
	}

	bool HandleConfig(const json& msg, bool fromDisk)
	{
		if (!msg.contains("slot_data") || !msg["slot_data"].is_object() || !msg["slot_data"].value("locations", json()).is_array())
		{
			Log("Ignored config without valid slot_data");
			return false;
		}
		const json& sd = msg["slot_data"];

		// parse into locals first; only replace the active config if everything parsed
		std::vector<LocationDef> newLocations;
		std::unordered_map<int64_t, ItemDef> newItems;
		std::unordered_set<int> newBossIds = { 0x31, 0x3F };
		int64_t newBase = sd.value("location_base", int64_t(0));
		for (auto& l : sd.value("locations", json::array()))
		{
			LocationDef d;
			d.id = l.value("id", int64_t(0));
			d.offset = int(d.id - newBase);
			d.kind = ParseKind(l.value("k", std::string()));
			d.room = l.value("room", -1);
			d.em = l.value("em_id", -1);
			d.loose = l.value("loose", 0) != 0;
			d.cut = l.value("cut", 0) != 0;
			d.consumable = l.value("c", 0) != 0;
			d.stage = l.value("stage", 0);
			d.keep = l.value("keep", 0) != 0;
			d.excluded = l.value("x", 0) != 0;
			for (auto& i : l.value("items", json::array()))
				d.items.push_back(i.get<int>());
			if (d.offset < 0 || d.offset >= kBitSlots * 32)
			{
				Log("Location offset out of range: " + std::to_string(d.id));
				continue;
			}
			if (d.kind == Kind::Boss && d.em >= 0)
				newBossIds.insert(d.em);
			newLocations.push_back(std::move(d));
		}
		for (auto& i : sd.value("items", json::array()))
		{
			ItemDef d;
			d.kind = i.value("k", std::string());
			d.game = i.value("g", -1);
			d.amount = i.value("amount", 0);
			newItems[i.value("id", int64_t(0))] = d;
		}

		uint32_t newTag = msg.value("save_tag", uint32_t(0));
		if (newTag != saveTag)
		{
			ResetSessionState();
			saveTag = newTag;
		}
		locations = std::move(newLocations);
		itemDefs = std::move(newItems);
		bossEmIds = std::move(newBossIds);
		locationBase = newBase;
		deathLink = sd.value("death_link", false);
		merchantCheckOnly = sd.value("merchant_check_only", false);
		configFromDisk = fromDisk;
		enemyHpMin = enemyHpMax = 0.0f;
		{
			const json& hp = sd.value("enemy_health", json());
			if (hp.is_array() && hp.size() == 2 && hp[0].is_number() && hp[1].is_number())
			{
				enemyHpMin = std::clamp(hp[0].get<float>(), 0.1f, 15.0f);
				enemyHpMax = std::clamp(hp[1].get<float>(), enemyHpMin, 15.0f);
			}
		}
		configured = true;
		Log("Configured: " + std::to_string(locations.size()) + " locations, slot " + msg.value("slot", std::string()) +
			(fromDisk ? " (from last session)" : ""));
		if (!fromDisk)
		{
			AddToast("Archipelago: connected as " + msg.value("slot", std::string()));
			// remember it, so pickups made before the client connects next time are still tracked
			try
			{
				std::ofstream f(configFile, std::ios::trunc);
				f << msg.dump(-1, ' ', false, json::error_handler_t::replace);
			}
			catch (...) {}
		}
		return true;
	}

	// Re-send everything this save has collected that the server hasn't confirmed (e.g. done while offline)
	void ResendFromSave()
	{
		if (!configured || saveState != 1 || configFromDisk)
			return;
		std::vector<int64_t> missing;
		for (auto& loc : locations)
			if (SaveBit(loc.offset) && !serverChecked.count(loc.id))
				missing.push_back(loc.id);
		if (!missing.empty())
			Send({ {"cmd", "check"}, {"locations", missing}, {"tag", saveTag} });
		if (uint32_t* w = SaveWork())
			if (w[kSlotFlags] & kFlagGoal)
				Send({ {"cmd", "goal"}, {"tag", saveTag} });
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
			std::string cmd;
			try
			{
				cmd = msg.value("cmd", std::string());
				if (cmd == "config")
				{
					if (HandleConfig(msg, msg.value("from_disk", false)))
						ResendFromSave();
				}
				else if (cmd == "items")
				{
					received.clear();
					for (auto& it : msg.value("items", json::array()))
						received.push_back({ it.value("id", int64_t(0)), it.value("name", std::string()), it.value("from", std::string()),
							it.value("loc", int64_t(-1)), it.value("own", false) });
				}
				else if (cmd == "checked")
				{
					serverChecked.clear();
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
					uint32_t* w = SaveWork();
					bool alreadyLinked = w && w[kSlotMagic] == kSaveMagic && w[kSlotTag] == saveTag;
					if (alreadyLinked)
						AddToast("This save is already linked to this seed");
					else if (configFromDisk)
						AddToast("Connect the client to the room first, then /bindsave");
					else if (IsEasyMode())
						AddToast("Easy cuts rooms that hold checks: play on Normal or Professional");
					else if (configured && InMainLoop() && IsMainGame())
					{
						BindSave();
						saveState = 0;
						resetSnapshot = true;
					}
				}
			}
			catch (const std::exception& e)
			{
				Log(std::string("Error handling message '") + cmd + "': " + e.what());
			}
		}
	}

	// Village -> castle and castle -> island are one-way: say how many item checks were left behind
	void WarnLeftBehind(int stage)
	{
		int left = 0;
		for (auto& loc : locations)
			if (!loc.excluded && !EventDone(loc) &&
				((loc.kind == Kind::Pickup && !loc.consumable && loc.room >= 0 && (loc.room >> 8) == stage) ||
				 (loc.kind == Kind::Drop && loc.stage == stage)))
				left++;
		if (!left)
			return;
		const char* names[] = { "", "village", "castle", "island" };
		std::string where = stage >= 1 && stage <= 3 ? names[stage] : "last area";
		Log(std::to_string(left) + " item checks left behind in the " + where);
		AddToast(std::to_string(left) + " item checks left behind in the " + where +
			". /missing in the client lists them; F1 > Trainer > Area Jump goes back.");
	}

	void SendHello()
	{
		uint32_t* w = SaveWork();
		bool linked = w && w[kSlotMagic] == kSaveMagic;
		Send({ {"cmd", "hello"}, {"version", kProtocolVersion}, {"mod_version", kModVersion},
			{"save_tag", linked ? w[kSlotTag] : 0},
			{"received", linked ? int(w[kSlotIndex]) : -1} });
	}

	void Tick()
	{
		// "world time": doesn't run while loading a room, in Options or at the title, so pickups and their flags
		// stay close together in frames even when a door or a menu comes between them
		if (InMainLoop() && !OptionOpenFlag())
			frame++;
		FlushConsole();
		ProcessInbox();

		if (clientJustConnected.exchange(false))
			SendHello();

		GLOBAL_WK* g = GlobalPtr();
		if (!configured || !InMainLoop() || !IsMainGame())
		{
			uiRoomChecks = 0;
			uiRoomSupplies = 0;
			resetSnapshot = true;
			if (!trackedBosses.empty() && !(g && g->Rno0_20 == uint8_t(GLOBAL_WK::Routine0::Option)))
				FinalBossScan(prevRoom);
			discoveryEms.clear();
			// Title screen / new load: anything queued belongs to a game state that no longer exists
			if (!g || g->Rno0_20 == uint8_t(GLOBAL_WK::Routine0::Init) || g->Rno0_20 == uint8_t(GLOBAL_WK::Routine0::StageInit))
			{
				pendingRemovals.clear();
				pendingGrant = {};
				pendingSupplies.clear();
				ResetRoomWatch();
				prevRoom = 0xFFFF; // a loaded save isn't "walking" into its room (no left-behind warning)
				saveState = 0;
			}
			return;
		}

		if (g->curRoomId_4FAC != prevRoom)
		{
			if (!trackedBosses.empty())
				FinalBossScan(prevRoom);
			uint16_t oldRoom = prevRoom;
			prevRoom = g->curRoomId_4FAC;
			if (oldRoom != 0xFFFF && (prevRoom >> 8) > (oldRoom >> 8) && saveState == 1)
				WarnLeftBehind(oldRoom >> 8);
		}

		int oldState = saveState;
		if (!CheckSaveBinding())
		{
			resetSnapshot = true;
			return;
		}
		if (oldState != 1)
			ResendFromSave();

		if (prevRoom == kRoomJetski)
		{
			uint32_t* w = SaveWork();
			if (w && !(w[kSlotFlags] & kFlagGoal))
				ReportGoal();
		}

		WatchRoomFlags();
		if (!IsLeon())
		{
			resetSnapshot = true; // Ashley's segment: her pickups stay vanilla
			return;
		}

		if (PickupContext())
			lastPickupCtxFrame = frame;
		if (ShopContext())
			lastShopCtxFrame = frame;
		if (Status(Flags_STATUS::STA_INTO_SHOP))
			lastMerchantNearFrame = frame;

		DiffInventory();
		ResolveSupplies();
		if (frame % 30 == 0)
		{
			int items = 0, supplies = 0;
			for (auto& loc : locations)
				if (loc.kind == Kind::Pickup && !loc.excluded && loc.room == int(g->curRoomId_4FAC) && !EventDone(loc))
					(loc.consumable ? supplies : items)++;
			uiRoomChecks = items;
			uiRoomSupplies = supplies;
		}
		ProcessRemovals();
		TrackBosses();
		DiscoveryLog();
		UpdateDeath();
		ApplyReceivedItems();
		FlushChecks();
	}
}

// =================================================================================== public API
bool Archipelago_EnemyHP(float* minMul, float* maxMul)
{
	if (!ap::configured || ap::enemyHpMax <= 0.0f)
		return false;
	*minMul = ap::enemyHpMin;
	*maxMul = ap::enemyHpMax;
	return true;
}

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
	GLOBAL_WK* g = GlobalPtr();
	bool inGame = g && g->Rno0_20 == uint8_t(GLOBAL_WK::Routine0::MainLoop) && g->curRoomId_4FAC < 0x400;
	bool showStatus = inGame && status != 2;
	int roomChecks = (inGame && status == 2) ? ap::uiRoomChecks.load() : 0;
	int roomSupplies = (inGame && status == 2) ? ap::uiRoomSupplies.load() : 0;
	if (lines.empty() && !showStatus && roomChecks <= 0 && roomSupplies <= 0)
		return;

	ImGui::SetNextWindowPos(ImVec2(16, 16), ImGuiCond_Always);
	ImGui::SetNextWindowBgAlpha(0.45f);
	ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs |
		ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
	if (ImGui::Begin("##archipelago", nullptr, flags))
	{
		if (showStatus)
		{
			const char* text =
				status == 0 ? "Archipelago: waiting for the RE4 UHD Client" :
				status == 1 ? "Archipelago: client connected, waiting for server" :
				status == 3 ? "Archipelago: this save belongs to another seed (/bindsave)" :
				status == 5 ? "Archipelago: Easy isn't supported - start a New Game on Normal or Professional" :
				"Archipelago: save not linked - start a New Game or /bindsave";
			ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "%s", text);
		}
		if (roomChecks > 0 || roomSupplies > 0)
			ImGui::TextColored(ImVec4(0.6f, 0.85f, 1.0f, 0.8f), "Archipelago checks in this area: %d items, %d ammo/herbs",
				roomChecks, roomSupplies);
		for (auto& [text, alpha] : lines)
			ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, alpha), "%s", text.c_str());
	}
	ImGui::End();
}

void re4t::init::Archipelago()
{
	ap::logFile = std::filesystem::path(rootPath) / "re4_tweaks" / "archipelago.log";
	ap::configFile = std::filesystem::path(rootPath) / "re4_tweaks" / "archipelago_config.json";
	ap::learnedFile = std::filesystem::path(rootPath) / "re4_tweaks" / "archipelago_learned.json";
	try
	{
		std::filesystem::create_directories(ap::logFile.parent_path());
	}
	catch (...) {}

	// Load the last seed's config so pickups are tracked even before the client connects
	try
	{
		std::ifstream f(ap::configFile);
		if (f)
		{
			json cfg = json::parse(f);
			if (cfg.is_object())
			{
				cfg["from_disk"] = true;
				std::lock_guard<std::mutex> lock(ap::inboxMutex);
				ap::inbox.push_back(std::move(cfg));
			}
		}
	}
	catch (...) {}

	try
	{
		std::ifstream f(ap::learnedFile);
		if (f && json::parse(f).value("room_flags", false))
			ap::flagMode = ap::FlagMode::Flags;
	}
	catch (...) {}

	std::thread(ap::NetThread).detach();
	std::thread(ap::SenderThread).detach();
	spd::log()->info("Archipelago module initialized");
}
