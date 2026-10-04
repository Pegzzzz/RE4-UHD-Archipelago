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
import zlib
from typing import Any, Dict, List, Optional

import Utils
from CommonClient import (ClientCommandProcessor, CommonContext, get_base_parser, gui_enabled, handle_url_arg,
                          logger, server_loop)
from NetUtils import ClientStatus, NetworkItem

GAME_NAME = "Resident Evil 4 UHD"
GAME_HOST = "127.0.0.1"
GAME_PORT = 46400
PROTOCOL_VERSION = 1


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
