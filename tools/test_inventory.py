"""Oracle for Minecraft's inventory and commands over Sam (both games running, tools/dev.py mc-start, sam tfe).

    uv run -q --with websockets python tools/test_inventory.py [--game tfe] [--level NAME] [--no-restart] [--shot PNG]

  1. sc_Inventory(1) opens Minecraft's creative inventory over Sam (the player turns creative while it's open);
  2. clicking the tab's first item and then hotbar slot 9 puts that item into the hotbar;
  3. closing it (sc_Inventory(0)) gives Sam the game back and the player is a survival player again;
  4. mc("time set midnight") runs as a Minecraft command and its answer comes back.
Prints PASS/FAIL per check.
"""
import argparse
import asyncio
import json
import time
from pathlib import Path

import websockets

from test_wall import GAMES, SamLog, clean, request, sam, start_level

MC_LOG = Path(__file__).resolve().parents[1] / "mc" / "run" / "logs" / "latest.log"
HERE = Path(__file__).resolve().parent


async def client(ws) -> dict:
    return await request(ws, {"t": "status"}, "client", timeout=3.0)


async def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--game", default="tfe", choices=GAMES)
    ap.add_argument("--level", default="01_Hatshepsut")
    ap.add_argument("--no-restart", action="store_true")
    ap.add_argument("--shot", default="")
    a = ap.parse_args()
    log = SamLog(a.game)
    results = []

    async with websockets.connect("ws://127.0.0.1:25610", max_size=None, max_queue=None, ping_interval=None) as ws:
        await ws.recv()
        if not a.no_restart:
            await start_level(log, a.level)
        sam("cht_bGod=1;", "cht_bInvisible=1;", "con_iLastLines=0;")
        await clean(ws)
        before = await client(ws)
        gw, gh = before["gui"]
        mark = len(MC_LOG.read_text(encoding="utf-8", errors="replace"))

        # 1. open
        sam("sc_Inventory(1);")
        await asyncio.sleep(2.5)
        c = await client(ws)
        results.append(("the creative inventory opens", c["screen"] == "CreativeModeInventoryScreen", f"screen {c['screen'] or 'none'}"))
        if a.shot:
            import subprocess, sys
            subprocess.run([sys.executable, str(HERE / "dev.py"), "shot", a.game, a.shot], check=False)

        # 2. the tab's first item into hotbar slot 9 (CreativeModeInventoryScreen: 195 x 136, items from (9, 18), the
        #    hotbar row at y 112, 18 apart)
        left, top = (gw - 195) // 2, (gh - 136) // 2
        item = ((left + 9 + 8) / gw, (top + 18 + 8) / gh)
        slot9 = ((left + 9 + 18 * 8 + 8) / gw, (top + 112 + 8) / gh)
        sam(f'sc_Click("{item[0]:.4f} {item[1]:.4f} 0");')
        await asyncio.sleep(0.8)
        sam(f'sc_Click("{slot9[0]:.4f} {slot9[1]:.4f} 0");')
        await asyncio.sleep(0.8)

        # 3. close
        sam("sc_Inventory(0);")
        await asyncio.sleep(2.0)
        c = await client(ws)
        changed = c["hotbar"][8] != before["hotbar"][8] and c["hotbar"][8] != ""
        results.append(("an item from the creative inventory lands in the hotbar", changed, f"slot 9: {before['hotbar'][8]!r} -> {c['hotbar'][8]!r}"))
        text = MC_LOG.read_text(encoding="utf-8", errors="replace")[mark:]
        back = "INVENTORY CLOSED: SURVIVAL" in text.upper()
        results.append(("closing it gives the game back, the player is survival again", c["screen"] == "" and back,
                        f"screen {c['screen'] or 'none'}, {'survival' if back else 'mode not restored'}"))

        # 4. a Minecraft command from Sam's console
        mark = len(MC_LOG.read_text(encoding="utf-8", errors="replace"))
        sam('mc("time set midnight");')
        await asyncio.sleep(1.5)
        text = MC_LOG.read_text(encoding="utf-8", errors="replace")[mark:]
        out = [l.split("command output: ", 1)[1] for l in text.splitlines() if "command output: " in l]
        results.append(("mc(...) runs a Minecraft command, its answer comes back", bool(out), out[0] if out else "no output"))
        await ws.send(json.dumps({"t": "cmd", "c": "time set noon"}))
        sam("cht_bInvisible=0;", "con_iLastLines=5;")

    for name, ok, detail in results:
        print(f"{'PASS' if ok else 'FAIL'}: {name} ({detail})")


if __name__ == "__main__":
    asyncio.run(main())
