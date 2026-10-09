"""Oracle for Minecraft blocks against Sam's guns, with both games running (tools/dev.py mc-start, sam tfe <level>).

    uv run -q --with websockets python tools/test_wall.py [--game tfe] [--dist BLOCKS] [--level NAME] [--down DEG]
                                                         [--no-restart]
    TSE: --game tse --level LevelsMP/1_1_Palenque --down 6

Sam (god mode, invisible to monsters, colts) spawns a Kleer where the crosshair meets the level, then Minecraft puts
a stone brick wall DIST blocks ahead, across the view. Checks, in order:
  1. a short burst only cracks the wall: the Kleer behind it keeps its health;
  2. long bursts break wall blocks, and they're gone in Sam too;
  3. control: with the wall removed, the same short burst hurts the Kleer.
Prints PASS/FAIL per check.
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

ROOT = Path(__file__).resolve().parents[2]
CMD = Path(tempfile.gettempdir()) / "SemCraft2" / "cmd.txt"
GAMES = {"tfe": "Serious Sam Classic The First Encounter", "tse": "Serious Sam Classic The Second Encounter"}


def sam(*commands: str) -> None:
    CMD.parent.mkdir(exist_ok=True)
    with open(CMD, "a", encoding="ascii") as f:
        for c in commands:
            f.write(c + "\n")


class SamLog:
    def __init__(self, game: str):
        self.path = ROOT / GAMES[game] / "SeriousSam_Custom.log"

    def text(self) -> str:
        return self.path.read_text(encoding="latin-1", errors="replace")

    async def after(self, command: str, want: str, timeout: float = 3.0) -> str:
        """Run a console command and return the log written after it, once it contains `want`."""
        start = len(self.text())
        sam(command)
        end = time.time() + timeout
        while time.time() < end:
            await asyncio.sleep(0.2)
            new = self.text()[start:]
            if want in new:
                await asyncio.sleep(0.2)
                return self.text()[start:]
        return self.text()[start:]


async def request(ws, message: dict, want: str, timeout: float = 2.0) -> dict:
    await ws.send(json.dumps(message))
    end = time.time() + timeout
    while time.time() < end:
        reply = json.loads(await asyncio.wait_for(ws.recv(), timeout=max(0.05, end - time.time())))
        if reply.get("t") == want:
            return reply
    raise TimeoutError(want)


async def kleer_health(log: SamLog) -> float | None:
    out = await log.after("sc_Monsters();", "monsters:")
    m = re.findall(r"monster Boneman \(\d+\) health ([\d.-]+)", out)
    return float(m[0]) if m else None


async def rays_sent(log: SamLog) -> int:
    out = await log.after("sc_Status();", "weapons:")
    m = re.search(r"weapons: (\d+) rays", out)
    return int(m.group(1)) if m else -1


async def solid_blocks(log: SamLog) -> int:
    out = await log.after("sc_Status();", "blocks:")
    m = re.search(r"blocks: (\d+) solid", out)
    return int(m.group(1)) if m else -1


async def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--game", default="tfe", choices=GAMES)
    ap.add_argument("--dist", type=int, default=5)
    ap.add_argument("--level", default="01_Hatshepsut")
    ap.add_argument("--down", type=float, default=0.0, help="look down this many degrees first (a closer Kleer)")
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
        sam("sc_iTestSelect=2;")  # colts: every pull of the trigger is a shot (the minigun has to spin up)
        await asyncio.sleep(2.0)

        client = await request(ws, {"t": "status"}, "client")
        eye = client["eye"]
        yaw = client["rot"][0]
        ex, ey, ez = (int(math.floor(v)) for v in eye)
        for block in ("stone_bricks", "gold_block", "grass_block", "oak_planks", "glass", "tnt", "fire", "cobblestone", "dirt"):
            await ws.send(json.dumps({"t": "cmd", "c": f"fill {ex - 16} {ey - 8} {ez - 16} {ex + 16} {ey + 8} {ez + 16} air replace minecraft:{block}"}))
        await asyncio.sleep(1.5)

        # Hatshepsut's start is at x = 0, a block seam: turn a little so the wall is hit inside one block
        sam("sc_fTestTurnH=0.15;", f"sc_fTestTurnP={-a.down / 10:.3f};", "sc_iTestTurn=10;")
        await asyncio.sleep(1.5)

        # the Kleer, where the crosshair meets Sam's level (before the wall is there)
        out = await log.after('sc_SpawnSam("Boneman");', "spawned")
        print(out.strip().splitlines()[-1] if out.strip() else "no spawn output")
        await asyncio.sleep(1.0)
        before = await kleer_health(log)
        print(f"Kleer health: {before}")
        # the crosshair is on the Kleer's feet: raise it to its chest
        out = await log.after("sc_Monsters();", "monsters:")
        m = re.findall(r"monster Boneman \(\d+\) health [\d.-]+ at ([\d.]+) m", out)
        if m:
            up = math.degrees(math.atan2(1.6, float(m[0])))
            sam(f"sc_fTestTurnH=0;", f"sc_fTestTurnP={up / 10:.4f};", "sc_iTestTurn=10;")
            await asyncio.sleep(1.5)
        # then nudge it until Sam's own ray meets the Kleer (slopes, a Kleer that settled lower)
        cur = 0.0
        for want in (0.0, 0.6, 1.2, 1.8, 2.4, 3.0, -0.6, -1.2, -1.8, -2.4):
            if want != cur:
                sam("sc_fTestTurnH=0;", f"sc_fTestTurnP={(want - cur) / 10:.4f};", "sc_iTestTurn=10;")
                await asyncio.sleep(1.0)
                cur = want
            out = await log.after("sc_Probe();", "probe:")
            if "probe: Boneman" in out:
                break
        print("without a wall:", out.strip().splitlines()[-1] if out.strip() else "no probe output")

        client = await request(ws, {"t": "status"}, "client")
        eye = client["eye"]
        yaw = client["rot"][0]

        # the wall: one block thick across the view, DIST blocks ahead, 9 wide, 6 high around the eye
        fx, fz = -math.sin(math.radians(yaw)), math.cos(math.radians(yaw))
        cx, cz = int(math.floor(eye[0] + fx * a.dist)), int(math.floor(eye[2] + fz * a.dist))
        if abs(fx) > abs(fz):
            box = (cx, ey - 3, cz - 4, cx, ey + 2, cz + 4)
        else:
            box = (cx - 4, ey - 3, cz, cx + 4, ey + 2, cz)
        await ws.send(json.dumps({"t": "cmd", "c": "fill %d %d %d %d %d %d minecraft:stone_bricks replace air" % box}))
        await asyncio.sleep(3.0)
        walls = await solid_blocks(log)
        print(f"solid blocks in Sam after the wall: {walls}")

        # 1. Sam's own ray along the crosshair (a ray between four blocks can slip through: SE1 models collide as
        # spheres, so this is informational; the bursts below decide)
        out = await log.after("sc_Probe();", "probe:")
        print(out.strip().splitlines()[-1] if out.strip() else "no probe output")

        # 2. a short burst only cracks the wall: the Kleer behind it keeps its health
        rays = await rays_sent(log)
        sam("sc_iTestFire=4;")
        await asyncio.sleep(2.5)
        rays = await rays_sent(log) - rays
        mid = await kleer_health(log)
        walls_mid = await solid_blocks(log)
        ok = rays > 0 and before is not None and mid is not None and abs(mid - before) < 0.01
        results.append(("short burst: Kleer behind the wall unhurt", ok, f"{rays} shots, health {before} -> {mid}, blocks {walls} -> {walls_mid}"))

        # 3. long bursts break blocks, and they're gone in Sam too
        rays = await rays_sent(log)
        for _ in range(4):
            sam("sc_iTestFire=40;")
            await asyncio.sleep(4.0)
        rays = await rays_sent(log) - rays
        walls_after = await solid_blocks(log)
        results.append(("long bursts: wall blocks broken, gone in Sam too", walls_after < walls_mid, f"{rays} shots, blocks {walls_mid} -> {walls_after}"))

        # 4. control: without the wall the same short burst hurts the Kleer (unless the shots through the hole the
        # long bursts made already killed it, which says the same)
        before2 = await kleer_health(log)
        if before2 is None and before is not None:
            results.append(("control: Kleer killed through the hole the long bursts made", True, f"health {before} -> dead"))
            sam("cht_bInvisible=0;", "con_iLastLines=5;")
            for name, ok, detail in results:
                print(f"{'PASS' if ok else 'FAIL'}: {name} ({detail})")
            return
        await ws.send(json.dumps({"t": "cmd", "c": "fill %d %d %d %d %d %d minecraft:air replace minecraft:stone_bricks" % box}))
        await asyncio.sleep(3.0)
        rays = await rays_sent(log)
        sam("sc_iTestFire=4;")
        await asyncio.sleep(2.5)
        rays = await rays_sent(log) - rays
        after = await kleer_health(log)
        ok = before2 is not None and (after is None or after < before2)
        results.append(("control: no wall, same burst hurts the Kleer", ok and rays > 0, f"{rays} shots, health {before2} -> {after}"))

        sam("cht_bInvisible=0;", "con_iLastLines=5;")

    for name, ok, detail in results:
        print(f"{'PASS' if ok else 'FAIL'}: {name} ({detail})")


if __name__ == "__main__":
    asyncio.run(main())
