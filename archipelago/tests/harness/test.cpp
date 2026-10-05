#include <filesystem>
// Behavioral test harness for dllmain/Archipelago.cpp.
// The module source is included unmodified so the test can inspect ap:: internals;
// it is driven only through re4t::init::Archipelago(), Archipelago_Tick(), Archipelago_Render()
// and a fake Archipelago client on 127.0.0.1:46400.
#include "Archipelago.cpp"
#include "sim.h"

#include <cstdio>
#include <fstream>
#include <functional>
#include <set>
#include <sstream>
#include <iterator>

// ---------------------------------------------------------------- ids
constexpr int64_t LB = 7741000; // location base
#include "ids.inc"
constexpr int64_t I_SHOTGUN = 7740028, I_RIFLE = 7740037, I_TMP = 7740039, I_HANDGUN_AMMO = 7740048,
	I_P1000 = 7740098, I_P5000 = 7740099, I_CASE = 7740102, I_SPINEL = 7740062;

// ---------------------------------------------------------------- fake client
struct FakeClient
{
	SOCKET s = INVALID_SOCKET;
	std::string buf;
	std::vector<json> got;             // every message received from the game
	std::set<int64_t> checked;         // server-side checked locations
	bool autoRespond = true;           // behave like client.py (hello -> config/items/checked, check -> checked)
	json config;                       // last config to send on hello
	json items = json::array();

	void sendRaw(const std::string& line)
	{
		::send(s, line.data(), int(line.size()), 0);
	}
	void sendMsg(const json& m) { sendRaw(m.dump() + "\n"); }
} cl;

json slotData;
int failures = 0, passes = 0;
std::vector<std::string> results;

void WaitInbox(size_t n)
{
	for (int i = 0; i < 400; i++)
	{
		{
			std::lock_guard<std::mutex> lock(ap::inboxMutex);
			if (ap::inbox.size() >= n)
				return;
		}
		Sleep(5);
	}
	printf("  (timeout waiting for %u inbox messages)\n", unsigned(n));
}

// send valid messages and wait until the net thread has queued them
void Deliver(const std::vector<json>& msgs)
{
	size_t before;
	{
		std::lock_guard<std::mutex> lock(ap::inboxMutex);
		before = ap::inbox.size();
	}
	for (auto& m : msgs)
		cl.sendMsg(m);
	WaitInbox(before + msgs.size());
}

void SendChecked() { Deliver({ { {"cmd", "checked"}, {"locations", std::vector<int64_t>(cl.checked.begin(), cl.checked.end())} } }); }

void Drain(int waitMs = 0)
{
	if (cl.s == INVALID_SOCKET)
		return;
	auto start = GetTickCount();
	std::vector<json> responses;
	while (true)
	{
		char chunk[8192];
		int n = recv(cl.s, chunk, sizeof(chunk), 0);
		if (n > 0)
		{
			cl.buf.append(chunk, n);
			continue;
		}
		if (GetTickCount() - start >= DWORD(waitMs))
			break;
		Sleep(2);
	}
	size_t pos;
	while ((pos = cl.buf.find('\n')) != std::string::npos)
	{
		std::string line = cl.buf.substr(0, pos);
		cl.buf.erase(0, pos + 1);
		json m = json::parse(line, nullptr, false);
		if (m.is_discarded())
			continue;
		cl.got.push_back(m);
		if (!cl.autoRespond)
			continue;
		std::string cmd = m.value("cmd", "");
		if (cmd == "hello" && !cl.config.is_null())
		{
			responses.push_back(cl.config);
			responses.push_back({ {"cmd", "items"}, {"items", cl.items} });
			responses.push_back({ {"cmd", "checked"}, {"locations", std::vector<int64_t>(cl.checked.begin(), cl.checked.end())} });
		}
		else if (cmd == "check")
		{
			bool added = false;
			for (auto& id : m["locations"])
				added |= cl.checked.insert(id.get<int64_t>()).second;
			if (added)
				responses.push_back({ {"cmd", "checked"}, {"locations", std::vector<int64_t>(cl.checked.begin(), cl.checked.end())} });
		}
	}
	if (!responses.empty())
		Deliver(responses);
}

void Tick(int n = 1)
{
	for (int i = 0; i < n; i++)
	{
		sim::gameFrame();
		Archipelago_Tick();
		Archipelago_Render();
		Drain(1);
	}
}

void Connect()
{
	cl.s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	sockaddr_in a{};
	a.sin_family = AF_INET;
	a.sin_port = htons(46400);
	inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
	for (int i = 0; i < 100; i++)
	{
		if (connect(cl.s, (sockaddr*)&a, sizeof(a)) == 0)
			break;
		Sleep(20);
	}
	u_long nb = 1;
	ioctlsocket(cl.s, FIONBIO, &nb);
	for (int i = 0; i < 200 && !ap::clientJustConnected; i++)
		Sleep(5);
	cl.buf.clear();
}

void Disconnect()
{
	closesocket(cl.s);
	cl.s = INVALID_SOCKET;
	for (int i = 0; i < 200 && ap::clientConnected; i++)
		Sleep(5);
}

bool gMerchantCheckOnly = true; // default YAML: merchant_purchases: check_only
json Config(uint32_t tag, bool deathLink = true)
{
	json sd = slotData;
	sd["death_link"] = deathLink;
	sd["merchant_check_only"] = gMerchantCheckOnly;
	return { {"cmd", "config"}, {"seed", "S"}, {"slot", "Leon"}, {"save_tag", tag}, {"slot_data", sd} };
}

json Item(int64_t id, int i) { return { {"i", i}, {"id", id}, {"name", "item" + std::to_string(id)}, {"from", ""} }; }

// ---------------------------------------------------------------- query helpers
std::vector<int64_t> ChecksSince(size_t from)
{
	std::vector<int64_t> out;
	for (size_t i = from; i < cl.got.size(); i++)
		if (cl.got[i].value("cmd", "") == "check")
			for (auto& id : cl.got[i]["locations"])
				out.push_back(id.get<int64_t>());
	return out;
}
int CountCmdSince(size_t from, const std::string& cmd)
{
	int n = 0;
	for (size_t i = from; i < cl.got.size(); i++)
		if (cl.got[i].value("cmd", "") == cmd)
			n++;
	return n;
}
json LastCmd(const std::string& cmd)
{
	for (size_t i = cl.got.size(); i-- > 0;)
		if (cl.got[i].value("cmd", "") == cmd)
			return cl.got[i];
	return nullptr;
}
bool Has(const std::vector<int64_t>& v, int64_t id) { return std::find(v.begin(), v.end(), id) != v.end(); }
bool ConContains(size_t from, const std::string& s)
{
	auto& c = sim::conLines();
	for (size_t i = from; i < c.size(); i++)
		if (c[i].find(s) != std::string::npos)
			return true;
	return false;
}
bool RenderContains(const std::string& s)
{
	for (auto& l : sim::renderLines())
		if (l.find(s) != std::string::npos)
			return true;
	return false;
}

// ---------------------------------------------------------------- test bookkeeping
std::string curName;
bool curOk;
std::vector<std::string> curNotes;
void Begin(const std::string& name)
{
	curName = name;
	curOk = true;
	curNotes.clear();
	printf("=== %s\n", name.c_str());
}
void Expect(bool cond, const std::string& what)
{
	printf("  [%s] %s\n", cond ? "ok" : "FAIL", what.c_str());
	if (!cond)
	{
		curOk = false;
		curNotes.push_back(what);
	}
}
void End()
{
	(curOk ? passes : failures)++;
	std::string line = std::string(curOk ? "PASS  " : "FAIL  ") + curName;
	for (auto& n : curNotes)
		line += "\n        - " + n;
	results.push_back(line);
}

// New bound game state in `room` with a fresh seed tag; client connected and configured.
uint32_t nextTag = 0x1000;
uint32_t Setup(uint16_t room, bool bind = true, bool deathLink = true)
{
	uint32_t tag = ++nextTag;
	sim::reset(room);
	if (bind)
	{
		sim::setSaveWork(60, ap::kSaveMagic);
		sim::setSaveWork(61, tag);
	}
	cl.checked.clear();
	cl.items = json::array();
	cl.config = Config(tag, deathLink);
	Deliver({ cl.config, { {"cmd", "items"}, {"items", cl.items} }, { {"cmd", "checked"}, {"locations", json::array()} } });
	Tick(200); // let pickup/shop windows from earlier scenarios expire
	return tag;
}

void SetItems(const std::vector<int64_t>& ids)
{
	cl.items = json::array();
	for (size_t i = 0; i < ids.size(); i++)
		cl.items.push_back(Item(ids[i], int(i)));
	Deliver({ { {"cmd", "items"}, {"items", cl.items} } });
}

// Pick up `num` of an item through the item-get screen; screen closes afterwards.
// placed: a map item (the game sets the room's item flag); otherwise an enemy drop.
// Supply pickups (ammo/herbs/grenades) are decided up to kFlagAfterFrames later: use Settle().
bool gPickupPlaced = true;
void Pickup(uint16_t id, int num, bool close = true, int chr = 0)
{
	sim::setOpenFlag(sim::SS_ITEM);
	sim::setItemGetFlag(true);
	sim::setStatus(sim::STA_ITEM_GET, true);
	Tick(3);
	sim::gameAdd(id, num, chr);
	if (gPickupPlaced)
		sim::setRoomItemFlag(sim::nextRoomItemBit());
	Tick(3);
	if (close)
	{
		sim::setOpenFlag(sim::SS_NULL);
		sim::setItemGetFlag(false);
		sim::setStatus(sim::STA_ITEM_GET, false);
		Tick(3);
	}
}

void Drop(uint16_t id, int num)
{
	gPickupPlaced = false;
	Pickup(id, num);
	gPickupPlaced = true;
}

// Pesetas lying in the world (placed) or dropped by an enemy; no pickup screen
void PickupGold(int amount, bool placed = true)
{
	sim::setGold(sim::gold() + amount);
	if (placed)
		sim::setRoomItemFlag(sim::nextRoomItemBit());
	Tick(3);
}

// let pending supply pickups be decided
void Settle() { Tick(ap::kFlagAfterFrames + 5); }

void Buy(uint16_t id, int price)
{
	sim::setOpenFlag(sim::SS_SHOP);
	sim::setStatus(sim::STA_INTO_SHOP, true);
	Tick(2);
	sim::setGold(sim::gold() - price);
	if (id)
		sim::gameAdd(id, 1);
	Tick(2);
}
void CloseShop()
{
	sim::setOpenFlag(sim::SS_NULL);
	sim::setStatus(sim::STA_INTO_SHOP, false);
	Tick(15);
}

// =================================================================== scenarios
void S1()
{
	Begin("1. New game in r100 auto-binds save; hello reports it");
	sim::reset(0x100);
	cl.checked.clear();
	cl.items = json::array();
	uint32_t tag = ++nextTag;
	cl.config = Config(tag);
	size_t m0 = cl.got.size();
	Connect();
	Tick(1);
	Drain(50);
	json hello = LastCmd("hello");
	Expect(!hello.is_null() && hello.value("save_tag", -1) == 0 && hello.value("received", 0) == -1,
		"first hello before config reports unlinked save: " + hello.dump());
	Tick(5);
	Expect(sim::saveWork(60) == ap::kSaveMagic, "magic written to save_free_work[60]");
	Expect(sim::saveWork(61) == tag, "seed tag written to save_free_work[61]");
	Expect(sim::saveWork(62) == 0, "received index 0");
	Expect(ap::saveState == 1 && ap::uiStatus == 2, "save state bound, ui status ready");
	Disconnect();
	Tick(3);
	size_t m1 = cl.got.size();
	Connect();
	Tick(1);
	Drain(50);
	hello = LastCmd("hello");
	Expect(cl.got.size() > m1 && hello.value("save_tag", int64_t(0)) == int64_t(tag) && hello.value("received", -1) == 0,
		"hello after reconnect reports bound tag and received=0: " + hello.dump());
	Tick(5);
	(void)m0;
	End();
}

void S2()
{
	Begin("2. Shotgun pickup in r101 -> check + removal after screen closes");
	Setup(0x101);
	size_t m = cl.got.size();
	Pickup(44, 1, false);
	Expect(Has(ChecksSince(m), L_SHOTGUN), "check 1-1 Village: Shotgun sent");
	Expect(sim::count(44) == 1, "Shotgun still present while pickup screen open");
	sim::setOpenFlag(sim::SS_NULL);
	sim::setItemGetFlag(false);
	sim::setStatus(sim::STA_ITEM_GET, false);
	Tick(3);
	Expect(sim::count(44) == 0, "Shotgun removed after screen closed");
	Expect(sim::count(35) == 1 && sim::count(4) == 20, "starting Handgun/ammo untouched");
	Expect(ap::SaveBit(1), "save bit for the location set");
	End();
}

void S3()
{
	Begin("3. Two Spinels in r103 -> two checks; after death/continue re-pick maps to first location");
	Setup(0x103);
	auto checkpoint = sim::save();
	size_t m = cl.got.size();
	Pickup(87, 1);
	Pickup(87, 1);
	auto c = ChecksSince(m);
	Expect(Has(c, L_FARM_SPINEL1) && Has(c, L_FARM_SPINEL2), "checks for Farm Spinel #1 and #2");
	Expect(sim::count(87) == 0, "both Spinels removed");
	// death + continue from checkpoint
	sim::setHp(0);
	Tick(10);
	sim::setRoutine(sim::R_ROOMINIT);
	Tick(5);
	sim::restore(checkpoint);
	Tick(5);
	Expect(!ap::SaveBit(OFF_FARM_SPINEL1) && !ap::SaveBit(OFF_FARM_SPINEL2), "save bits rolled back with GLOBAL_WK");
	size_t m2 = cl.got.size();
	Pickup(87, 1);
	auto c2 = ChecksSince(m2);
	Expect(!Has(c2, L_FARM_SPINEL2) && (c2.empty() || (c2.size() == 1 && c2[0] == L_FARM_SPINEL1)),
		"no new location checked after continue (got " + json(c2).dump() + ")");
	Expect(ap::SaveBit(OFF_FARM_SPINEL1) && !ap::SaveBit(OFF_FARM_SPINEL2), "re-pick maps to Spinel #1 (bit 4) again");
	Expect(sim::count(87) == 0, "re-picked Spinel removed");
	End();
}

void S4()
{
	Begin("4. Consumables: room spots taken in order, extra drops kept, death re-maps, unlisted room");
	Setup(0x106); // 3 consumable spots
	size_t m = cl.got.size(), c0 = sim::conLines().size();
	int before = sim::count(4);
	Pickup(4, 10);
	Settle();
	auto ch = ChecksSince(m);
	Expect(ch.size() == 1, "first ammo pickup -> one check");
	Expect(sim::count(4) == before, "picked ammo removed");
	auto snapshot = sim::save();
	Pickup(6, 1); // green herb counts toward the same room's spots
	Pickup(24, 6);
	Settle();
	Expect(ChecksSince(m).size() == 3, "three spots -> three checks");
	size_t m2 = cl.got.size();
	Pickup(4, 10);
	Settle();
	Expect(ChecksSince(m2).empty(), "4th consumable: no check");
	Expect(sim::count(4) == before + 10, "4th consumable kept");
	Expect(!ConContains(c0, "Unmapped"), "no Unmapped log for consumables");
	// death/continue: save rolls back to after the first pickup; the next pickup maps to spot 2 again
	sim::setHp(0);
	Tick(10);
	sim::setRoutine(sim::R_ROOMINIT);
	Tick(5);
	sim::restore(snapshot);
	Tick(5);
	size_t m3 = cl.got.size();
	Pickup(6, 1);
	Settle();
	Expect(ChecksSince(m3).size() <= 1, "re-pickup after rollback sends at most the already-known spot");
	Expect(sim::count(6) == 0, "re-picked herb maps to a spot again (room flags rolled back too) and is removed");
	// a room with no spots of its own may take an unverified spot from the stage
	Setup(0x102);
	size_t m4 = cl.got.size();
	Pickup(4, 10);
	Settle();
	Expect(ChecksSince(m4).size() <= 1, "unlisted room: at most one loose spot");
	End();
}

void S5()
{
	Begin("5. Merchant (keep_item): first purchase checked once & kept; free Punisher -> medallion reward");
	gMerchantCheckOnly = false;
	Setup(0x104);
	gMerchantCheckOnly = true;
	sim::setGold(100000);
	Tick(2);
	size_t m = cl.got.size();
	Buy(37, 14000); // Red9
	CloseShop();
	auto c = ChecksSince(m);
	Expect(Has(c, L_BUY_RED9), "Merchant: Buy Red9 checked");
	Expect(sim::count(37) == 1, "Red9 kept");
	// sell and buy again
	Buy(0, -7000);
	sim::gameRemoveAll(37);
	Tick(2);
	size_t m2 = cl.got.size();
	Buy(37, 14000);
	CloseShop();
	Expect(!Has(ChecksSince(m2), L_BUY_RED9) && ChecksSince(m2).empty(), "second purchase: no second check");
	Expect(sim::count(37) == 1, "second Red9 kept");
	size_t m3 = cl.got.size();
	Buy(33, 0); // Punisher for free
	CloseShop();
	auto c3 = ChecksSince(m3);
	Expect(Has(c3, L_MEDALLION) && !Has(c3, L_BUY_PUNISHER), "free Punisher -> Blue Medallions reward only");
	Expect(sim::count(33) == 1, "Punisher kept");
	sim::gameRemoveAll(33);
	Tick(2);
	size_t m4 = cl.got.size();
	Buy(33, 20000);
	CloseShop();
	Expect(Has(ChecksSince(m4), L_BUY_PUNISHER), "paid Punisher -> Merchant: Buy Punisher");
	End();
}

void S5b()
{
	Begin("5b. Merchant (check_only): first purchase sends the check, item taken back after the shop; vest stays");
	Setup(0x104);
	sim::setGold(200000);
	Tick(2);
	size_t m = cl.got.size();
	Buy(37, 14000); // Red9
	Expect(sim::count(37) == 1, "Red9 still there while the shop is open");
	CloseShop();
	Expect(Has(ChecksSince(m), L_BUY_RED9), "Merchant: Buy Red9 checked");
	Expect(sim::count(37) == 0, "Red9 taken back after leaving the shop");
	Expect(sim::gold() == 200000 - 14000, "price stays paid");
	size_t m2 = cl.got.size();
	Buy(37, 14000);
	CloseShop();
	Expect(ChecksSince(m2).empty() && sim::count(37) == 1, "second purchase: normal, Red9 kept");
	size_t m3 = cl.got.size();
	Buy(33, 0); // free Punisher (medallion reward)
	CloseShop();
	Expect(Has(ChecksSince(m3), L_MEDALLION) && sim::count(33) == 0, "medallion reward checked, Punisher taken back");
	Buy(254, 60000); // Tactical Vest
	CloseShop();
	Expect(sim::count(254) == 1, "tactical vest kept (costume change)");
	End();
}

void S6()
{
	Begin("6. Attache case purchase (board size grows in shop) -> merchant check");
	Setup(0x104);
	sim::setGold(100000);
	Tick(2);
	size_t m = cl.got.size();
	sim::setOpenFlag(sim::SS_SHOP);
	Tick(2);
	sim::setGold(sim::gold() - 24000);
	sim::setBoardSize(1);
	Tick(2);
	CloseShop();
	Expect(Has(ChecksSince(m), L_BUY_CASE_M), "Merchant: Buy Attache Case M checked");
	End();
}

void S7()
{
	Begin("7. Bottle cap appears -> cap check");
	Setup(0x209);
	size_t m = cl.got.size();
	sim::setStatus(sim::STA_SHOOTING, true);
	Tick(2);
	sim::gameAdd(222, 1);
	Tick(2);
	sim::setStatus(sim::STA_SHOOTING, false);
	Tick(3);
	Expect(Has(ChecksSince(m), L_CAP_HANDGUN), "Shooting Gallery A: Leon w/ handgun Cap checked");
	Expect(sim::count(222) == 1, "cap kept");
	End();
}

void S8()
{
	Begin("8. Received items: cooldown, safety, persistence, re-apply, pesetas, case, case-full, left-behind");
	Setup(0x101);
	sim::setGold(0);
	// block with an open pickup screen
	sim::setOpenFlag(sim::SS_ITEM);
	Tick(1);
	SetItems({ I_P1000, I_P5000, I_HANDGUN_AMMO });
	Tick(40);
	Expect(sim::gold() == 0 && sim::saveWork(62) == 0, "nothing applied while a sub screen is open");
	sim::setOpenFlag(sim::SS_NULL);
	int firstFrame = -1, secondFrame = -1;
	auto before = sim::save();
	for (int f = 0; f < 80; f++)
	{
		Tick(1);
		if (firstFrame < 0 && sim::gold() == 1000) firstFrame = f;
		if (secondFrame < 0 && sim::gold() == 6000) secondFrame = f;
		if (f == 0) before = sim::save(); // state with index 1
	}
	Expect(firstFrame >= 0 && secondFrame >= 0 && secondFrame - firstFrame >= ap::kGrantCooldownFrames,
		"items applied one per cooldown (frames " + std::to_string(firstFrame) + ", " + std::to_string(secondFrame) + ")");
	Expect(sim::gold() == 6000, "pesetas add gold (6000)");
	Expect(sim::count(4) == 30, "handgun ammo added (+10)");
	Expect(sim::saveWork(62) == 3, "index persisted in save work (3)");
	json rec = LastCmd("received");
	Expect(rec.value("received", 0) == 3, "client told received=3");
	// older save state
	sim::restore(before);
	int idx = int(sim::saveWork(62));
	Tick(80);
	Expect(idx == 1 && sim::saveWork(62) == 3 && sim::gold() == 6000 && sim::count(4) == 30,
		"older save (index " + std::to_string(idx) + ") re-applies remaining items");
	// progressive case
	size_t m = cl.got.size();
	SetItems({ I_P1000, I_P5000, I_HANDGUN_AMMO, I_CASE });
	Tick(30);
	Expect(sim::boardSize() == 1 && sim::saveWork(62) == 4, "progressive case: board size 0 -> 1");
	Expect(ChecksSince(m).empty(), "no merchant check from a received case");
	// case full
	sim::setCaseFull(true);
	SetItems({ I_P1000, I_P5000, I_HANDGUN_AMMO, I_CASE, I_RIFLE, I_P1000 });
	Tick(25);
	Expect((sim::openFlag() & sim::SS_PZZL) && sim::getItemId() == 46, "case full: organize screen opened for Rifle");
	Expect(sim::saveWork(62) == 4, "index not advanced while Rifle waits");
	Tick(100);
	Expect(sim::saveWork(62) == 4 && sim::gold() == 6000, "next item not applied while Rifle waits");
	m = cl.got.size();
	sim::placePending();
	Tick(2);
	Expect(sim::count(46) == 1 && sim::saveWork(62) >= 5, "index advances after Rifle lands (rifle " +
		std::to_string(sim::count(46)) + ", index " + std::to_string(sim::saveWork(62)) + ", grant " + std::to_string(ap::pendingGrant.active) + ")");
	Expect(ChecksSince(m).empty(), "no check for the received Rifle");
	Tick(30);
	Expect(sim::gold() == 7000 && sim::saveWork(62) == 6, "following item applied afterwards");
	// left behind
	SetItems({ I_P1000, I_P5000, I_HANDGUN_AMMO, I_CASE, I_RIFLE, I_P1000, I_TMP });
	Tick(25);
	Expect(sim::getItemId() == 48, "organize screen for TMP");
	sim::discardPending();
	Tick(50);
	Expect(sim::saveWork(62) == 6, "not given up too early");
	Tick(60);
	Expect(sim::saveWork(62) == 7 && sim::count(48) == 0, "left-behind TMP: index advances after timeout");
	sim::setCaseFull(false);
	End();
}

void S9()
{
	Begin("9. Bosses: hp 0 -> check, despawn -> none; Saddler goal only in r332");
	Setup(0x10B);
	sim::setEm(0, 0x2F, 5000, 77, true);
	Tick(3);
	size_t m = cl.got.size();
	sim::setEm(0, 0x2F, 0, 77, true);
	Tick(3);
	Expect(Has(ChecksSince(m), L_DEL_LAGO), "Del Lago hp 0 -> Defeat Del Lago");
	sim::setEm(0, 0, 0, 0, false);
	sim::setRoom(0x119);
	Tick(3);
	sim::setEm(1, 0x2B, 3000, 88, true);
	Tick(3);
	m = cl.got.size();
	sim::setEm(1, 0x2B, 3000, 88, false); // despawned with hp left
	Tick(3);
	Expect(!Has(ChecksSince(m), L_GIGANTE_QUARRY), "El Gigante despawn without hp 0 -> no check");
	sim::setEm(1, 0, 0, 0, false);
	sim::setRoom(0x331);
	Tick(3);
	sim::setEm(2, 0x31, 9000, 99, true);
	Tick(3);
	m = cl.got.size();
	sim::setEm(2, 0x31, 0, 99, true);
	Tick(3);
	Expect(CountCmdSince(m, "goal") == 0 && !(sim::saveWork(63) & 1), "Saddler killed outside r332 -> no goal");
	sim::setEm(2, 0, 0, 0, false);
	sim::setRoom(0x332);
	Tick(3);
	sim::setEm(3, 0x31, 9000, 100, true);
	Tick(3);
	m = cl.got.size();
	sim::setEm(3, 0x31, 0, 100, true);
	Tick(3);
	Expect(CountCmdSince(m, "goal") >= 1 && (sim::saveWork(63) & 1), "Saddler hp 0 in r332 -> goal");
	sim::setEm(3, 0, 0, 0, false);
	End();
}

void S10()
{
	Begin("10. Save of a different seed: no items/checks, mismatch status; bind_save relinks");
	uint32_t tag = Setup(0x101, false);
	sim::setSaveWork(60, ap::kSaveMagic);
	sim::setSaveWork(61, 0xDEAD);
	sim::setSaveWork(62, 7);
	sim::setGold(0);
	Tick(3);
	SetItems({ I_P1000 });
	size_t m = cl.got.size();
	Pickup(44, 1);
	Tick(40);
	Expect(ap::saveState == 2 && ap::uiStatus == 3, "save state mismatch / ui status 3");
	Expect(RenderContains("belongs to another seed"), "overlay shows the mismatch");
	Expect(sim::gold() == 0, "no received item applied");
	Expect(ChecksSince(m).empty() && sim::count(44) == 1, "no check, Shotgun kept");
	Deliver({ { {"cmd", "bind_save"} } });
	Tick(40);
	Expect(sim::saveWork(61) == tag && ap::saveState == 1, "bind_save relinks to current seed");
	Expect(sim::gold() == 1000, "received items applied after relink");
	Expect(ChecksSince(m).empty() && sim::count(44) == 1, "Shotgun picked before relink stays, no check");
	End();
}

void S11()
{
	Begin("11. Checks made while disconnected are sent after reconnect + config");
	Setup(0x101);
	Disconnect();
	Tick(3);
	Pickup(44, 1);
	Tick(5);
	Expect(sim::count(44) == 0, "Shotgun removed even while offline");
	size_t m = cl.got.size();
	Connect();
	Tick(5);
	Drain(50);
	Tick(5);
	Expect(Has(ChecksSince(m), L_SHOTGUN), "offline check delivered after reconnect");
	Expect(cl.checked.count(L_SHOTGUN) == 1, "fake server marked it checked");
	End();
}

void S12()
{
	Begin("12. Ashley as player: pickups ignored, no received items");
	Setup(0x101);
	sim::setPlType(sim::PL_ASHLEY);
	sim::setChar(1);
	sim::setGold(0);
	Tick(3);
	SetItems({ I_P1000 });
	size_t m = cl.got.size();
	Pickup(44, 1, true, 1);
	Tick(40);
	Expect(ChecksSince(m).empty() && sim::count(44, 1) == 1, "Ashley's Shotgun pickup ignored and kept");
	Expect(sim::gold() == 0, "no received item while playing Ashley");
	sim::setPlType(sim::PL_LEON);
	sim::setChar(0);
	Tick(40);
	Expect(ChecksSince(m).empty(), "switching back to Leon produces no check");
	Expect(sim::gold() == 1000, "received item applied once Leon is back");
	End();
}

void S13()
{
	Begin("13. Garbage / non-object JSON does not crash");
	Setup(0x101);
	cl.sendRaw("this is not json\n");
	cl.sendRaw("[1,2,3]\n42\n\"str\"\nnull\n\n");
	cl.sendRaw("{\"cmd\":5}\n");
	cl.sendRaw("{\"cmd\":\"checked\",\"locations\":[\"x\",{}]}\n");
	cl.sendRaw("{\"cmd\":\"message\",\"text\":12}\n");
	cl.sendRaw("{\"cmd\":\"kill\",\"cause\":[]}\n");
	cl.sendRaw("{\"cmd\":\"config\",\"save_tag\":\"x\"}\n");
	cl.sendRaw("{\"cmd\":\"unknown\"}\n");
	cl.sendRaw("{\"cmd\":\"ite");
	Sleep(30);
	cl.sendRaw("ms\",\"items\":[]}\n");
	cl.sendRaw(std::string(200000, '{') + "\n");
	cl.sendRaw(std::string("\xff\xfe\x00garbage\n", 11));
	Sleep(300);
	Tick(10);
	Expect(ap::clientConnected, "connection still up");
	SendChecked();
	size_t m = cl.got.size();
	Pickup(44, 1);
	Tick(5);
	Expect(Has(ChecksSince(m), L_SHOTGUN), "module still works afterwards");
	End();
}

// ---------------------------------------------------------------- extra edge cases
void E1()
{
	Begin("E1. Overlay shows 'waiting for client' after the client disconnects mid-game");
	Setup(0x101);
	Disconnect();
	Tick(5);
	Expect(ap::uiStatus == 0, "uiStatus is 0 (waiting for client), got " + std::to_string(ap::uiStatus.load()));
	Expect(RenderContains("waiting for the RE4 UHD Client"), "overlay tells the player the client is gone");
	Connect();
	Tick(5);
	Drain(50);
	Tick(3);
	End();
}

void E2()
{
	Begin("E2. /bindsave on a save already linked to this seed keeps progress");
	Setup(0x101);
	sim::setGold(0);
	SetItems({ I_P1000 });
	Tick(30);
	Pickup(44, 1);
	Tick(5);
	int gold = sim::gold();
	Deliver({ { {"cmd", "bind_save"} } });
	Tick(60);
	Expect(sim::saveWork(62) == 1 && sim::gold() == gold, "received index kept (no duplicate items), gold " +
		std::to_string(gold) + " -> " + std::to_string(sim::gold()));
	Expect(ap::SaveBit(1), "collected-location bits kept");
	End();
}

void E3()
{
	Begin("E3. Case-full grant when SubScreenOpen takes effect a frame later");
	Setup(0x101);
	sim::setCaseFull(true);
	sim::setDeferredSubScreenOpen(true);
	size_t m = cl.got.size(), c0 = sim::conLines().size();
	SetItems({ I_SHOTGUN });
	Tick(25);
	bool refused = ConContains(c0, "could not be added");
	Expect(!refused && sim::saveWork(62) == 0, "item not reported as refused / index not advanced before it lands");
	sim::placePending();
	Tick(5);
	Expect(!Has(ChecksSince(m), L_SHOTGUN), "received Shotgun not mistaken for the r101 Shotgun pickup (checks " + json(ChecksSince(m)).dump() + ")");
	Expect(sim::count(44) == 1 && sim::saveWork(62) == 1, "received Shotgun kept, index 1 (count " + std::to_string(sim::count(44)) + ", index " + std::to_string(sim::saveWork(62)) + ")");
	sim::setCaseFull(false);
	sim::setDeferredSubScreenOpen(false);
	End();
}

void E4()
{
	Begin("E4. DeathLink: kill sets hp 0 without echo; own death sends death once");
	Setup(0x101, true, true);
	size_t m = cl.got.size();
	Deliver({ { {"cmd", "kill"}, {"cause", "x died"} } });
	Tick(5);
	Expect(sim::hp() <= 0, "kill applied");
	Expect(CountCmdSince(m, "death") == 0, "no death echoed for a DeathLink kill");
	sim::setHp(1200);
	Tick(700);
	m = cl.got.size();
	sim::setHp(0);
	Tick(10);
	Expect(CountCmdSince(m, "death") == 1, "own death sends one death");
	sim::setHp(1200);
	Tick(3);
	Setup(0x101, true, false);
	Deliver({ { {"cmd", "kill"}, {"cause", "x"} } });
	Tick(5);
	Expect(sim::hp() > 0, "kill ignored when death_link is off");
	End();
}

void E5()
{
	Begin("E5. Cutscene item without pickup screen (Golden Sword r207) -> check + removal");
	Setup(0x207);
	size_t m = cl.got.size();
	sim::setStatus(sim::STA_EVENT, true);
	Tick(2);
	sim::gameAdd(128, 1);
	Tick(2);
	sim::setStatus(sim::STA_EVENT, false);
	Tick(3);
	Expect(Has(ChecksSince(m), L_GOLDEN_SWORD), "3-1 Barracks: Golden Sword checked");
	Expect(sim::count(128) == 0, "Golden Sword removed");
	End();
}

void E6()
{
	Begin("E6. Goal flag in save is re-sent on reconnect; jet-ski room reports goal");
	size_t m = cl.got.size();
	Setup(0x333);
	Tick(3);
	Expect(CountCmdSince(m, "goal") >= 1 && (sim::saveWork(63) & 1), "r333 reports goal");
	Disconnect();
	Tick(2);
	m = cl.got.size();
	Connect();
	Tick(3);
	Drain(50);
	Tick(3);
	Expect(CountCmdSince(m, "goal") >= 1, "goal re-sent after reconnect");
	End();
}

void E7()
{
	Begin("E7. Unmapped weapon via pickup screen is kept and logged");
	Setup(0x101);
	size_t m = cl.got.size();
	Pickup(45, 1); // Striker: never placed in the village
	Tick(3);
	Expect(sim::count(45) == 1, "kept");
	Expect(CountCmdSince(m, "log") >= 1, "Unmapped log sent to client");
	End();
}

void E8()
{
	Begin("E8. Malformed config (slot_data not an object) does not wipe the active config");
	uint32_t tag = Setup(0x101);
	size_t n = ap::locations.size();
	cl.sendRaw(json({ {"cmd", "config"}, {"save_tag", tag}, {"slot_data", "oops"} }).dump() + "\n");
	Sleep(100);
	Tick(3);
	Expect(ap::locations.size() == n, "locations kept (" + std::to_string(n) + " -> " + std::to_string(ap::locations.size()) + ")");
	sim::setGold(0);
	SetItems({ I_P1000 });
	Tick(30);
	Expect(sim::gold() == 1000 || sim::saveWork(62) == 0, "received item not consumed as 'Unknown item id' (gold " +
		std::to_string(sim::gold()) + ", index " + std::to_string(sim::saveWork(62)) + ")");
	Deliver({ cl.config });
	Tick(3);
	End();
}

void E9()
{
	Begin("E9. Stacked treasure picked up 2 at once maps to two locations");
	Setup(0x106);
	size_t m = cl.got.size();
	Pickup(87, 2);
	auto c = ChecksSince(m);
	Expect(c.size() == 2 && sim::count(87) == 0, "two checks and both removed: " + json(c).dump());
	End();
}

void E10()
{
	Begin("E10. Real pickup with full case (organize screen, get_item_id set) -> check + removal");
	Setup(0x101);
	size_t m = cl.got.size();
	sim::setGetItem(44, 1);
	sim::setOpenFlag(sim::SS_PZZL);
	Tick(3);
	sim::placePending();
	Tick(5);
	Expect(Has(ChecksSince(m), L_SHOTGUN), "1-1 Village: Shotgun checked");
	Expect(sim::count(44) == 0, "Shotgun removed");
	End();
}

void E11()
{
	Begin("E11. Malformed 'checked' message does not forget server-checked locations");
	Setup(0x101);
	cl.checked.insert(L_SHOTGUN);
	SendChecked();
	Tick(2);
	cl.sendRaw("{\"cmd\":\"checked\",\"locations\":[7741001,\"x\"]}\n");
	Sleep(100);
	Tick(2);
	Expect(ap::serverChecked.count(L_SHOTGUN) == 1, "serverChecked still contains 7741001");
	End();
}

void E13()
{
	Begin("E13. Received Shotgun applied in r101 is not taken as the r101 Shotgun pickup");
	Setup(0x101);
	size_t m = cl.got.size();
	SetItems({ I_SHOTGUN });
	Tick(30);
	Expect(ChecksSince(m).empty(), "no check");
	Expect(sim::count(44) == 1 && sim::saveWork(62) == 1, "Shotgun kept, index 1");
	End();
}


// ------------------------------------------------------------------ extra scenarios (v0.2)
void H1()
{
	Begin("H1. Starting inventory (precollected items) delivered at a new game, not counted as pickups");
	Setup(0x100);
	size_t m = cl.got.size();
	int gold0 = sim::gold();
	SetItems({ IT_RED9, IT_HANDGUN_AMMO, IT_HANDGUN_AMMO, IT_GREEN_HERB, IT_P1000 });
	Tick(200);
	Expect(sim::saveWork(62) == 5, "all 5 starting items applied (index " + std::to_string(sim::saveWork(62)) + ")");
	Expect(sim::count(37) == 1, "Red9 in the case");
	Expect(sim::gold() == gold0 + 1000, "1000 pesetas added");
	Expect(ChecksSince(m).empty(), "no checks from starting items");
	End();
}

void H2()
{
	Begin("H2. Buying ammo at the Merchant is not a consumable check; selling doesn't crash");
	Setup(0x101);
	size_t m = cl.got.size();
	int ammo = sim::count(4);
	Buy(4, 500);
	CloseShop();
	Expect(ChecksSince(m).empty(), "no check for bought ammo");
	Expect(sim::count(4) == ammo + 1 || sim::count(4) > ammo, "bought ammo kept");
	// sell the Handgun ammo
	sim::setOpenFlag(sim::SS_SHOP);
	sim::setStatus(sim::STA_INTO_SHOP, true);
	Tick(2);
	sim::gameRemoveAll(4);
	sim::setGold(sim::gold() + 300);
	Tick(2);
	CloseShop();
	Expect(ChecksSince(m).empty(), "selling sends nothing");
	End();
}

void H3()
{
	Begin("H3. Combining a treasure in the inventory screen is not a pickup");
	Setup(0x101);
	sim::gameAdd(198, 1); // Elegant Mask (already in the case, not a pickup now)
	sim::gameAdd(199, 1); // Green Gem
	ap::Resnapshot();
	Tick(5);
	size_t m = cl.got.size(), c0 = sim::conLines().size();
	sim::setOpenFlag(sim::SS_NORMAL);
	sim::setStatus(sim::STA_SUB_SCRN, true);
	Tick(2);
	sim::gameRemoveAll(198);
	sim::gameRemoveAll(199);
	sim::gameAdd(202, 1); // Elegant Mask w/ (G)
	Tick(2);
	sim::setOpenFlag(sim::SS_NULL);
	sim::setStatus(sim::STA_SUB_SCRN, false);
	Tick(10);
	Expect(ChecksSince(m).empty(), "no check");
	Expect(sim::count(202) == 1, "combined treasure kept");
	Expect(!ConContains(c0, "Unmapped"), "not logged as an unmapped pickup");
	End();
}

void H4()
{
	Begin("H4. A burst of 30 received items is delivered one at a time");
	Setup(0x101);
	int gold0 = sim::gold();
	std::vector<int64_t> ids(30, IT_P1000);
	SetItems(ids);
	Tick(25);
	Expect(sim::saveWork(62) >= 1 && sim::saveWork(62) <= 3, "only the first item or two after 25 frames");
	Tick(800);
	Expect(sim::saveWork(62) == 30, "all 30 applied (index " + std::to_string(sim::saveWork(62)) + ")");
	Expect(sim::gold() == gold0 + 30000, "30000 pesetas added");
	End();
}

void H5()
{
	Begin("H5. Removal waits through a cutscene and a room change");
	Setup(0x101);
	size_t m = cl.got.size();
	Pickup(44, 1, false);                 // Shotgun, screen still open
	sim::setStatus(sim::STA_EVENT, true); // a cutscene starts as the screen closes
	sim::setOpenFlag(sim::SS_NULL);
	sim::setItemGetFlag(false);
	sim::setStatus(sim::STA_ITEM_GET, false);
	Tick(10);
	Expect(sim::count(44) == 1, "not removed during the cutscene");
	sim::setRoutine(sim::R_ROOMINIT);
	Tick(5);
	sim::setRoom(0x102);
	sim::setRoutine(sim::R_MAINLOOP);
	sim::setStatus(sim::STA_EVENT, false);
	Tick(10);
	Expect(Has(ChecksSince(m), L_SHOTGUN), "Shotgun check sent");
	Expect(sim::count(44) == 0, "Shotgun removed after the room change");
	End();
}

void H6()
{
	Begin("H6. Consumable picked up through the full-case organize screen counts");
	Setup(0x101);
	size_t m = cl.got.size();
	int ammo = sim::count(24);
	sim::setOpenFlag(sim::SS_PZZL);
	sim::setGetItem(24, 6);
	Tick(3);
	sim::gameAdd(24, 6);
	sim::setRoomItemFlag(sim::nextRoomItemBit());
	sim::setGetItem(0, 0);
	sim::setOpenFlag(sim::SS_NULL);
	Settle();
	Expect(ChecksSince(m).size() == 1, "one consumable check");
	Expect(sim::count(24) == ammo, "shells removed");
	End();
}

void H7()
{
	Begin("H7. High location offsets (Final chapter) use the extended save bitset and roll back");
	Setup(0x332);
	auto checkpoint = sim::save();
	size_t m = cl.got.size();
	Pickup(24, 6);
	Settle();
	auto ch = ChecksSince(m);
	Expect(ch.size() == 1 && ch[0] == L_SADDLER_SHELLS, "Saddler arena shells check");
	uint32_t word = sim::saveWork(28 + OFF_SADDLER_SHELLS / 32);
	Expect((word >> (OFF_SADDLER_SHELLS % 32)) & 1, "bit stored in save_free_work[" + std::to_string(28 + OFF_SADDLER_SHELLS / 32) + "]");
	sim::setHp(0);
	Tick(10);
	sim::setRoutine(sim::R_ROOMINIT);
	Tick(5);
	sim::restore(checkpoint);
	Tick(5);
	Expect(!ap::SaveBit(OFF_SADDLER_SHELLS), "bit rolled back with the save");
	End();
}

void H8()
{
	Begin("H8. Enemy-randomizer config: no Saddler em, jet-ski room still reports the goal");
	Setup(0x332);
	size_t m = cl.got.size();
	sim::setEm(0, 0x2B, 0, 99, true); // an El Gigante dies in Saddler's arena (randomized boss)
	Tick(5);
	Expect(CountCmdSince(m, "goal") == 0, "no goal from a non-Saddler enemy");
	sim::setRoutine(sim::R_ROOMINIT);
	Tick(3);
	sim::setEm(0, 0, 0, 0, false);
	sim::setRoom(0x333);
	sim::setRoutine(sim::R_MAINLOOP);
	Tick(5);
	Expect(CountCmdSince(m, "goal") >= 1, "goal sent on reaching the jet-ski");
	End();
}


void H9()
{
	Begin("H9. Treasure in a room the data doesn't list for it -> same item elsewhere in the stage");
	Setup(0x20d); // castle room with no Velvet Blue location
	size_t m = cl.got.size(), c0 = sim::conLines().size();
	Pickup(86, 1); // Velvet Blue
	Tick(5);
	auto ch = ChecksSince(m);
	Expect(ch.size() == 1, "one check");
	Expect(sim::count(86) == 0, "Velvet Blue removed");
	Expect(ConContains(c0, "by stage"), "logged as a stage match (room data to fix)");
	End();
}

void H10()
{
	Begin("H10. Treasure no location accounts for -> bonus treasure checks for its stage, then kept");
	Setup(0x20d);
	size_t m = cl.got.size();
	for (int i = 0; i < 5; i++)
		Pickup(119, 1); // Ruby: no castle Ruby locations exist
	Tick(5);
	auto ch = ChecksSince(m);
	Expect(ch.size() == 5 && Has(ch, L_CASTLE_BONUS1) && Has(ch, L_CASTLE_BONUS5), "5 castle bonus checks");
	Expect(sim::count(119) == 0, "all 5 removed");
	size_t m2 = cl.got.size(), c0 = sim::conLines().size();
	Pickup(119, 1);
	Tick(5);
	Expect(ChecksSince(m2).empty() && sim::count(119) == 1, "6th Ruby: no bonus left, kept");
	Expect(ConContains(c0, "Unmapped pickup"), "logged as unmapped");
	End();
}


void H11()
{
	Begin("H11. Diagnostics: pickups are logged with whether the game flagged a placed item");
	Setup(0x101);
	Tick(3);
	size_t c0 = sim::conLines().size();
	sim::setOpenFlag(sim::SS_ITEM);
	sim::setItemGetFlag(true);
	Tick(2);
	sim::setRoomItemFlag(5);  // placed item taken
	sim::gameAdd(4, 10);
	Tick(3);
	sim::setOpenFlag(sim::SS_NULL);
	sim::setItemGetFlag(false);
	Tick(200);                // flag window expires
	Drop(24, 6);              // a drop: no room flag
	Tick(3);
	Expect(ConContains(c0, "[roomflag] r101 item_flg bit 5"), "room flag flip logged");
	Expect(ConContains(c0, "[pickup] Handgun Ammo x10 r101 pickup-screen placed-item-flag"), "placed pickup logged with the flag");
	bool dropLine = false;
	for (size_t i = c0; i < sim::conLines().size(); i++)
		if (sim::conLines()[i].find("[pickup] Shotgun Shells x6 r101 pickup-screen") != std::string::npos &&
			sim::conLines()[i].find("placed-item-flag") == std::string::npos)
			dropLine = true;
	Expect(dropLine, "drop logged without the flag");
	End();
}

// Must run first: needs a process where no config has ever been received
void E12()
{
	Begin("E12. Pickup made before the client connects is tracked (config remembered from last session)");
	sim::reset(0x101);
	sim::setSaveWork(60, ap::kSaveMagic);
	uint32_t tag = ++nextTag;
	sim::setSaveWork(61, tag);
	Tick(5);
	Pickup(44, 1);
	Tick(5);
	cl.checked.clear();
	cl.items = json::array();
	cl.config = Config(tag);
	size_t m = cl.got.size();
	Connect();
	Tick(3);
	Drain(50);
	Tick(5);
	Expect(Has(ChecksSince(m), L_SHOTGUN), "Shotgun check sent once configured");
	Expect(sim::count(44) == 0, "vanilla Shotgun removed");
	Disconnect();
	Tick(2);
	End();
}

// =================================================================== 0.5: placed items vs drops, pesetas
void P1()
{
	Begin("P1. Enemy drops don't take consumable spots; placed items still do");
	Setup(0x106); // 3 consumable spots
	size_t m = cl.got.size(), c0 = sim::conLines().size();
	int ammo = sim::count(4);
	Drop(4, 10);
	Drop(6, 1);
	Settle();
	Expect(ChecksSince(m).empty(), "two drops: no checks");
	Expect(sim::count(4) == ammo + 10 && sim::count(6) == 1, "drops kept");
	Expect(ConContains(c0, "[pickup] drop Handgun Ammo x10 in r106 (not a check)"), "drop logged");
	Pickup(24, 6);
	Settle();
	Expect(ChecksSince(m).size() == 1, "placed shells after the drops -> the room's first spot");
	Expect(sim::count(24) == 0, "placed shells removed");
	End();
}

void P2()
{
	Begin("P2. Room flag set a little after the item appears still counts");
	Setup(0x106);
	size_t m = cl.got.size();
	gPickupPlaced = false;
	Pickup(4, 10);
	gPickupPlaced = true;
	Tick(30);
	sim::setRoomItemFlag(sim::nextRoomItemBit());
	Settle();
	Expect(ChecksSince(m).size() == 1, "late flag -> check");
	// and a flag set shortly before the item appears
	sim::setRoomItemFlag(sim::nextRoomItemBit());
	Tick(20);
	gPickupPlaced = false;
	Pickup(6, 1);
	gPickupPlaced = true;
	Settle();
	Expect(ChecksSince(m).size() == 2, "early flag -> check");
	End();
}

void P3()
{
	Begin("P3. A treasure's room flag doesn't make a drop picked up right after it count");
	Setup(0x103);
	size_t m = cl.got.size();
	Pickup(87, 1); // placed Spinel: sets a flag
	Drop(4, 10);   // drop right after it
	Settle();
	auto c = ChecksSince(m);
	Expect(c.size() == 1 && c[0] == L_FARM_SPINEL1, "only the Spinel checked (got " + json(c).dump() + ")");
	Expect(sim::count(4) == 30, "dropped ammo kept");
	End();
}

void P4()
{
	Begin("P4. Game that never flags pickups: falls back to counting every pickup, then learns");
	Setup(0x106);
	ap::flagMode = ap::FlagMode::Unknown;
	size_t m = cl.got.size(), c0 = sim::conLines().size();
	for (int i = 0; i < ap::kLegacyAfterUnflagged - 1; i++)
		Drop(6, 1);
	Settle();
	Expect(ChecksSince(m).empty(), "undecided pickups wait");
	Drop(6, 1);
	Settle();
	Expect(ap::flagMode == ap::FlagMode::Legacy, "switched to counting every pickup");
	Expect(ChecksSince(m).size() == 3, "the room's 3 spots checked retroactively");
	Expect(sim::count(6) == ap::kLegacyAfterUnflagged, "retroactive pickups kept");
	Expect(ConContains(c0, "counting every ammo/herb/pesetas pickup"), "fallback logged");
	Setup(0x101);
	size_t m2 = cl.got.size();
	Pickup(4, 10); // the game does flag this one
	Settle();
	Expect(ap::flagMode == ap::FlagMode::Flags, "a flagged supply pickup switches to flag mode");
	Expect(ChecksSince(m2).size() == 1, "and counts");
	std::ifstream f("re4_tweaks/archipelago_learned.json");
	std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	Expect(text.find("\"room_flags\":true") != std::string::npos, "learned state saved: " + text);
	End();
}

void P5()
{
	Begin("P5. Pesetas: placed pesetas take the stage's next pesetas check and are kept; drops and sales don't");
	Setup(0x101);
	size_t m = cl.got.size();
	PickupGold(500);
	Settle();
	auto c = ChecksSince(m);
	Expect(c.size() == 1 && c[0] == L_VILLAGE_PESETAS1, "Village Pesetas 1 (got " + json(c).dump() + ")");
	Expect(sim::gold() == 500, "pesetas kept");
	auto snapshot = sim::save();
	PickupGold(300, false); // enemy drop
	Settle();
	Expect(ChecksSince(m).size() == 1, "dropped pesetas: no check");
	sim::setOpenFlag(sim::SS_SHOP); // selling a treasure
	sim::setStatus(sim::STA_INTO_SHOP, true);
	Tick(2);
	sim::setGold(sim::gold() + 5000);
	Tick(2);
	CloseShop();
	Settle();
	Expect(ChecksSince(m).size() == 1, "sale at the Merchant: no check");
	PickupGold(1000);
	Settle();
	Expect(Has(ChecksSince(m), L_VILLAGE_PESETAS2), "next placed pesetas -> Village Pesetas 2");
	// death: pesetas bits roll back with the save, the re-pickup maps to Pesetas 2 again
	sim::setHp(0);
	Tick(10);
	sim::setRoutine(sim::R_ROOMINIT);
	Tick(5);
	sim::restore(snapshot);
	Tick(5);
	Expect(!ap::SaveBit(int(L_VILLAGE_PESETAS2 - LB)), "Pesetas 2 bit rolled back");
	size_t m2 = cl.got.size();
	PickupGold(1000);
	Settle();
	Expect(ap::SaveBit(int(L_VILLAGE_PESETAS2 - LB)), "re-pickup maps to Pesetas 2 again");
	Expect(ChecksSince(m2).size() <= 1, "no new location from the re-pickup");
	End();
}

void P6()
{
	Begin("P6. Island pesetas use the extended save bitset (offsets past 768); the pool runs out at 25");
	Setup(0x301);
	size_t m = cl.got.size();
	for (int i = 0; i < 25; i++)
		PickupGold(100);
	Settle();
	auto c = ChecksSince(m);
	Expect(c.size() == 25 && Has(c, L_ISLAND_PESETAS1) && Has(c, L_ISLAND_PESETAS11), "25 island pesetas checks");
	Expect((sim::saveWork(28 + OFF_ISLAND_PESETAS11 / 32) >> (OFF_ISLAND_PESETAS11 % 32)) & 1,
		"bit stored in save_free_work[" + std::to_string(28 + OFF_ISLAND_PESETAS11 / 32) + "]");
	PickupGold(100);
	Settle();
	Expect(ChecksSince(m).size() == 25, "26th island pesetas: none left");
	End();
}

void P7()
{
	Begin("P7. Random enemy health range comes from slot data");
	Setup(0x101);
	float lo = 0, hi = 0;
	Expect(!Archipelago_EnemyHP(&lo, &hi), "off by default");
	json cfg = cl.config;
	cfg["slot_data"]["enemy_health"] = { 0.5, 2.5 };
	Deliver({ cfg });
	Tick(2);
	Expect(Archipelago_EnemyHP(&lo, &hi) && lo == 0.5f && hi == 2.5f, "range 0.5 - 2.5");
	cfg["slot_data"]["enemy_health"] = { 50.0, 0.0 };
	Deliver({ cfg });
	Tick(2);
	Expect(Archipelago_EnemyHP(&lo, &hi) && lo == 15.0f && hi == 15.0f, "out-of-range values clamped");
	Deliver({ cl.config });
	Tick(2);
	Expect(!Archipelago_EnemyHP(&lo, &hi), "off again with a config without it");
	End();
}

// =================================================================== 0.5.1: what the first real session showed
void Q1()
{
	Begin("Q1. Treasures and key items reach the case before their pickup screen: they still count");
	Setup(0x106);
	size_t m = cl.got.size();
	sim::gameAdd(87, 1); // Spinel shot down from the tunnel roof, no pickup screen seen yet
	Tick(60);
	sim::setRoomItemFlag(11);
	Tick(30);
	auto c = ChecksSince(m);
	Expect(c.size() == 1 && c[0] == L_OLDHOUSE_SPINEL1, "Old House Road Spinel #1 checked (got " + json(c).dump() + ")");
	Expect(sim::count(87) == 0, "Spinel removed");
	sim::gameAdd(87, 1);
	Tick(30);
	Expect(Has(ChecksSince(m), L_OLDHOUSE_SPINEL2), "Spinel #2 checked");
	Setup(0x105);
	size_t m2 = cl.got.size();
	sim::gameAdd(59, 1); // Insignia Key
	Tick(30);
	Expect(Has(ChecksSince(m2), L_INSIGNIA_KEY), "Insignia Key checked");
	Expect(sim::count(59) == 0, "Insignia Key removed (it comes from the multiworld)");
	End();
}

void Q2()
{
	Begin("Q2. Hidden items: a barrel's contents count when picked up later; a drop afterwards doesn't");
	Setup(0x106);
	size_t m = cl.got.size(), c0 = sim::conLines().size();
	sim::setRoomItemFlag(20); // barrel broken: item appears, both flags set together
	sim::setRoomFindFlag(20);
	Tick(300);                // walk over a few seconds later
	Drop(4, 10);              // the pickup itself sets no flag
	Settle();
	Expect(ChecksSince(m).size() == 1, "barrel ammo -> check");
	Expect(ConContains(c0, "matches hidden item 20"), "logged as the hidden item");
	Drop(4, 10);
	Settle();
	Expect(ChecksSince(m).size() == 1, "an enemy drop afterwards: no check");
	sim::setRoomItemFlag(21);  // crate with pesetas
	sim::setRoomFindFlag(21);
	Tick(200);
	PickupGold(800, false);
	Settle();
	Expect(ChecksSince(m).size() == 2, "crate pesetas -> pesetas check");
	End();
}

void Q3()
{
	Begin("Q3. A knocked-down treasure uses up its own hidden-item credit");
	Setup(0x106);
	size_t m = cl.got.size();
	sim::setRoomFindFlag(13); // Spinel shot loose
	Tick(120);
	sim::gameAdd(87, 1);      // picked up
	Tick(60);
	sim::setRoomItemFlag(13); // taken flag a second later
	Settle();
	Expect(ChecksSince(m).size() == 1, "Spinel checked");
	Drop(6, 1);
	Settle();
	Expect(ChecksSince(m).size() == 1, "a drop afterwards doesn't use the Spinel's credit");
	End();
}

void Q4()
{
	Begin("Q4. Stocks attach to the gun when bought: check sent, nothing taken back");
	Setup(0x104);
	sim::setGold(100000);
	Tick(2);
	size_t m = cl.got.size(), c0 = sim::conLines().size();
	Buy(67, 10000); // Stock (TMP)
	CloseShop();
	Expect(Has(ChecksSince(m), L_BUY_STOCK_TMP), "Merchant: Buy Stock (TMP) checked");
	Expect(sim::count(67) == 1, "stock kept");
	Expect(!ConContains(c0, "Could not remove"), "no failed removal");
	End();
}

int main()
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	{
		std::ifstream f("slot_data.json");
		slotData = json::parse(f);
	}
	WSADATA w;
	WSAStartup(MAKEWORD(2, 2), &w);
	sim::reset(0x100);
	{
		// last session's config on disk, as the module writes it after a real config
		std::filesystem::create_directories("re4_tweaks");
		std::ofstream f("re4_tweaks/archipelago_config.json", std::ios::trunc);
		f << Config(nextTag + 1).dump();
	}
	re4t::init::Archipelago();
	Sleep(200);

	E12();
	// S1 models a fresh process: drop the seed E12 configured (the module keeps it across disconnects)
	ap::configured = false;
	ap::saveTag = 0;
	S1();
	S2(); S3(); S4(); S5(); S5b(); S6(); S7(); S8(); S9(); S10(); S11(); S12(); S13();
	E1(); E2(); E3(); E4(); E5(); E6(); E7(); E8(); E9(); E10(); E11(); E13();
	H1(); H2(); H3(); H4(); H5(); H6(); H7(); H8(); H9(); H10(); H11();
	P1(); P2(); P3(); P4(); P5(); P6(); P7();
	Q1(); Q2(); Q3(); Q4();

	printf("\n================ SUMMARY ================\n");
	for (auto& r : results)
		printf("%s\n", r.c_str());
	printf("%d passed, %d failed\n", passes, failures);
	fflush(stdout);
	_exit(failures ? 1 : 0);
}
