"""Oracle for Sam's moving brushes in Minecraft, with both games running (tools/dev.py mc-start, sam tfe <level>).

    uv run -q --with websockets python tools/test_movers.py [--game tfe] [--level NAME] [--walk TICKS] [--mover I]
                                                           [--no-restart]

Sam walks forward WALK ticks (Hatshepsut: to the temple, so its doors are in Minecraft's range), then:
  1. at rest, every mover Minecraft has placed fills a box of cells matching the brush's own box in Sam (±1.5 m);
  2. mover I is triggered (sc_TriggerMover): while it moves in Sam, its cells in Minecraft move the same way;
  3. redstone -> Sam: a redstone lamp lit by a redstone block next to mover LAMP sets it moving in Sam;
  4. Sam -> redstone: a target block next to mover TARGET gives a signal when that mover is triggered in Sam.
Prints PASS/FAIL per check.
"""
import argparse
import asyncio
import json
import re
import math
import time
from pathlib import Path

import websockets

from test_wall import GAMES, SamLog, sam, start_level

LINE = re.compile(r"mover (\d+) \(entity \d+\): at \(([\d.-]+), ([\d.-]+), ([\d.-]+)\) box \(([\d.-]+), ([\d.-]+), ([\d.-]+)\)-\(([\d.-]+), ([\d.-]+), ([\d.-]+)\)")


async def sam_movers(log: SamLog) -> dict[int, dict]:
    out = await log.after("sc_Movers();", "mover ", timeout=3.0)
    movers = {}
    for m in LINE.finditer(out):
        v = [float(x) for x in m.groups()[1:]]
        movers[int(m.group(1))] = {"at": v[0:3], "min": v[3:6], "max": v[6:9]}
    return movers


async def mc_mover(ws, i: int) -> dict:
    await ws.send(json.dumps({"t": "moverinfo", "i": i}))
    end = time.time() + 3.0
    while time.time() < end:
        reply = json.loads(await asyncio.wait_for(ws.recv(), timeout=max(0.05, end - time.time())))
        if reply.get("t") == "moverinfo" and reply.get("i") == i:
            return reply
    return {"cells": -3}


async def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--game", default="tfe", choices=GAMES)
    ap.add_argument("--level", default="01_Hatshepsut")
    ap.add_argument("--walk", type=int, default=280)
    ap.add_argument("--mover", type=int, default=10)
    ap.add_argument("--lamp", type=int, default=11)
    ap.add_argument("--target", type=int, default=8)
    ap.add_argument("--no-restart", action="store_true")
    a = ap.parse_args()
    log = SamLog(a.game)
    results = []

    async with websockets.connect("ws://127.0.0.1:25610", max_size=None, max_queue=None, ping_interval=None) as ws:
        await ws.recv()  # hello
        if not a.no_restart:
            await start_level(log, a.level)
        sam("cht_bGod=1;", "cht_bInvisible=1;", "con_iLastLines=0;")
        # blocks earlier tests left on the way are solid for Sam too
        for block in ("stone_bricks", "gold_block", "grass_block", "oak_planks", "glass", "tnt", "cobblestone", "dirt", "redstone_lamp", "redstone_block", "target"):
            await ws.send(json.dumps({"t": "cmd", "c": f"execute at @p run fill ~-16 ~-8 ~-48 ~16 ~8 ~8 air replace minecraft:{block}"}))
        await asyncio.sleep(2.0)
        if a.walk > 0:
            sam(f"sc_iTestWalk={a.walk};")
            await asyncio.sleep(a.walk / 20.0 + 7.0)  # and a resend of every mover (each 5 s)

        # 1. at rest: Minecraft's cells match Sam's brush boxes
        movers = await sam_movers(log)
        matched, placed, worst = 0, 0, ""
        for i, s in sorted(movers.items()):
            info = await mc_mover(ws, i)
            if info.get("cells", 0) <= 0:
                continue
            placed += 1
            err = max(max(abs(info["min"][k] - s["min"][k]), abs(info["max"][k] - s["max"][k])) for k in range(3))
            if err <= 1.5:
                matched += 1
            else:
                worst += f" mover {i}: Sam {s['min']}-{s['max']}, MC {info['min']}-{info['max']};"
        results.append(("at rest, Minecraft's cells match Sam's brushes", placed > 0 and matched == placed,
                        f"{matched}/{placed} placed movers match ({len(movers)} in all){worst}"))

        # 2. a door moves in both
        i = a.mover
        start_sam = movers.get(i, {}).get("min")
        start_mc = (await mc_mover(ws, i)).get("min")
        sam(f"sc_TriggerMover({i});")
        best = (0.0, None)
        for _ in range(16):
            await asyncio.sleep(0.5)
            s = (await sam_movers(log)).get(i)
            info = await mc_mover(ws, i)
            if not s or not start_sam or not start_mc or info.get("cells", 0) <= 0:
                continue
            ds = [s["min"][k] - start_sam[k] for k in range(3)]
            dm = [info["min"][k] - start_mc[k] for k in range(3)]
            moved = max(abs(v) for v in ds)
            if moved > best[0]:
                best = (moved, (ds, dm))
        if best[1] is None:
            results.append((f"mover {i} moves in Minecraft too", False, "no movement seen"))
        else:
            ds, dm = best[1]
            ok = best[0] >= 1.0 and all(abs(ds[k] - dm[k]) <= 1.5 for k in range(3))
            results.append((f"mover {i} moves in Minecraft too", ok,
                            "Sam moved " + ", ".join(f"{v:.1f}" for v in ds) + "; Minecraft cells moved " + ", ".join(f"{v:.1f}" for v in dm)))
        # Sam -> Minecraft offset (the player is in both)
        await ws.send(json.dumps({"t": "status"}))
        st = None
        end = time.time() + 3
        while time.time() < end and st is None:
            r = json.loads(await asyncio.wait_for(ws.recv(), timeout=3))
            if r.get("t") == "status":
                st = r
        off = [st["player"]["mc"][k] - st["player"]["sam"][k] for k in range(3)]

        def mc(v):
            return tuple(int(math.floor(v[k] + off[k])) for k in range(3))

        # 3. redstone -> Sam: a lamp beside mover LAMP, lit by a redstone block
        await asyncio.sleep(6.0)  # the doors close again
        j = a.lamp
        info = await mc_mover(ws, j)
        before = (await sam_movers(log)).get(j)
        ok, detail = False, "mover not placed in Minecraft"
        if info.get("cells", 0) > 0 and before:
            lx, ly, lz = mc((info["min"][0] - 1.5, info["min"][1] + 1.5, info["max"][2] + 0.5))
            await ws.send(json.dumps({"t": "cmd", "c": f"setblock {lx} {ly} {lz} minecraft:redstone_lamp"}))
            await asyncio.sleep(0.5)
            await ws.send(json.dumps({"t": "cmd", "c": f"setblock {lx - 1} {ly} {lz} minecraft:redstone_block"}))
            moved = 0.0
            for _ in range(8):
                await asyncio.sleep(0.5)
                now = (await sam_movers(log)).get(j)
                if now:
                    moved = max(moved, max(abs(now["at"][k] - before["at"][k]) for k in range(3)))
            mc_log = (Path(__file__).resolve().parents[1] / "mc" / "run" / "logs" / "latest.log").read_text(encoding="utf-8", errors="replace")
            said = f"triggers Sam's mover {j}" in mc_log
            ok, detail = moved >= 1.0 and said, f"Minecraft {'sent' if said else 'did not send'} the trigger, Sam's mover moved {moved:.1f} m"
            await ws.send(json.dumps({"t": "cmd", "c": f"fill {lx - 1} {ly} {lz} {lx} {ly} {lz} minecraft:air"}))
        results.append((f"a lit redstone lamp sets Sam's mover {j} going", ok, detail))

        # 4. Sam -> redstone: a target block beside mover TARGET pulses when it starts moving
        j = a.target
        info = await mc_mover(ws, j)
        ok, detail = False, "mover not placed in Minecraft"
        if info.get("cells", 0) > 0:
            tx, ty, tz = mc((info["max"][0] + 0.5, info["min"][1] + 1.5, info["max"][2] + 0.5))
            await ws.send(json.dumps({"t": "cmd", "c": f"setblock {tx} {ty} {tz} minecraft:target"}))
            await asyncio.sleep(1.0)
            mark = f"semtest-target-{time.time():.0f}"
            sam(f"sc_TriggerMover({j});")
            for _ in range(6):
                await asyncio.sleep(0.25)
                await ws.send(json.dumps({"t": "cmd", "c": f"execute if block {tx} {ty} {tz} minecraft:target[power=15] run say {mark}"}))
            await asyncio.sleep(0.5)
            mc_log = (Path(__file__).resolve().parents[1] / "mc" / "run" / "logs" / "latest.log").read_text(encoding="utf-8", errors="replace")
            ok = f"[Server] {mark}" in mc_log
            detail = "target powered" if ok else "target stayed off"
            await ws.send(json.dumps({"t": "cmd", "c": f"setblock {tx} {ty} {tz} minecraft:air"}))
        results.append((f"Sam's mover {j} starting gives a redstone pulse", ok, detail))

        sam("cht_bInvisible=0;", "con_iLastLines=5;")

    for name, ok, detail in results:
        print(f"{'PASS' if ok else 'FAIL'}: {name} ({detail})")


if __name__ == "__main__":
    asyncio.run(main())
