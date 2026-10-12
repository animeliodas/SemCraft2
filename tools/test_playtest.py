"""Oracle for the playtest fixes (both games running; Sam on a flat level, e.g. tools/dev.py sam tfe +11_AlleyOfSphinxes).

    uv run -q --with websockets python tools/test_playtest.py [--game tfe] [--height 40]

  1. opening the wings keeps the view's pitch (it used to snap level);
  2. a firework in a hand boosts the glide and is used up there (see test_deeper.py: one only in the inventory doesn't);
  3. holding attack in build mode breaks oak planks (it used to hit once only);
  4. Minecraft's frames reach Sam at a good rate.
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

from test_wall import GAMES, SamLog, clean, request, sam

MC_LOG = Path(__file__).resolve().parents[1] / "mc" / "run" / "logs" / "latest.log"


async def where(log: SamLog) -> dict:
    out = await log.after("sc_Where();", "where:", timeout=1.5)
    m = re.findall(r"where: ([\d.-]+) ([\d.-]+) ([\d.-]+) heading ([\d.-]+) pitch ([\d.-]+)(.*)", out)
    if not m:
        return {}
    v = m[-1]
    return {"p": tuple(float(x) for x in v[:3]), "heading": float(v[3]), "pitch": float(v[4]), "flags": v[5]}


async def status_line(log: SamLog, key: str) -> str:
    out = await log.after("sc_Status();", key, timeout=2.0)
    m = re.findall(key + r"[^;\n]*", out)
    return m[-1] if m else ""


async def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--game", default="tfe", choices=GAMES)
    ap.add_argument("--height", type=float, default=40.0)
    a = ap.parse_args()
    log = SamLog(a.game)
    results = []

    async with websockets.connect("ws://127.0.0.1:25610", max_size=None, max_queue=None, ping_interval=None) as ws:
        await ws.recv()
        sam("sc_CloseNetricsa();", "cht_bGod=1;", "cht_bInvisible=1;", "con_iLastLines=0;")
        await clean(ws)
        await ws.send(json.dumps({"t": "cmd", "c": "item replace entity @p armor.chest with minecraft:elytra"}))
        await ws.send(json.dumps({"t": "cmd", "c": "item replace entity @p weapon.offhand with minecraft:firework_rocket 16"}))
        await asyncio.sleep(3.0)

        # 1. the pitch survives opening the wings: look 15 degrees down first
        sam("sc_fTestTurnH=0;", "sc_fTestTurnP=-1.5;", "sc_iTestTurn=10;")
        await asyncio.sleep(1.5)
        w0 = await where(log)
        sam(f'sc_Move("0 {a.height:.1f} 0");')
        await asyncio.sleep(0.4)
        sam("sc_iTestJump=2;")
        await asyncio.sleep(1.2)
        w1 = await where(log)
        gliding = "GLIDING" in await status_line(log, "elytra:")
        ok = gliding and abs(w1.get("pitch", 99) - w0.get("pitch", 0)) < 3.0
        results.append(("opening the wings keeps the view's pitch", ok,
                        f"gliding {gliding}, pitch {w0.get('pitch')} -> {w1.get('pitch')}"))

        # 2. a firework in a hand
        mark = len(MC_LOG.read_text(encoding="utf-8", errors="replace"))
        out = await log.after("sc_Firework();", "firework:", timeout=2.0)
        await asyncio.sleep(1.0)
        text = MC_LOG.read_text(encoding="utf-8", errors="replace")[mark:]
        lit = "firework: lit" in out
        taken = "firework from the off hand: 15 left" in text
        results.append(("a firework in a hand boosts the glide", lit and taken, f"{'lit' if lit else 'not lit'}, {'one used' if taken else 'none used'}"))

        # back on the ground, level view
        for _ in range(40):
            await asyncio.sleep(0.5)
            if "GLIDING" not in await status_line(log, "elytra:"):
                break
        sam("sc_fTestTurnH=0;", "sc_fTestTurnP=1.5;", "sc_iTestTurn=10;")
        await asyncio.sleep(2.0)

        # 3. hold attack on oak planks three blocks ahead at eye height
        client = await request(ws, {"t": "status"}, "client", timeout=3.0)
        eye, (yaw, pitch) = client["eye"], client["rot"]
        cp = math.cos(math.radians(pitch))
        d = (-math.sin(math.radians(yaw)) * cp, -math.sin(math.radians(pitch)), math.cos(math.radians(yaw)) * cp)
        bx, by, bz = (int(math.floor(eye[i] + d[i] * 3.0)) for i in range(3))
        await ws.send(json.dumps({"t": "cmd", "c": f"setblock {bx} {by} {bz} minecraft:oak_planks"}))
        await asyncio.sleep(1.0)
        sam("sc_Build();")
        await asyncio.sleep(0.5)
        await ws.send(json.dumps({"t": "slot", "n": 7}))  # whatever is there, not a block to place
        await ws.send(json.dumps({"t": "key", "k": "attack", "down": True}))
        await asyncio.sleep(5.0)
        await ws.send(json.dumps({"t": "key", "k": "attack", "down": False}))
        await asyncio.sleep(0.5)
        sam("sc_Build();")
        markb = f"semtest-planks-{time.time():.0f}"
        mark = len(MC_LOG.read_text(encoding="utf-8", errors="replace"))
        await ws.send(json.dumps({"t": "cmd", "c": f"execute if block {bx} {by} {bz} minecraft:oak_planks run say {markb}"}))
        await asyncio.sleep(1.0)
        text = MC_LOG.read_text(encoding="utf-8", errors="replace")[mark:]
        broken = f"[Server] {markb}" not in text
        results.append(("holding attack breaks oak planks", broken, "broken" if broken else "still there"))
        await ws.send(json.dumps({"t": "cmd", "c": f"setblock {bx} {by} {bz} minecraft:air"}))

        # 4. frame rate
        s0 = await status_line(log, "frames:")
        t0 = time.time()
        await asyncio.sleep(5.0)
        s1 = await status_line(log, "frames:")
        n0 = re.findall(r"uploaded (\d+)", s0)
        n1 = re.findall(r"uploaded (\d+)", s1)
        fps = (int(n1[0]) - int(n0[0])) / (time.time() - t0) if n0 and n1 else 0
        results.append(("Minecraft's frames reach Sam often", fps > 90, f"{fps:.0f} frames/s"))
        sam("cht_bInvisible=0;", "con_iLastLines=5;")

    for name, ok, detail in results:
        print(f"{'PASS' if ok else 'FAIL'}: {name} ({detail})")


if __name__ == "__main__":
    asyncio.run(main())
