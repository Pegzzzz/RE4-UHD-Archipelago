"""Resident Evil 4 UHD client.

Bridges the Archipelago server and the game mod (re4_tweaks + Archipelago module).
The mod listens on 127.0.0.1:46400 and speaks newline-delimited JSON:

client -> game
  {"cmd": "config", "seed": str, "slot": str, "save_tag": int, "slot_data": {...}}
  {"cmd": "items", "items": [{"i": index, "id": ap_item_id, "from": str}]}   full list
  {"cmd": "checked", "locations": [ids]}                                       server-side checks
  {"cmd": "message", "text": str}
  {"cmd": "kill", "cause": str}                                                DeathLink
  {"cmd": "bind_save"}                                                         re-bind save to seed

game -> client
  {"cmd": "hello", "version": int, "save_tag": int, "received": int}
  {"cmd": "check", "locations": [ids]}
  {"cmd": "goal"}
  {"cmd": "death"}
  {"cmd": "log", "text": str}
"""
import asyncio
import json
import os
import sys
import zipfile
import zlib
from typing import Any, Dict, List, Optional

import Utils
from CommonClient import (ClientCommandProcessor, CommonContext, get_base_parser, gui_enabled, handle_url_arg,
                          logger, server_loop)
from NetUtils import ClientStatus, NetworkItem

from . import rando_bridge

GAME_NAME = "Resident Evil 4 UHD"
GAME_HOST = "127.0.0.1"
GAME_PORT = 46400
PROTOCOL_VERSION = 1


def get_game_folder_setting() -> Optional[str]:
    try:
        from settings import get_settings
        value = get_settings()["re4uhd_options"]["game_folder"]
        return str(value) if value else None
    except Exception:
        return None


def set_game_folder_setting(folder: str) -> None:
    try:
        from settings import get_settings
        s = get_settings()
        s["re4uhd_options"]["game_folder"] = folder
        s.save()
    except Exception as e:
        logger.debug(f"couldn't save the game folder to host.yaml: {e}")


def bundled_game_files() -> Optional[str]:
    """The game mod files shipped inside this APWorld (release builds), as a folder on disk."""
    here = os.path.dirname(os.path.abspath(__file__))
    folder = os.path.join(here, "game_files")
    if os.path.isdir(folder):
        return folder
    # packaged .apworld: extract once to a cache folder
    apworld = os.path.dirname(here)
    if not zipfile.is_zipfile(apworld):
        return None
    with zipfile.ZipFile(apworld) as z:
        names = [n for n in z.namelist() if n.startswith("re4uhd/game_files/") and not n.endswith("/")]
        if not names:
            return None
        stamp = str(zlib.crc32(b"".join(z.getinfo(n).CRC.to_bytes(4, "little") for n in names)))
        cache = Utils.user_path("re4uhd_game_files", stamp)
        if not os.path.isdir(cache):
            for n in names:
                target = os.path.join(cache, *n[len("re4uhd/game_files/"):].split("/"))
                os.makedirs(os.path.dirname(target), exist_ok=True)
                with z.open(n) as src, open(target, "wb") as dst:
                    dst.write(src.read())
        return cache


def save_tag(seed_name: str, slot: int, team: int) -> int:
    """32-bit tag written into the game save so a save can't be mixed up with another seed."""
    return zlib.crc32(f"{seed_name}:{team}:{slot}".encode()) or 1


class RE4CommandProcessor(ClientCommandProcessor):
    def _cmd_game(self) -> bool:
        """Show the connection state of the game mod."""
        if isinstance(self.ctx, RE4Context):
            state = "connected" if self.ctx.game_writer else "not connected"
            logger.info(f"Game mod: {state}. Save received-item index: {self.ctx.game_received}")
        return True

    def _cmd_setup(self, folder: str = "") -> bool:
        """Set up the game: install the Archipelago game mod, and (if your YAML uses re_duke_randomizer)
        prepare re_duke's randomizer and open it so you can click Generate Seed.
        Optional: the game folder, if it isn't found automatically (the folder that contains Bin32)."""
        if isinstance(self.ctx, RE4Context):
            self.ctx.run_setup(folder.strip().strip('"') or None)
        return True

    def _cmd_rando(self) -> bool:
        """Write the Archipelago profile for re_duke's randomizer and open it (then click Generate Seed)."""
        if isinstance(self.ctx, RE4Context):
            self.ctx.prepare_rando(launch=True)
        return True

    def _cmd_bindsave(self) -> bool:
        """Bind the currently loaded game save to this seed (use only if you are sure it is the right save)."""
        if isinstance(self.ctx, RE4Context):
            self.ctx.send_game({"cmd": "bind_save"})
            logger.info("Asked the game to bind the loaded save to this seed.")
        return True


class RE4Context(CommonContext):
    command_processor = RE4CommandProcessor
    game = GAME_NAME
    items_handling = 0b111  # every item, including our own, comes from the server

    def __init__(self, server_address: Optional[str], password: Optional[str]) -> None:
        super().__init__(server_address, password)
        self.slot_data: Dict[str, Any] = {}
        self.game_writer: Optional[asyncio.StreamWriter] = None
        self.game_received = -1
        self.game_task: Optional[asyncio.Task] = None
        self.goal_sent = False
        self.goal_pending = False
        self.known_game_folder: Optional[str] = None

    # ---- local setup -------------------------------------------------------------
    def game_folder(self, explicit: Optional[str] = None) -> Optional[str]:
        stored = get_game_folder_setting()
        folder = rando_bridge.find_game_folder(explicit or self.known_game_folder or stored)
        if folder:
            self.known_game_folder = folder
            if folder != stored:
                set_game_folder_setting(folder)
        return folder

    def run_setup(self, explicit: Optional[str] = None) -> None:
        game = self.game_folder(explicit)
        if not game:
            logger.error("Couldn't find Resident Evil 4. Type /setup followed by the game folder, for example:\n"
                         '  /setup "C:\\Program Files (x86)\\Steam\\steamapps\\common\\Resident Evil 4"')
            return
        logger.info(f"Game folder: {game}")
        files = bundled_game_files()
        if files:
            try:
                written = rando_bridge.install_mod(game, files)
                logger.info(f"Installed the Archipelago game mod into Bin32 ({len(written)} files). "
                            "Your previous dinput8.dll, if any, was kept as dinput8.dll.pre-archipelago.")
            except OSError as e:
                logger.error(f"Couldn't install the game mod (is the game running?): {e}")
                return
        elif not rando_bridge.mod_installed(game):
            logger.warning("The game mod isn't installed: extract RE4-UHD-Archipelago.zip into Bin32 "
                           "(this APWorld build doesn't include the game files).")
        else:
            logger.info("Archipelago game mod is installed.")
        if self.slot_data.get("re_duke") or (self.slot_data and rando_bridge.rando_folder(game)):
            self.prepare_rando(launch=True, game=game)
        elif not self.slot_data:
            logger.info("Connect to the room to also set up re_duke's randomizer, if you use it.")

    def prepare_rando(self, launch: bool, game: Optional[str] = None) -> bool:
        settings = self.slot_data.get("re_duke")
        if not settings:
            if not self.slot_data:
                logger.info("Connect to the room first, so the profile can match your slot.")
                return False
            # slot doesn't use it, but it may be installed anyway: random enemies only, Merchant left alone so
            # this slot's Merchant checks still work, items/doors off
            settings = rando_bridge.rando_settings(0, True, True, False, False,
                                                   1 + zlib.crc32((self.seed_name or "").encode()) % 99)
            logger.info("Your YAML doesn't use re_duke's randomizer (re_duke_randomizer: false), so the profile only "
                        "randomizes enemies and keeps items, doors and the Merchant normal.")
        game = game or self.game_folder()
        if not game:
            logger.error("Couldn't find Resident Evil 4; run /setup with the game folder first.")
            return False
        rando = rando_bridge.rando_folder(game)
        if not rando:
            logger.error(f"re_duke's randomizer isn't in the game folder. Extract it there so that "
                         f"{os.path.join(game, rando_bridge.RANDO_FOLDER, rando_bridge.RANDO_EXE)} exists "
                         "(get it from moddb.com/mods/re4randomizer or re_duke's Patreon), then run /rando.")
            return False
        try:
            unknown = rando_bridge.apply_profile(rando, settings)
        except OSError as e:
            logger.error(f"Couldn't write the randomizer profile: {e}")
            return False
        if unknown:
            logger.warning("This randomizer version doesn't know these settings (they'll be ignored): "
                           + ", ".join(unknown))
        logger.info("Wrote the 'Archipelago' profile for re_duke's randomizer: random enemies etc. as in your "
                    "YAML; doors, items and key items OFF (Archipelago places the items).")
        if launch:
            if sys.platform != "win32":
                logger.info(f"Open {rando_bridge.RANDO_EXE} yourself and click Generate Seed.")
            else:
                try:
                    rando_bridge.launch_rando(rando)
                    logger.info("Opened the randomizer. Check that the 'Archipelago' settings are loaded, click "
                                "Generate Seed, wait for 'Seed generated correctly', then start the game. "
                                "Type /rando again any time to redo this.")
                except OSError as e:
                    logger.error(f"Couldn't start the randomizer: {e}")
        return True

    def check_local_setup(self) -> None:
        game = self.game_folder()
        if not game:
            logger.info("Tip: type /setup to install the game mod (and set up re_duke's randomizer).")
            return
        if not rando_bridge.mod_installed(game):
            logger.warning("The Archipelago game mod isn't installed in this game folder. Type /setup to install it.")
        settings = self.slot_data.get("re_duke")
        rando = rando_bridge.rando_folder(game)
        if settings:
            if not rando:
                logger.warning("Your YAML uses re_duke's randomizer, but it isn't in the game folder. "
                               "See the tutorial, then type /setup.")
                return
            status, message = rando_bridge.check_generated(rando, settings)
            (logger.info if status == "ok" else logger.warning)("re_duke randomizer: " + message +
                                                               ("" if status == "ok" else " Type /rando."))
            if status == "unsafe":
                self.send_game({"cmd": "message", "text": "re_duke's randomizer moved items/doors: checks won't "
                                                         "line up! Type /rando in the client."})
        elif rando:
            # installed even though this slot doesn't use it: its last seed must still leave items alone
            status, message = rando_bridge.check_generated(rando, rando_bridge.rando_settings(0, True, True, False,
                                                                                               False, 1))
            if status == "unsafe":
                logger.warning("re_duke randomizer: " + message + " Type /rando to write a safe profile "
                               "(random enemies only), then click Generate Seed.")
                self.send_game({"cmd": "message", "text": "re_duke's randomizer moved items/doors: checks won't "
                                                         "line up! Type /rando in the client."})
            elif status != "missing":
                logger.info("re_duke's randomizer is installed. Its items and doors are off, which is what "
                            "Archipelago needs. For random enemies with Merchant/boss checks handled for you, set "
                            "re_duke_randomizer: true in your next YAML.")

    # ---- Archipelago side ------------------------------------------------------
    async def server_auth(self, password_requested: bool = False) -> None:
        if password_requested and not self.password:
            await super().server_auth(password_requested)
        await self.get_username()
        await self.send_connect()

    def on_package(self, cmd: str, args: Dict[str, Any]) -> None:
        if cmd == "Connected":
            self.slot_data = args.get("slot_data", {}) or {}
            if self.slot_data.get("death_link"):
                Utils.async_start(self.update_death_link(True))
            Utils.async_start(self.send_msgs([{
                "cmd": "LocationScouts", "locations": list(self.missing_locations | self.checked_locations),
                "create_as_hint": 0}]))
            self.goal_sent = False
            Utils.async_start(self.send_goal())
            self.send_config()
            self.send_items()
            self.send_checked()
            try:
                self.check_local_setup()
            except Exception as e:  # never let a local-file problem break the connection
                logger.debug(f"local setup check failed: {e}")
        elif cmd == "ReceivedItems":
            self.send_items()
        elif cmd == "RoomUpdate":
            if "checked_locations" in args:
                self.send_checked()

    def on_print_json(self, args: Dict[str, Any]) -> None:
        super().on_print_json(args)
        if args.get("type") != "ItemSend" or "item" not in args:
            return
        item: NetworkItem = args["item"]
        receiving = args.get("receiving")
        if item.player == self.slot and receiving != self.slot:
            name = self.item_names.lookup_in_slot(item.item, receiving)
            self.send_game({"cmd": "message", "text": f"Sent {name} to {self.player_names[receiving]}"})

    def on_deathlink(self, data: Dict[str, Any]) -> None:
        super().on_deathlink(data)
        self.send_game({"cmd": "kill", "cause": data.get("cause") or f"{data.get('source', 'Someone')} died"})

    def config_dict(self) -> Dict[str, Any]:
        return {
            "cmd": "config",
            "seed": self.seed_name or "",
            "slot": self.auth or "",
            "save_tag": save_tag(self.seed_name or "", self.slot or 0, self.team or 0),
            "slot_data": self.slot_data,
        }

    def send_config(self) -> None:
        if self.slot_data:
            self.send_game(self.config_dict())

    def send_items(self) -> None:
        if not self.slot_data:
            return
        items: List[Dict[str, Any]] = []
        for index, item in enumerate(self.items_received):
            items.append({"i": index, "id": item.item,
                          "name": self.item_names.lookup_in_slot(item.item, self.slot),
                          "from": "" if item.player == self.slot else self.player_names.get(item.player, "Archipelago")})
        self.send_game({"cmd": "items", "items": items})

    def send_checked(self) -> None:
        if self.slot_data:
            self.send_game({"cmd": "checked", "locations": sorted(self.checked_locations)})

    # ---- game side --------------------------------------------------------------
    def send_game(self, msg: Dict[str, Any]) -> None:
        if self.game_writer is None:
            return
        try:
            self.game_writer.write((json.dumps(msg, separators=(",", ":")) + "\n").encode())
        except Exception as e:  # connection dropped; the reader loop will notice
            logger.debug(f"game write failed: {e}")

    async def handle_game_message(self, msg: Dict[str, Any]) -> None:
        cmd = msg.get("cmd")
        if cmd == "hello":
            self.game_received = msg.get("received", -1)
            if msg.get("version") != PROTOCOL_VERSION:
                logger.warning("Game mod and client versions differ; update both from the same release.")
            logger.info("Game mod connected.")
            self.send_config()
            self.send_items()
            self.send_checked()
        elif cmd == "check":
            locations = [l for l in msg.get("locations", []) if l in self.missing_locations]
            if locations:
                await self.check_locations(locations)
                for l in locations:
                    self.announce_location(l)
        elif cmd == "goal":
            self.goal_pending = True
            await self.send_goal()
        elif cmd == "death":
            if self.slot_data.get("death_link"):
                await self.send_death(f"{self.player_names.get(self.slot, 'Leon')} died in Spain.")
        elif cmd == "received":
            self.game_received = msg.get("received", self.game_received)
        elif cmd == "log":
            logger.info(f"[game] {msg.get('text', '')}")

    async def send_goal(self) -> None:
        if self.goal_pending and not self.goal_sent and self.slot is not None and self.server and self.server.socket:
            self.goal_sent = True
            await self.send_msgs([{"cmd": "StatusUpdate", "status": ClientStatus.CLIENT_GOAL}])
            logger.info("Goal complete!")

    def announce_location(self, location_id: int) -> None:
        info = self.locations_info.get(location_id)
        if info is None:
            return
        name = self.item_names.lookup_in_slot(info.item, info.player)
        if info.player == self.slot:
            self.send_game({"cmd": "message", "text": f"Found {name}"})
        else:
            self.send_game({"cmd": "message", "text": f"Found {name} for {self.player_names[info.player]}"})

    async def game_loop(self) -> None:
        while not self.exit_event.is_set():
            try:
                reader, writer = await asyncio.wait_for(
                    asyncio.open_connection(GAME_HOST, GAME_PORT), timeout=5)
            except (OSError, asyncio.TimeoutError):
                await asyncio.sleep(3)
                continue
            self.game_writer = writer
            try:
                while not self.exit_event.is_set():
                    line = await reader.readline()
                    if not line:
                        break
                    try:
                        msg = json.loads(line)
                    except json.JSONDecodeError:
                        continue
                    await self.handle_game_message(msg)
            except (ConnectionError, OSError):
                pass
            finally:
                self.game_writer = None
                try:
                    writer.close()
                except Exception:
                    pass
                logger.info("Game mod disconnected, retrying...")
            await asyncio.sleep(1)

    def run_gui(self) -> None:
        from kvui import GameManager

        class RE4Manager(GameManager):
            logging_pairs = [("Client", "Archipelago")]
            base_title = "Archipelago Resident Evil 4 UHD Client"

        self.ui = RE4Manager(self)
        self.ui_task = asyncio.create_task(self.ui.async_run(), name="UI")


async def main(args) -> None:
    ctx = RE4Context(args.connect, args.password)
    if getattr(args, "name", None):
        ctx.auth = args.name
    ctx.server_task = asyncio.create_task(server_loop(ctx), name="ServerLoop")
    if gui_enabled:
        ctx.run_gui()
    ctx.run_cli()
    ctx.game_task = asyncio.create_task(ctx.game_loop(), name="GameLoop")
    await ctx.exit_event.wait()
    ctx.server_address = None
    await ctx.shutdown()
    if ctx.game_task:
        ctx.game_task.cancel()


def launch(*launch_args: str) -> None:
    import colorama
    parser = get_base_parser(description="Resident Evil 4 UHD Archipelago client")
    parser.add_argument("url", nargs="?", help="Archipelago connection url")
    args = handle_url_arg(parser.parse_args(launch_args), parser=parser)
    if args.connect and "@" in args.connect:
        args.connect = args.connect.rsplit("@", 1)[1]  # name/password were already taken from the URL
    colorama.just_fix_windows_console()
    asyncio.run(main(args))
    colorama.deinit()
