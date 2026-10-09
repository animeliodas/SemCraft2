"""Oracle for TNT against Sam, with both games running (tools/dev.py mc-start, sam tfe <level>).

    uv run -q --with websockets python tools/test_tnt.py [--game tfe] [--dist BLOCKS] [--level NAME] [--no-restart]

Minecraft puts a TNT block DIST blocks ahead on Sam's crosshair; Sam (god mode, colt) shoots it. Checks:
  1. the shot lights the TNT (the block turns into primed TNT);
  2. the explosion throws Sam's player (Minecraft logs the push, Sam's player moves more than a metre).
Prints PASS/FAIL per check.
"""
import argparse
import asyncio
import json
import math
import re
import time
from pathlib import Path

import websockets

from test_wall import GAMES, SamLog, rays_sent, request, sam

MC_LOG = Path(__file__).resolve().parents[1] / "mc" / "run" / "logs" / "latest.log"


async def where(log: SamLog) -> tuple[float, float, float] | None:
    out = await log.after("sc_Where();", "where:")
    m = re.search(r"where: ([\d.-]+) ([\d.-]+) ([\d.-]+)", out)
    return (float(m.group(1)), float(m.group(2)), float(m.group(3))) if m else None


async def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--game", default="tfe", choices=GAMES)
    ap.add_argument("--dist", type=float, default=4.0)
    ap.add_argument("--level", default="01_Hatshepsut")
    ap.add_argument("--no-restart", action="store_true")
    a = ap.parse_args()
    log = SamLog(a.game)
    results = []

    async with websockets.connect("ws://127.0.0.1:25610", max_size=None) as ws:
        await ws.recv()  # hello
        await ws.send(json.dumps({"t": "cmd", "c": "kill @e[type=!minecraft:player]"}))
        if not a.no_restart:
            sam(f'StartMap("{a.level}");')
            await asyncio.sleep(30.0)
        sam("cht_bGod=1;", "cht_bInvisible=1;", "cht_bGiveAll=1;", "con_iLastLines=0;")
        await asyncio.sleep(1.0)
        sam("sc_iTestSelect=2;")  # colt
        await asyncio.sleep(2.0)

        client = await request(ws, {"t": "status"}, "client")
        eye, (yaw, pitch) = client["eye"], client["rot"]
        ex, ey, ez = (int(math.floor(v)) for v in eye)
        for block in ("stone_bricks", "gold_block", "grass_block", "oak_planks", "glass", "tnt", "fire", "cobblestone", "dirt"):
            await ws.send(json.dumps({"t": "cmd", "c": f"fill {ex - 16} {ey - 8} {ez - 16} {ex + 16} {ey + 8} {ez + 16} air replace minecraft:{block}"}))
        await asyncio.sleep(1.0)

        # the TNT on the crosshair
        cp = math.cos(math.radians(pitch))
        d = (-math.sin(math.radians(yaw)) * cp, -math.sin(math.radians(pitch)), math.cos(math.radians(yaw)) * cp)
        bx, by, bz = (int(math.floor(eye[i] + d[i] * a.dist)) for i in range(3))
        await ws.send(json.dumps({"t": "cmd", "c": f"setblock {bx} {by} {bz} minecraft:tnt"}))
        await asyncio.sleep(2.0)

        start = await where(log)
        mc_mark = len(MC_LOG.read_text(encoding="utf-8", errors="replace"))
        rays = await rays_sent(log)
        sam("sc_iTestFire=2;")
        await asyncio.sleep(1.0)
        print(f"shots: {await rays_sent(log) - rays}")
        await ws.send(json.dumps({"t": "cmd", "c": f"execute if block {bx} {by} {bz} minecraft:tnt run say semtest-tnt-still-there"}))
        await ws.send(json.dumps({"t": "cmd", "c": f"execute if entity @e[type=minecraft:tnt,x={bx},y={by},z={bz},distance=..3] run say semtest-tnt-primed"}))
        await asyncio.sleep(0.8)
        mc = MC_LOG.read_text(encoding="utf-8", errors="replace")[mc_mark:]
        results.append(("the shot lit the TNT", "[Server] semtest-tnt-primed" in mc and "[Server] semtest-tnt-still-there" not in mc,
                         "primed" if "[Server] semtest-tnt-primed" in mc else "not primed"))

        # the fuse is 4 s; then Sam flies
        moved = 0.0
        t_end = time.time() + 6.0
        while time.time() < t_end:
            await asyncio.sleep(0.1)
            p = await where(log)
            if p and start:
                moved = max(moved, math.dist(p, start))
        mc = MC_LOG.read_text(encoding="utf-8", errors="replace")[mc_mark:]
        pushes = re.findall(r"explosion pushes Sam: ([\d. -]+) m/s", mc)
        results.append(("the explosion threw Sam", bool(pushes) and moved > 1.0,
                        f"push {pushes[0] if pushes else 'none'}, Sam moved up to {moved:.2f} m"))
        sam("cht_bInvisible=0;", "con_iLastLines=5;")

    for name, ok, detail in results:
        print(f"{'PASS' if ok else 'FAIL'}: {name} ({detail})")


if __name__ == "__main__":
    asyncio.run(main())
