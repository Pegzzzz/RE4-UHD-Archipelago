"""Glue between this APWorld and the user's local install.

- finds the Resident Evil 4 game folder (Steam library or a configured path)
- installs the bundled game mod files (dinput8.dll + re4_tweaks data) into Bin32, keeping a backup
- drives re_duke's RE4 PC Randomizer (installed separately by the user, inside the game folder):
  writes an Archipelago-safe settings profile, launches the generator, and checks the generated seed.

Everything here is plain file handling so it can be unit-tested on any OS.
"""
import datetime
import os
import re
import shutil
import subprocess
import sys
from typing import Dict, List, Optional, Tuple

STEAM_APP_ID = "254700"
RANDO_FOLDER = "RE4_PC_Randomizer"
RANDO_EXE = "RE4RND_v2.exe"
PROFILE_DIR = os.path.join("Profiles", "PC")
AP_PROFILE = "Archipelago"
LATEST_PROFILE = "LatestGenerated"
MOD_MARKER = b"archipelago_config.json"  # string only present in our build of dinput8.dll

# Settings that would break Archipelago's item logic or location table. Always written, whatever the preset.
FORCED_SETTINGS: Dict[str, str] = {
    "randomizeDoorsCheckBox": "0",        # door rando changes the room graph the logic is built on
    "randomizeItemsCheckBox": "0",        # Archipelago is the item randomizer
    "randomizeKeyItemsCheckBox": "0",
    "enemiesDropKeyItemsCheckBox": "0",
    "assignmentLeonModeCheckBox": "0",    # door-rando game mode
    "seedHashCheckBox": "1",              # same enemies every time this slot regenerates
    "saveSettingsCheckBox": "1",
    "openLogCheckBox": "0",
}
# Settings Archipelago reads back from a generated seed to confirm it is safe to play.
VERIFY_KEYS = ("randomizeDoorsCheckBox", "randomizeItemsCheckBox", "randomizeKeyItemsCheckBox",
               "enemiesDropKeyItemsCheckBox")

PRESETS = {0: "Default", 1: "Normal", 2: "Hard"}


# ------------------------------------------------------------------------------------------- slot side
def rando_settings(preset: int, enemies: bool, enemy_health: bool, merchant: bool, loadout: bool,
                   seed_hash: int) -> Dict[str, object]:
    """What the APWorld puts in slot_data: which preset to start from and which keys to override."""
    overrides = dict(FORCED_SETTINGS)
    overrides.update({
        "randomizeEnemiesCheckBox": "1" if enemies else "0",
        "randomEnemyHealthCheckBox": "1" if enemy_health else "0",
        "randomizeMerchantStockCheckBox": "1" if merchant else "0",
        "randomWeaponPriceCheckBox": "1" if merchant else "0",
        "randomHerbGrenadeCheckBox": "1" if merchant else "0",
        "randomAttacheCasePriceCheckBox": "1" if merchant else "0",
        "randomFirepowerCheckBox": "1" if merchant else "0",
        "randomCapacityCheckBox": "1" if merchant else "0",
        "randomInventoryCheckBox": "1" if loadout else "0",
        "seedHashNumericBox": str(seed_hash),
    })
    return {"preset": PRESETS.get(preset, "Default"), "overrides": overrides}


# ------------------------------------------------------------------------------------------- game folder
def _steam_roots() -> List[str]:
    roots: List[str] = []
    if sys.platform == "win32":
        try:
            import winreg
            for hive, key in ((winreg.HKEY_CURRENT_USER, r"Software\Valve\Steam"),
                              (winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\WOW6432Node\Valve\Steam")):
                try:
                    with winreg.OpenKey(hive, key) as k:
                        for name in ("SteamPath", "InstallPath"):
                            try:
                                roots.append(winreg.QueryValueEx(k, name)[0])
                            except OSError:
                                pass
                except OSError:
                    pass
        except ImportError:
            pass
        roots += [r"C:\Program Files (x86)\Steam", r"C:\Program Files\Steam"]
    else:
        roots += [os.path.expanduser("~/.steam/steam"), os.path.expanduser("~/.local/share/Steam")]
    return [r for r in dict.fromkeys(os.path.normpath(r) for r in roots) if os.path.isdir(r)]


def steam_library_folders(steam_root: str) -> List[str]:
    libs = [steam_root]
    vdf = os.path.join(steam_root, "steamapps", "libraryfolders.vdf")
    try:
        with open(vdf, encoding="utf-8", errors="replace") as f:
            for m in re.finditer(r'"path"\s+"([^"]+)"', f.read()):
                libs.append(m.group(1).replace("\\\\", "\\"))
    except OSError:
        pass
    return list(dict.fromkeys(os.path.normpath(p) for p in libs))


def find_game_in_library(library: str) -> Optional[str]:
    apps = os.path.join(library, "steamapps")
    acf = os.path.join(apps, f"appmanifest_{STEAM_APP_ID}.acf")
    installdir = "Resident Evil 4"
    try:
        with open(acf, encoding="utf-8", errors="replace") as f:
            m = re.search(r'"installdir"\s+"([^"]+)"', f.read())
            if m:
                installdir = m.group(1)
    except OSError:
        pass
    folder = os.path.join(apps, "common", installdir)
    return folder if is_game_folder(folder) else None


def is_game_folder(folder: str) -> bool:
    return os.path.isfile(os.path.join(folder, "Bin32", "bio4.exe")) or \
        os.path.isfile(os.path.join(folder, "Bin32", "Bio4.exe"))


def find_game_folder(configured: Optional[str] = None) -> Optional[str]:
    if configured and is_game_folder(configured):
        return os.path.normpath(configured)
    for root in _steam_roots():
        for lib in steam_library_folders(root):
            found = find_game_in_library(lib)
            if found:
                return found
    return None


# ------------------------------------------------------------------------------------------- game mod
def mod_installed(game: str) -> bool:
    dll = os.path.join(game, "Bin32", "dinput8.dll")
    try:
        with open(dll, "rb") as f:
            return MOD_MARKER in f.read()
    except OSError:
        return False


def install_mod(game: str, files_dir: str) -> List[str]:
    """Copy the bundled game files into Bin32. Existing files that would be replaced are backed up once
    (dinput8.dll -> dinput8.dll.pre-archipelago), and our dinput8.ini is never written over an existing one,
    because re_duke's randomizer and the user both keep settings there. Returns the files written."""
    bin32 = os.path.join(game, "Bin32")
    written: List[str] = []
    for root, _dirs, files in os.walk(files_dir):
        rel_root = os.path.relpath(root, files_dir)
        for name in files:
            rel = os.path.normpath(os.path.join(rel_root, name))
            dst = os.path.join(bin32, rel)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            if rel.lower().endswith(".ini") and os.path.exists(dst):
                continue
            if rel.lower() == "dinput8.dll" and os.path.exists(dst) and not mod_installed(game):
                backup = dst + ".pre-archipelago"
                if not os.path.exists(backup):
                    shutil.copy2(dst, backup)
            shutil.copy2(os.path.join(root, name), dst)
            written.append(rel)
    return written


# ------------------------------------------------------------------------------------------- re_duke rando
def rando_folder(game: str) -> Optional[str]:
    folder = os.path.join(game, RANDO_FOLDER)
    return folder if os.path.isfile(os.path.join(folder, RANDO_EXE)) else None


def read_profile(path: str) -> Tuple[str, Dict[str, str]]:
    """A profile is a header line ("RE4 Randomizer - <build>") followed by key=value lines, in order."""
    with open(path, encoding="utf-8-sig", errors="replace") as f:
        lines = f.read().splitlines()
    header = lines[0] if lines else ""
    values: Dict[str, str] = {}
    for line in lines[1:]:
        if "=" in line:
            k, v = line.split("=", 1)
            values[k.strip()] = v.strip()
    return header, values


def write_profile(path: str, header: str, values: Dict[str, str]) -> None:
    with open(path, "w", encoding="utf-8", newline="\r\n") as f:
        f.write(header + "\n")
        for k, v in values.items():
            f.write(f"{k}={v}\n")


def build_profile(rando: str, settings: Dict[str, object]) -> Tuple[str, Dict[str, str], List[str]]:
    """Start from the user's own preset file (so the header matches their randomizer build and every key
    their version knows is present), then apply our overrides. Returns header, values and any override
    keys this randomizer version doesn't know (they are still written; the randomizer ignores unknown keys)."""
    preset = str(settings.get("preset", "Default"))
    base = os.path.join(rando, PROFILE_DIR, preset)
    if not os.path.isfile(base):
        base = os.path.join(rando, PROFILE_DIR, "Default")
    header, values = read_profile(base)
    default = os.path.join(rando, PROFILE_DIR, "Default")
    if os.path.isfile(default):
        header = read_profile(default)[0]  # the shipped presets can carry an older build line; Default is current
    unknown = []
    for k, v in dict(settings.get("overrides", {})).items():
        if k not in values:
            unknown.append(k)
        values[k] = str(v)
    values["profileListComboBox"] = "-1"
    return header, values, unknown


def apply_profile(rando: str, settings: Dict[str, object]) -> List[str]:
    """Write the Archipelago profile, and make it the one the randomizer opens with."""
    header, values, unknown = build_profile(rando, settings)
    folder = os.path.join(rando, PROFILE_DIR)
    write_profile(os.path.join(folder, AP_PROFILE), header, values)
    latest = os.path.join(folder, LATEST_PROFILE)
    if os.path.isfile(latest) and not os.path.isfile(latest + ".pre-archipelago"):
        shutil.copy2(latest, latest + ".pre-archipelago")
    write_profile(latest, header, values)
    return unknown


def launch_rando(rando: str) -> subprocess.Popen:
    # the randomizer resolves its files relative to its own folder (./Tools, ../Bin32)
    return subprocess.Popen([os.path.join(rando, RANDO_EXE)], cwd=rando)


def latest_seedlog(rando: str) -> Optional[str]:
    logs = os.path.join(rando, "Seedlogs")
    try:
        entries = [os.path.join(logs, d) for d in os.listdir(logs)]
    except OSError:
        return None
    candidates = [d for d in entries if os.path.isfile(os.path.join(d, "Settings.cfg"))]
    if not candidates:
        return None
    return max(candidates, key=lambda d: os.path.getmtime(os.path.join(d, "Settings.cfg")))


def check_generated(rando: str, settings: Dict[str, object]) -> Tuple[str, str]:
    """Look at the most recent seed the randomizer generated.
    Returns (status, message); status is "ok", "missing", "mismatch" or "unsafe"."""
    log = latest_seedlog(rando)
    if not log:
        return "missing", "No randomizer seed has been generated yet."
    _header, used = read_profile(os.path.join(log, "Settings.cfg"))
    unsafe = [k for k in VERIFY_KEYS if used.get(k, "0") != "0"]
    if unsafe:
        return "unsafe", ("The last randomizer seed has " + ", ".join(unsafe) + " turned on, which breaks "
                          "Archipelago's item logic. Generate again with the Archipelago profile.")
    wanted = dict(settings.get("overrides", {}))
    differs = [k for k in ("randomizeEnemiesCheckBox", "seedHashNumericBox", "randomizeMerchantStockCheckBox")
               if k in wanted and k in used and used[k] != str(wanted[k])]
    if differs:
        return "mismatch", ("The last randomizer seed was generated with different settings than this slot "
                            "(" + ", ".join(differs) + "). It's safe to play, but generate again for the intended setup.")
    when = datetime.datetime.fromtimestamp(os.path.getmtime(os.path.join(log, "Settings.cfg")))
    return "ok", f"Randomizer seed generated {when:%Y-%m-%d %H:%M} matches this slot."
