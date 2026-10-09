"""Screenshot of Sam's bullets cracking a Minecraft wall (both games running, Sam on 01_Hatshepsut).

    uv run -q --with pillow --with websockets python tools/shot_cracks.py <out.png> [tfe|tse]
"""
import asyncio
import json
import math
import subprocess
import sys
from pathlib import Path

import websockets

from test_wall import request, sam

HERE = Path(__file__).resolve().parent


async def main() -> None:
    out = sys.argv[1]
    game = sys.argv[2] if len(sys.argv) > 2 else "tfe"
    async with websockets.connect("ws://127.0.0.1:25610", max_size=None) as ws:
        await ws.recv()
        sam("cht_bGod=1;", "cht_bInvisible=1;", "con_iLastLines=0;")
        client = await request(ws, {"t": "status"}, "client")
        eye, yaw = client["eye"], client["rot"][0]
        ey = int(math.floor(eye[1]))
        fx, fz = -math.sin(math.radians(yaw)), math.cos(math.radians(yaw))
        cx, cz = int(math.floor(eye[0] + fx * 5)), int(math.floor(eye[2] + fz * 5))
        box = (cx, ey - 3, cz - 4, cx, ey + 2, cz + 4) if abs(fx) > abs(fz) else (cx - 4, ey - 3, cz, cx + 4, ey + 2, cz)
        await ws.send(json.dumps({"t": "cmd", "c": "fill %d %d %d %d %d %d minecraft:stone_bricks replace air" % box}))
        await asyncio.sleep(2.5)
        # a few shots each at a few spots across the wall
        for h in (0.0, -0.4, 0.8, -0.8):
            sam(f"sc_fTestTurnH={h / 4:.3f};", "sc_fTestTurnP=0;", "sc_iTestTurn=4;")
            await asyncio.sleep(0.6)
            sam("sc_iTestFire=6;")
            await asyncio.sleep(1.6)
        await asyncio.sleep(0.5)
    subprocess.run([sys.executable, str(HERE / "dev.py"), "shot", game, out], check=False)


if __name__ == "__main__":
    asyncio.run(main())
