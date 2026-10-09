"""Oracle for Sam's weapons hurting Minecraft mobs, with both games running (tools/dev.py mc-start, sam tfe <level>).

    uv run -q --with websockets python tools/test_shoot.py [--weapon KEY] [--zombies N] [--dist BLOCKS] [--ticks T]
                                                          [--bursts B] [--level NAME] [--no-restart]

Restarts the level (a living player), turns on Sam's god mode and gives all weapons, selects weapon KEY (Sam's number
key: TFE 2 colt, 3 shotguns, 4 tommygun/minigun, 5 rocket launcher, 6 grenade launcher, 7 laser, 8 cannon), puts N
still (NoAI) zombies DIST blocks ahead along the view, makes Sam hold fire for T player actions B times, and checks that
the zombies lost health. Prints PASS/FAIL.
"""
import argparse
import asyncio
import json
import math
import tempfile
import time
from pathlib import Path

import websockets

CMD = Path(tempfile.gettempdir()) / "SemCraft2" / "cmd.txt"
MC_LOG = Path(__file__).resolve().parents[1] / "mc" / "run" / "logs" / "latest.log"


async def request(ws, message: dict, want: str, timeout: float = 2.0) -> dict:
    await ws.send(json.dumps(message))
    end = time.time() + timeout
    while time.time() < end:
        reply = json.loads(await asyncio.wait_for(ws.recv(), timeout=max(0.05, end - time.time())))
        if reply.get("t") == want:
            return reply
    raise TimeoutError(want)


def sam(command: str) -> None:
    CMD.parent.mkdir(exist_ok=True)
    with open(CMD, "a", encoding="ascii") as f:
        f.write(command + "\n")


async def total_health(ws, n: int) -> float:
    """Sum of the test zombies' health (dead ones count 0)."""
    marker = f"semtest-{time.time():.3f}"
    await ws.send(json.dumps({"t": "cmd", "c": f"say {marker}"}))
    for i in range(n):
        await ws.send(json.dumps({"t": "cmd", "c": f"data get entity @e[tag=semtest{i},limit=1] Health"}))
    await asyncio.sleep(0.8)
    text = MC_LOG.read_text(encoding="utf-8", errors="replace")
    tail = text[text.find(marker):]
    values = [float(l.rsplit(":", 1)[1].strip().rstrip("f")) for l in tail.splitlines() if "has the following entity data" in l]
    return sum(values)


async def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--weapon", type=int, default=2)
    ap.add_argument("--zombies", type=int, default=1)
    ap.add_argument("--dist", type=float, default=6.0)
    ap.add_argument("--ticks", type=int, default=30)
    ap.add_argument("--bursts", type=int, default=3)
    ap.add_argument("--level", default="01_Hatshepsut")
    ap.add_argument("--no-restart", action="store_true")
    a = ap.parse_args()

    async with websockets.connect("ws://127.0.0.1:25610") as ws:
        await ws.recv()  # hello
        await ws.send(json.dumps({"t": "cmd", "c": "kill @e[type=minecraft:zombie]"}))
        if not a.no_restart:
            sam(f'StartMap("{a.level}");')
            await asyncio.sleep(30.0)
        sam("cht_bGod=1;")
        sam("cht_bGiveAll=1;")
        await asyncio.sleep(1.0)
        sam(f"sc_iTestSelect={a.weapon};")
        await asyncio.sleep(2.0)
        await ws.send(json.dumps({"t": "cmd", "c": "kill @e[type=minecraft:zombie]"}))
        client = await request(ws, {"t": "status"}, "client")
        eye = client["eye"]
        yaw, _ = client["rot"]
        ex, ey, ez = (int(v) for v in eye)
        # what earlier tests built near the player
        for block in ("stone_bricks", "gold_block", "grass_block", "oak_planks", "glass"):
            await ws.send(json.dumps({"t": "cmd", "c": f"fill {ex - 12} {ey - 8} {ez - 12} {ex + 12} {ey + 8} {ez + 12} air replace minecraft:{block}"}))
        fx, fz = -math.sin(math.radians(yaw)), math.cos(math.radians(yaw))
        sx, sz = fz, -fx  # sideways
        for i in range(a.zombies):
            off = (i - (a.zombies - 1) / 2.0) * 0.9
            x, z = eye[0] + fx * a.dist + sx * off, eye[2] + fz * a.dist + sz * off
            await ws.send(json.dumps({"t": "cmd", "c": f"summon minecraft:zombie {x:.2f} {eye[1] - 1.4:.2f} {z:.2f} {{NoAI:1b,Tags:[\"semtest{i}\"],PersistenceRequired:1b}}"}))
        await asyncio.sleep(1.5)
        before = await total_health(ws, a.zombies)
        print(f"zombies' health before: {before:.1f}")
        for _ in range(a.bursts):
            sam(f"sc_iTestFire={a.ticks};")
            await asyncio.sleep(a.ticks / 20.0 + 2.5)
        after = await total_health(ws, a.zombies)
        print(f"zombies' health after: {after:.1f}")
        print("PASS: Sam's weapon hurt the zombies" if after < before else "FAIL: the zombies weren't hurt")
        sam("cht_bGod=0;")


asyncio.run(main())
