import os
import tempfile
import unittest

from . import RE4TestBase
from .. import rando_bridge as rb

# A synthetic profile in the randomizer's format (header line + key=value), with the keys we touch
# plus a few we don't, to check they are preserved.
PROFILE_KEYS = {
    "LanguageIndex": "0", "seedHashCheckBox": "0", "seedHashNumericBox": "1", "profileListComboBox": "2",
    "randomizeEnemiesCheckBox": "1", "enemyVarietyTrackBar": "3", "randomEnemyHealthCheckBox": "1",
    "randomizeDoorsCheckBox": "1", "minimumRoomsNumericUpDown": "7", "assignmentLeonModeCheckBox": "1",
    "randomizeItemsCheckBox": "1", "randomizeKeyItemsCheckBox": "1", "enemiesDropKeyItemsCheckBox": "1",
    "randomInventoryCheckBox": "1", "randomizeMerchantStockCheckBox": "1", "randomWeaponPriceCheckBox": "1",
    "randomHerbGrenadeCheckBox": "1", "randomAttacheCasePriceCheckBox": "1", "randomFirepowerCheckBox": "1",
    "randomCapacityCheckBox": "1", "saveSettingsCheckBox": "1", "openLogCheckBox": "1",
}
HEADER = "RE4 Randomizer - Test Build C"


def make_game(root: str, with_rando: bool = True, dll: bytes = b"MZ upstream re4_tweaks 1.9.1.0") -> str:
    game = os.path.join(root, "Resident Evil 4")
    os.makedirs(os.path.join(game, "Bin32"))
    open(os.path.join(game, "Bin32", "bio4.exe"), "wb").write(b"MZ")
    open(os.path.join(game, "Bin32", "dinput8.dll"), "wb").write(dll)
    open(os.path.join(game, "Bin32", "dinput8.ini"), "w").write("[DISPLAY]\nFOVAdditional = 0\n")
    if with_rando:
        rando = os.path.join(game, rb.RANDO_FOLDER)
        os.makedirs(os.path.join(rando, "Profiles", "PC"))
        open(os.path.join(rando, rb.RANDO_EXE), "wb").write(b"MZ")
        for preset in ("Default", "Normal", "Hard"):
            values = dict(PROFILE_KEYS)
            if preset == "Hard":
                values["enemyVarietyTrackBar"] = "6"
            # like the real install, the non-default presets carry an older build line
            header = HEADER if preset == "Default" else "RE4 Randomizer - Test Build"
            rb.write_profile(os.path.join(rando, "Profiles", "PC", preset), header, values)
    return game


def write_seedlog(rando: str, name: str, values: dict) -> None:
    folder = os.path.join(rando, "Seedlogs", name)
    os.makedirs(folder)
    rb.write_profile(os.path.join(folder, "Settings.cfg"), HEADER, values)


class TestProfiles(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory()
        self.game = make_game(self.tmp.name)
        self.rando = rb.rando_folder(self.game)

    def tearDown(self) -> None:
        self.tmp.cleanup()

    def test_found(self) -> None:
        self.assertTrue(rb.is_game_folder(self.game))
        self.assertEqual(rb.find_game_folder(self.game), os.path.normpath(self.game))
        self.assertIsNotNone(self.rando)

    def test_profile_is_safe_and_keeps_header(self) -> None:
        settings = rb.rando_settings(2, True, False, True, False, 42)
        unknown = rb.apply_profile(self.rando, settings)
        self.assertEqual(unknown, [])
        for name in (rb.AP_PROFILE, rb.LATEST_PROFILE):
            header, values = rb.read_profile(os.path.join(self.rando, "Profiles", "PC", name))
            self.assertEqual(header, HEADER, "header must match the user's randomizer build")
            for k in rb.VERIFY_KEYS + ("assignmentLeonModeCheckBox",):
                self.assertEqual(values[k], "0", k)
            self.assertEqual(values["seedHashCheckBox"], "1")
            self.assertEqual(values["seedHashNumericBox"], "42")
            self.assertEqual(values["randomEnemyHealthCheckBox"], "0")
            self.assertEqual(values["enemyVarietyTrackBar"], "6", "Hard preset used as the base")
            self.assertEqual(values["minimumRoomsNumericUpDown"], "7", "untouched keys preserved")
            self.assertEqual(list(values)[:2], ["LanguageIndex", "seedHashCheckBox"], "key order preserved")

    def test_existing_latest_profile_backed_up(self) -> None:
        latest = os.path.join(self.rando, "Profiles", "PC", rb.LATEST_PROFILE)
        rb.write_profile(latest, HEADER, {"mine": "1"})
        rb.apply_profile(self.rando, rb.rando_settings(0, True, True, True, False, 5))
        self.assertTrue(os.path.isfile(latest + ".pre-archipelago"))
        _h, backup = rb.read_profile(latest + ".pre-archipelago")
        self.assertEqual(backup, {"mine": "1"})

    def test_unknown_keys_reported(self) -> None:
        settings = rb.rando_settings(0, True, True, True, False, 5)
        settings["overrides"]["someFutureCheckBox"] = "1"
        self.assertEqual(rb.apply_profile(self.rando, settings), ["someFutureCheckBox"])

    def test_generated_seed_checks(self) -> None:
        settings = rb.rando_settings(0, True, True, True, False, 7)
        self.assertEqual(rb.check_generated(self.rando, settings)[0], "missing")
        unsafe = dict(PROFILE_KEYS)
        write_seedlog(self.rando, "2026-10-04_10-00-00", unsafe)
        self.assertEqual(rb.check_generated(self.rando, settings)[0], "unsafe")
        _h, good = rb.build_profile(self.rando, settings)[:2]
        write_seedlog(self.rando, "2026-10-04_11-00-00", good)
        os.utime(os.path.join(self.rando, "Seedlogs", "2026-10-04_11-00-00", "Settings.cfg"), (2e9, 2e9))
        self.assertEqual(rb.check_generated(self.rando, settings)[0], "ok")
        other = rb.rando_settings(0, True, True, True, False, 8)
        self.assertEqual(rb.check_generated(self.rando, other)[0], "mismatch")


class TestInstall(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory()
        self.game = make_game(self.tmp.name, with_rando=False)
        self.files = os.path.join(self.tmp.name, "game_files")
        os.makedirs(os.path.join(self.files, "re4_tweaks"))
        open(os.path.join(self.files, "dinput8.dll"), "wb").write(b"MZ ours " + rb.MOD_MARKER)
        open(os.path.join(self.files, "dinput8.ini"), "w").write("[DISPLAY]\nFOVAdditional = 99\n")
        open(os.path.join(self.files, "re4_tweaks", "trainer.ini"), "w").write("x")

    def tearDown(self) -> None:
        self.tmp.cleanup()

    def test_install_backs_up_and_keeps_ini(self) -> None:
        bin32 = os.path.join(self.game, "Bin32")
        self.assertFalse(rb.mod_installed(self.game))
        written = rb.install_mod(self.game, self.files)
        self.assertTrue(rb.mod_installed(self.game))
        self.assertIn("dinput8.dll", written)
        self.assertEqual(open(os.path.join(bin32, "dinput8.dll.pre-archipelago"), "rb").read(),
                         b"MZ upstream re4_tweaks 1.9.1.0")
        self.assertIn("FOVAdditional = 0", open(os.path.join(bin32, "dinput8.ini")).read(),
                      "existing dinput8.ini (randomizer/user settings) must not be replaced")
        self.assertTrue(os.path.isfile(os.path.join(bin32, "re4_tweaks", "trainer.ini")))
        # reinstalling doesn't overwrite the original backup with our own dll
        rb.install_mod(self.game, self.files)
        self.assertEqual(open(os.path.join(bin32, "dinput8.dll.pre-archipelago"), "rb").read(),
                         b"MZ upstream re4_tweaks 1.9.1.0")

    def test_steam_library_parsing(self) -> None:
        steam = os.path.join(self.tmp.name, "Steam")
        lib2 = os.path.join(self.tmp.name, "Games")
        os.makedirs(os.path.join(steam, "steamapps"))
        os.makedirs(os.path.join(lib2, "steamapps", "common"))
        os.replace(self.game, os.path.join(lib2, "steamapps", "common", "Resident Evil 4"))
        with open(os.path.join(steam, "steamapps", "libraryfolders.vdf"), "w") as f:
            f.write('"libraryfolders"\n{\n "0" { "path" "%s" }\n "1" { "path" "%s" }\n}\n'
                    % (steam.replace("\\", "\\\\"), lib2.replace("\\", "\\\\")))
        libs = rb.steam_library_folders(steam)
        self.assertIn(os.path.normpath(lib2), libs)
        self.assertEqual(rb.find_game_in_library(lib2),
                         os.path.join(lib2, "steamapps", "common", "Resident Evil 4"))


class TestSlotData(RE4TestBase):
    options = {"re_duke_randomizer": True, "re_duke_preset": "hard", "re_duke_merchant": False}

    def test_slot_data(self) -> None:
        rd = self.world.fill_slot_data()["re_duke"]
        self.assertEqual(rd["preset"], "Hard")
        o = rd["overrides"]
        for k in rb.VERIFY_KEYS:
            self.assertEqual(o[k], "0")
        self.assertEqual(o["randomizeMerchantStockCheckBox"], "0")
        self.assertTrue(1 <= int(o["seedHashNumericBox"]) <= 99)
        names = {l.name for l in self.multiworld.get_locations(self.player)}
        # the randomizer leaves the Merchant alone here, so Merchant checks stay
        self.assertTrue(any(n.startswith("Merchant:") for n in names))


class TestNoReDuke(RE4TestBase):
    def test_no_rando_data(self) -> None:
        self.assertIsNone(self.world.fill_slot_data()["re_duke"])


class TestClientSetupFlow(unittest.TestCase):
    """The client's /setup on a fake PC: installs the bundled mod, writes the profile, opens the randomizer,
    and the connect-time check follows the generated seed."""

    def test_flow(self) -> None:
        import asyncio
        from unittest import mock
        from .. import client as C

        with tempfile.TemporaryDirectory() as tmp:
            game = make_game(tmp)
            files = os.path.join(tmp, "game_files")
            os.makedirs(files)
            open(os.path.join(files, "dinput8.dll"), "wb").write(b"MZ " + rb.MOD_MARKER)
            launched = []
            messages = []

            async def run() -> None:
                ctx = C.RE4Context(None, None)
                ctx.slot_data = {"re_duke": rb.rando_settings(1, True, True, False, False, 33)}
                with mock.patch.object(C, "bundled_game_files", lambda: files), \
                        mock.patch.object(C, "get_game_folder_setting", lambda: None), \
                        mock.patch.object(C, "set_game_folder_setting", lambda f: None), \
                        mock.patch.object(rb, "launch_rando", lambda folder: launched.append(folder)), \
                        mock.patch.object(C.sys, "platform", "win32"), \
                        mock.patch.object(C.logger, "info", lambda m, *a: messages.append(("info", m))), \
                        mock.patch.object(C.logger, "warning", lambda m, *a: messages.append(("warn", m))), \
                        mock.patch.object(C.logger, "error", lambda m, *a: messages.append(("error", m))):
                    ctx.run_setup(game)
                    self.assertTrue(rb.mod_installed(game))
                    self.assertEqual(len(launched), 1)
                    messages.clear()
                    ctx.check_local_setup()  # nothing generated yet
                    self.assertTrue(any(k == "warn" and "No randomizer seed" in m for k, m in messages), messages)
                    rando = rb.rando_folder(game)
                    _h, values = rb.read_profile(os.path.join(rando, "Profiles", "PC", rb.AP_PROFILE))
                    write_seedlog(rando, "2026-10-04_12-00-00", values)
                    messages.clear()
                    ctx.check_local_setup()  # generated with the Archipelago profile
                    self.assertTrue(any(k == "info" and "matches this slot" in m for k, m in messages), messages)
                    self.assertFalse(any(k in ("warn", "error") for k, _m in messages), messages)

            asyncio.run(run())


class TestUnsafeRandoWithoutReDukeYaml(unittest.TestCase):
    """re_duke's randomizer installed with its item randomizer on, slot doesn't use it: warn (client + game)."""

    def test_warns(self) -> None:
        import asyncio
        from unittest import mock
        from .. import client as C

        with tempfile.TemporaryDirectory() as tmp:
            game = make_game(tmp, dll=b"MZ " + rb.MOD_MARKER)
            rando = rb.rando_folder(game)
            write_seedlog(rando, "2026-10-04_17-00-00", dict(PROFILE_KEYS))  # items/doors randomized
            warnings, sent = [], []

            async def run() -> None:
                ctx = C.RE4Context(None, None)
                ctx.slot_data = {"re_duke": None}
                with mock.patch.object(C, "get_game_folder_setting", lambda: game), \
                        mock.patch.object(C, "set_game_folder_setting", lambda f: None), \
                        mock.patch.object(ctx, "send_game", lambda m: sent.append(m)), \
                        mock.patch.object(C.logger, "warning", lambda m, *a: warnings.append(m)), \
                        mock.patch.object(C.logger, "info", lambda m, *a: None):
                    ctx.check_local_setup()
                    self.assertTrue(any("breaks Archipelago" in w and "/rando" in w for w in warnings), warnings)
                    self.assertTrue(any(m.get("cmd") == "message" and "/rando" in m.get("text", "") for m in sent))
                    # /rando writes an enemies-only safe profile, Merchant untouched
                    with mock.patch.object(rb, "launch_rando", lambda folder: None):
                        self.assertTrue(ctx.prepare_rando(launch=False, game=game))
                    _h, v = rb.read_profile(os.path.join(rando, "Profiles", "PC", rb.AP_PROFILE))
                    self.assertEqual(v["randomizeItemsCheckBox"], "0")
                    self.assertEqual(v["randomizeDoorsCheckBox"], "0")
                    self.assertEqual(v["randomizeMerchantStockCheckBox"], "0")
                    self.assertEqual(v["randomizeEnemiesCheckBox"], "1")

            asyncio.run(run())
