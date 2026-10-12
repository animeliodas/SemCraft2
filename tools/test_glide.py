"""Oracle for Minecraft's elytra on Sam's player (both games running, tools/dev.py mc-start, sam tfe).

    uv run -q --with websockets python tools/test_glide.py [--game tfe] [--level NAME] [--height M] [--no-restart]

Each run: Sam is lifted HEIGHT m (sc_Move), and while he falls, jump is pressed (sc_iTestJump).
  1. with an elytra: the wings open and he glides: he lands far ahead (flown > 2 x the drop);
  2. without one (control): he falls straight down;
  3. with an elytra and a firework (sc_Boost) right after the wings open: he flies further than without.
Prints PASS/FAIL per check.
"""
import argparse
import asyncio
import json
import math
import re
import time

import websockets

from test_wall import GAMES, SamLog, clean, sam, start_level


async def where(log: SamLog) -> tuple[float, float, float]:
    out = await log.after("sc_Where();", "where:", timeout=1.5)
    m = re.findall(r"where: ([\d.-]+) ([\d.-]+) ([\d.-]+)", out)
    return tuple(float(v) for v in m[-1]) if m else (math.nan, math.nan, math.nan)


async def flight(ws, log: SamLog, height: float, elytra: bool, boost: bool) -> tuple[float, float, str]:
    """Lift Sam, press jump while he falls, wait until he lands; returns (horizontal distance, drop, glide log)."""
    item = "minecraft:elytra" if elytra else "minecraft:air"
    await ws.send(json.dumps({"t": "cmd", "c": f"item replace entity @p armor.chest with {item}"}))
    await asyncio.sleep(1.5)  # the gear message reaches Sam
    p0 = await where(log)
    start = len(log.text())
    sam(f'sc_Move("0 {height:.1f} 0");')
    await asyncio.sleep(0.4)
    sam("sc_iTestJump=2;")
    if boost:
        await asyncio.sleep(0.4)
        sam("sc_Boost();")
    # until he stands still again
    last, still = None, 0
    end = time.time() + 20
    while time.time() < end and still < 3:
        await asyncio.sleep(0.5)
        p = await where(log)
        if last is not None and math.dist(p, last) < 0.05:
            still += 1
        else:
            still = 0
        last = p
    p1 = last or p0
    text = log.text()[start:]
    folded = re.findall(r"elytra folded: ([\d.]+) m flown, ([\d.-]+) m down", text)
    note = f"glide log: {folded[-1][0]} m flown, {folded[-1][1]} m down" if folded else ("wings opened" if "elytra open" in text else "no glide")
    return math.dist((p0[0], p0[2]), (p1[0], p1[2])), p0[1] + height - p1[1], note


async def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--game", default="tfe", choices=GAMES)
    ap.add_argument("--level", default="01_Hatshepsut")
    ap.add_argument("--height", type=float, default=40.0)
    ap.add_argument("--no-restart", action="store_true")
    ap.add_argument("--watch-intro", action="store_true", help="let the level's intro play instead of clicking it away")
    a = ap.parse_args()
    log = SamLog(a.game)
    results = []

    async with websockets.connect("ws://127.0.0.1:25610", max_size=None, max_queue=None, ping_interval=None) as ws:
        await ws.recv()

        home = None

        async def fresh():
            # --no-restart: back to where the first flight started instead of loading the level again
            if a.no_restart and home is not None:
                p = await where(log)
                sam(f'sc_Move("{home[0] - p[0]:.3f} {home[1] - p[1] + 0.5:.3f} {home[2] - p[2]:.3f}");')
                await asyncio.sleep(2.5)
                return
            await start_level(log, a.level, skip_intro=not a.watch_intro)
            sam("cht_bGod=1;", "cht_bInvisible=1;", "con_iLastLines=0;")
            await clean(ws)

        if not a.no_restart:
            await fresh()
        else:
            sam("sc_CloseNetricsa();", "cht_bGod=1;", "cht_bInvisible=1;", "con_iLastLines=0;")
            await asyncio.sleep(1.0)
        home = await where(log)
        far, drop, note = await flight(ws, log, a.height, elytra=True, boost=False)
        results.append(("with an elytra Sam glides", far > 2 * max(drop, 1.0) and far > 15, f"{far:.1f} m ahead for {drop:.1f} m down; {note}"))

        await fresh()
        far0, drop0, note0 = await flight(ws, log, a.height, elytra=False, boost=False)
        results.append(("without one he falls straight", far0 < 2.0, f"{far0:.1f} m ahead for {drop0:.1f} m down; {note0}"))

        await fresh()
        far2, drop2, note2 = await flight(ws, log, a.height, elytra=True, boost=True)
        results.append(("a firework takes him further", far2 > far * 1.3, f"{far2:.1f} m ahead (without: {far:.1f}); {note2}"))
        sam("cht_bInvisible=0;", "con_iLastLines=5;")
        await ws.send(json.dumps({"t": "cmd", "c": "item replace entity @p armor.chest with minecraft:elytra"}))

    for name, ok, detail in results:
        print(f"{'PASS' if ok else 'FAIL'}: {name} ({detail})")


if __name__ == "__main__":
    asyncio.run(main())
