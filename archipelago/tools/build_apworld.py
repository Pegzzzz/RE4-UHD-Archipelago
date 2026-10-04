"""Packages the world with Archipelago's own "Build APWorlds" component.
Run from an Archipelago checkout that has worlds/re4uhd in place:  python <path>/build_apworld.py
Output: build/apworlds/re4uhd.apworld
"""
import inspect
import os
import sys

sys.path.insert(0, os.getcwd())
os.environ.setdefault("SKIP_REQUIREMENTS_UPDATE", "1")

import Launcher  # noqa: E402

Launcher.open_folder = lambda *args, **kwargs: None  # headless: don't try to open the output folder

from worlds.LauncherComponents import components  # noqa: E402

build = next(c for c in components if c.display_name == "Build APWorlds")
args = ["Resident Evil 4 UHD"]
if "skip_open_folder" in inspect.getsource(build.func):
    args.append("--skip_open_folder")
build.func(*args)
