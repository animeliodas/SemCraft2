"""Oracle: Minecraft's blocks are solid for Serious Sam's player (both games running, tools/dev.py mc-start, sam tfe).

    uv run -q --with websockets python tools/test_blocks.py [--level 01_Hatshepsut] [--ticks 20]

1. restart the level, run forward for TICKS player actions (sc_iTestWalk), measure how far Sam got;
2. restart, put a stone brick wall 3 blocks ahead in Minecraft (fill), wait until Sam has the block entities, run again.
PASS if the wall stopped Sam (and the free run went much further).
"""
import argparse
import asyncio
import json
import math
import re
import tempfile
import time
from pathlib import Path

import websockets

CMD = Path(tempfile.gettempdir()) / "SemCraft2" / "cmd.txt"
ROOT = Path(__file__).resolve().parents[2]
SAM_LOG = ROOT / "Serious Sam Classic The First Encounter" / "SeriousSam_Custom.log"


def sam(command: str) -> None:
    CMD.parent.mkdir(exist_ok=True)
    with open(CMD, "a", encoding="ascii") as f:
        f.write(command + "\n")


def last_where() -> tuple[float, float, float] | None:
    text = SAM_LOG.read_text(encoding="cp1252", errors="replace")
    m = re.findall(r"where: (-?[\d.]+) (-?[\d.]+) (-?[\d.]+)", text)
    return tuple(float(v) for v in m[-1]) if m else None


async def where() -> tuple[float, float, float]:
    before = last_where()
    sam("sc_Where();")
    for _ in range(20):
        await asyncio.sleep(0.25)
        now = last_where()
        if now is not None and now != before:
            return now
    return last_where()


async def request(ws, message: dict, want: str) -> dict:
    await ws.send(json.dumps(message))
    end = time.time() + 3
    while time.time() < end:
        reply = json.loads(await asyncio.wait_for(ws.recv(), timeout=max(0.05, end - time.time())))
        if reply.get("t") == want:
            return reply
    raise TimeoutError(want)


async def run(ticks: int) -> float:
    p0 = await where()
    sam(f"sc_iTestWalk={ticks};")
    await asyncio.sleep(ticks / 20.0 + 1.5)
    p1 = await where()
    d = math.dist((p0[0], p0[2]), (p1[0], p1[2]))
    print(f"  from {p0} to {p1}: {d:.2f} m")
    return d


async def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--level", default="01_Hatshepsut")
    ap.add_argument("--ticks", type=int, default=20)
    a = ap.parse_args()

    async with websockets.connect("ws://127.0.0.1:25610") as ws:
        await ws.recv()
        await ws.send(json.dumps({"t": "cmd", "c": "kill @e[type=minecraft:zombie]"}))

        print("free run:")
        sam(f'StartMap("{a.level}");')
        await asyncio.sleep(30)
        sam("cht_bGod=1;")
        client = await request(ws, {"t": "status"}, "client")
        eye, (yaw, _) = client["eye"], client["rot"]
        # clear anything built ahead earlier
        ex, ey, ez = (math.floor(v) for v in eye)
        await ws.send(json.dumps({"t": "cmd", "c": f"fill {ex - 6} {ey - 3} {ez - 10} {ex + 6} {ey + 3} {ez + 10} air replace minecraft:stone_bricks"}))
        await asyncio.sleep(1)
        free = await run(a.ticks)

        print("run into a Minecraft wall:")
        sam(f'StartMap("{a.level}");')
        await asyncio.sleep(30)
        sam("cht_bGod=1;")
        client = await request(ws, {"t": "status"}, "client")
        eye, (yaw, _) = client["eye"], client["rot"]
        fx, fz = -math.sin(math.radians(yaw)), math.cos(math.radians(yaw))
        cx, cz = eye[0] + fx * 3.0, eye[2] + fz * 3.0
        feet = math.floor(eye[1] - 1.62)
        # a wall 5 wide (across the view), 3 high, 1 thick, 3 blocks ahead
        sx, sz = round(fz), round(-fx)
        x0, z0 = math.floor(cx - sx * 2), math.floor(cz - sz * 2)
        x1, z1 = math.floor(cx + sx * 2), math.floor(cz + sz * 2)
        await ws.send(json.dumps({"t": "cmd", "c": f"fill {min(x0, x1)} {feet} {min(z0, z1)} {max(x0, x1)} {feet + 2} {max(z0, z1)} minecraft:stone_bricks"}))
        await asyncio.sleep(3)
        sam("sc_Status();")
        await asyncio.sleep(1)
        text = SAM_LOG.read_text(encoding="cp1252", errors="replace")
        print("  Sam:", re.findall(r"blocks: [^;]*", text)[-1])
        walled = await run(a.ticks)

        ok = walled < 3.5 and free > walled + 2.0
        print(("PASS" if ok else "FAIL") + f": free {free:.2f} m, against the wall {walled:.2f} m")


asyncio.run(main())
