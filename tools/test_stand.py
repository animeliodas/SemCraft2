"""Oracle: Sam stands and walks on Minecraft blocks as on a floor (both games running, tools/dev.py mc-start, sam tfe).

    uv run -q --with websockets python tools/test_stand.py [--game tfe] [--level NAME] [--modes 1,0] [--ticks 14]

For each block mode (sc_iBlockBrushes: 1 brushes, 0 models):
  1. a 1x2x1 stone pillar; Sam is dropped on it 0.45 m off its centre and must stay on top;
  2. a stone platform 3 wide, 14 long, one block up; Sam runs along it for TICKS player actions, sampled every 0.15 s:
     how far his feet ever were from the platform's top, how far he strayed sideways, how far he got.
Removing the pillar and the platform afterwards also exercises rebuilding a block brush Sam stood on.
Brushes have flat tops; models collide as spheres, so the seams between blocks are dips.
"""
import argparse
import asyncio
import json
import math
import re
import time

import websockets

from test_wall import GAMES, SamLog, clean, sam, start_level


async def where(log: SamLog, heading: bool = False) -> tuple:
    out = await log.after("sc_Where();", "where:", timeout=1.0)
    m = re.findall(r"where: ([\d.-]+) ([\d.-]+) ([\d.-]+) heading ([\d.-]+)", out)
    v = tuple(float(x) for x in m[-1]) if m else (math.nan,) * 4
    return v if heading else v[:3]


async def status(ws) -> dict:
    await ws.send(json.dumps({"t": "status"}))
    end = time.time() + 3
    while time.time() < end:
        r = json.loads(await asyncio.wait_for(ws.recv(), timeout=3))
        if r.get("t") == "status":
            return r
    raise TimeoutError("status")


async def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--game", default="tfe", choices=GAMES)
    ap.add_argument("--level", default="01_Hatshepsut")
    ap.add_argument("--modes", default="1,0")
    ap.add_argument("--ticks", type=int, default=14)
    a = ap.parse_args()
    log = SamLog(a.game)
    results = []

    async with websockets.connect("ws://127.0.0.1:25610", max_size=None, max_queue=None, ping_interval=None) as ws:
        await ws.recv()
        for mode in (int(v) for v in a.modes.split(",")):
            name = "brushes" if mode else "models"
            sam(f"sc_iBlockBrushes={mode};")
            await start_level(log, a.level)
            sam("cht_bGod=1;", "cht_bInvisible=1;", "con_iLastLines=0;")
            await clean(ws)
            st = await status(ws)
            off = [st["player"]["mc"][k] - st["player"]["sam"][k] for k in range(3)]
            x, y, z = await where(log)
            if math.isnan(x):
                results.append((f"mode {name}", False, "Sam's position unknown"))
                continue
            fy = math.floor(y)

            def mc(cx: int, cy: int, cz: int) -> tuple[int, int, int]:
                return int(round(cx + off[0])), int(round(cy + off[1])), int(round(cz + off[2]))

            # 1. the pillar: the column whose centre is 0.45 m behind Sam on x, two blocks tall from his feet
            cx, cz = math.floor(x - 0.45), math.floor(z)
            p0, p1 = mc(cx, fy, cz), mc(cx, fy + 1, cz)
            await ws.send(json.dumps({"t": "cmd", "c": "fill %d %d %d %d %d %d minecraft:stone" % (*p0, *p1)}))
            await asyncio.sleep(3.0)
            sam(f'sc_Move("{cx + 0.95 - x:.3f} {fy + 2.2 - y:.3f} {cz + 0.5 - z:.3f}");')
            await asyncio.sleep(2.5)
            p = await where(log)
            ok = abs(p[1] - (fy + 2)) < 0.3
            results.append((f"mode {name}: Sam stays on a pillar 0.45 m off its centre", ok, f"feet at {p[1]:.2f}, top {fy + 2}"))
            await ws.send(json.dumps({"t": "cmd", "c": "fill %d %d %d %d %d %d minecraft:air" % (*p0, *p1)}))
            await asyncio.sleep(2.5)

            # 2. the platform: 3 cells wide around Sam, 14 long ahead of him (along the axis he looks down)
            x, y, z, h = await where(log, heading=True)
            fx, fz = -math.sin(math.radians(h)), -math.cos(math.radians(h))
            cx, cz = math.floor(x), math.floor(z)
            if abs(fx) > abs(fz):
                sx = 1 if fx > 0 else -1
                a0, a1 = mc(cx, fy, cz - 1), mc(cx + 13 * sx, fy, cz + 1)
            else:
                sz = 1 if fz > 0 else -1
                a0, a1 = mc(cx - 1, fy, cz), mc(cx + 1, fy, cz + 13 * sz)
            m0 = tuple(min(a0[k], a1[k]) for k in range(3))
            m1 = tuple(max(a0[k], a1[k]) for k in range(3))
            await ws.send(json.dumps({"t": "cmd", "c": "fill %d %d %d %d %d %d minecraft:stone" % (*m0, *m1)}))
            await asyncio.sleep(3.0)
            top = fy + 1
            sam(f'sc_Move("{cx + 0.5 - x:.3f} {top + 0.2 - y:.3f} {cz + 0.5 - z:.3f}");')
            await asyncio.sleep(1.5)
            q0 = await where(log)
            sam(f"sc_iTestWalk={a.ticks};")
            worst_y, worst_x = 0.0, 0.0
            t_end = time.time() + a.ticks / 20.0 + 1.0
            while time.time() < t_end:
                q = await where(log)
                if not math.isnan(q[1]):
                    worst_y = max(worst_y, abs(q[1] - top))
                    side = (q[0] - q0[0]) * fz - (q[2] - q0[2]) * fx  # across the run
                    worst_x = max(worst_x, abs(side))
                await asyncio.sleep(0.15)
            q1 = await where(log)
            run = math.dist((q1[0], q1[2]), (q0[0], q0[2]))
            ok = worst_y < 0.1 and worst_x < 0.1 and run > 3.0
            results.append((f"mode {name}: Sam runs along a block platform like a floor", ok,
                            f"feet off the top by up to {worst_y:.2f} m, strayed {worst_x:.2f} m, ran {run:.2f} m"))
            await ws.send(json.dumps({"t": "cmd", "c": "fill %d %d %d %d %d %d minecraft:air" % (*m0, *m1)}))
            await asyncio.sleep(2.5)
            alive = await where(log)
            results.append((f"mode {name}: blocks removed under Sam, the game goes on", not math.isnan(alive[0]), ""))
        sam("sc_iBlockBrushes=1;", "cht_bInvisible=0;", "con_iLastLines=5;")

    for name, ok, detail in results:
        print(f"{'PASS' if ok else 'FAIL'}: {name} ({detail})")


if __name__ == "__main__":
    asyncio.run(main())
