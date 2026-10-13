"""Oracle for a level change with the bridge busy (both games running; Sam started on a level with a world link).

    uv run -q --with websockets python tools/test_levelchange.py [--game tfe] [--level 01_Hatshepsut] [--link 0]

Minecraft blocks stand near the player (Sam holds block brushes for them), a Kleer is near (Sam holds it for
Minecraft's stand-in), then the level's world link is taken as reaching it would. The level change used to crash
Sam in Engine.dll (references to the old world's entities released after it was gone).
  1. Sam survives the change and announces the next level to Minecraft;
  2. Minecraft's world is reset for it ("world": the inventory stays).
Prints PASS/FAIL per check.
"""
import argparse
import asyncio
import json
import re
import subprocess
import time

import websockets

from test_wall import GAMES, SamLog, clean, request, sam, start_level


def sam_alive() -> bool:
    out = subprocess.run(["tasklist", "/FI", "IMAGENAME eq SeriousSam_Custom.exe"], capture_output=True, text=True).stdout
    return "SeriousSam_Custom.exe" in out


async def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--game", default="tfe", choices=GAMES)
    ap.add_argument("--level", default="01_Hatshepsut")
    ap.add_argument("--link", type=int, default=0)
    a = ap.parse_args()
    log = SamLog(a.game)
    results = []

    async with websockets.connect("ws://127.0.0.1:25610", max_size=None, max_queue=None, ping_interval=None) as ws:
        await ws.recv()
        await start_level(log, a.level)
        sam("cht_bGod=1;", "cht_bInvisible=1;", "con_iLastLines=0;")
        await asyncio.sleep(1.0)
        await clean(ws)

        # Minecraft blocks around the player (Sam builds brushes for them) and a Kleer ahead (Sam holds it)
        st = await request(ws, {"t": "status"}, "status", timeout=3.0)
        x, y, z = (int(v // 1) for v in st["player"]["mc"])
        await ws.send(json.dumps({"t": "cmd", "c": f"fill {x + 2} {y} {z + 2} {x + 4} {y + 2} {z + 4} minecraft:stone_bricks replace air"}))
        sam("sc_fTestTurnH=0;", "sc_fTestTurnP=-2.5;", "sc_iTestTurn=10;")
        await asyncio.sleep(1.5)
        sam('sc_SpawnSam("Boneman");')
        sam("sc_fTestTurnH=0;", "sc_fTestTurnP=2.5;", "sc_iTestTurn=10;")
        await asyncio.sleep(3.0)
        held = await log.after("sc_Status();", "monsters:", timeout=2.0)
        blocks = re.findall(r"blocks: (\d+) solid in Sam as brushes \((\d+) chunks", held)
        monsters = re.findall(r"monsters: (\d+) mirrored", held)
        print(f"before the change: blocks {blocks[-1] if blocks else '?'}, monsters mirrored {monsters[-1] if monsters else '?'}")

        links = await log.after(f"sc_WorldLink(-1);", "world link", timeout=2.0)
        print("\n".join(l for l in links.splitlines() if "world link" in l))
        mark = len(log.text())
        sam(f"sc_WorldLink({a.link});")
        survived, announced = True, False
        end = time.time() + 90
        while time.time() < end:
            await asyncio.sleep(2.0)
            if not sam_alive():
                survived = False
                break
            text = log.text()[mark:]
            if re.search(r"level announced to Minecraft: .*\(sent\)", text):
                announced = True
                await asyncio.sleep(5.0)
                survived = sam_alive()
                break
        new = re.findall(r"level announced to Minecraft: (\S+)", log.text()[mark:])
        results.append(("Sam survives the level change and announces the next level", survived and announced,
                        f"{'alive' if survived else 'CRASHED'}, next level {new[-1] if new else 'not announced'}"))
        if survived:
            text = log.text()[mark:]
            reset = re.findall(r"Minecraft world reset \((\w+)", text)
            results.append(("Minecraft's world is reset for it (world, not all)", bool(reset) and reset[-1] == "world", f"resets: {reset}"))
            sam("cht_bInvisible=0;", "con_iLastLines=5;")

    for name, ok, detail in results:
        print(f"{'PASS' if ok else 'FAIL'}: {name} ({detail})")


if __name__ == "__main__":
    asyncio.run(main())
