"""Second showcase take: build a Minecraft wall across Hatshepsut's plaza, then Minecraft's zombies take on a Kleer.

    uv run -q --with websockets python tools/director_build.py <out base path> [--seconds 26]
(same set-up and rules as tools/director.py)
"""
import argparse
import asyncio
import json
import subprocess
import tempfile
import time
from pathlib import Path

import websockets

CMD = Path(tempfile.gettempdir()) / "SemCraft2" / "cmd.txt"


def sam(*commands: str) -> None:
    CMD.parent.mkdir(exist_ok=True)
    with open(CMD, "a", encoding="ascii") as f:
        for c in commands:
            f.write(c + "\n")


async def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--seconds", type=float, default=26.0)
    ap.add_argument("--level", default="01_Hatshepsut")
    a = ap.parse_args()

    async with websockets.connect("ws://127.0.0.1:25610", max_size=None) as ws:
        async def mc(**m):
            await ws.send(json.dumps(m))

        async def click():
            await mc(t="key", k="use", down=True)
            await asyncio.sleep(0.1)
            await mc(t="key", k="use", down=False)

        async def drain():
            try:
                while True:
                    await ws.recv()
            except Exception:
                pass

        asyncio.get_event_loop().create_task(drain())

        await mc(t="cmd", c="kill @e[type=!minecraft:player]")
        sam(f'StartMap("{a.level}");')
        await asyncio.sleep(30)
        await mc(t="cmd", c="kill @e[type=!minecraft:player]")
        for block in ("stone_bricks", "gold_block", "grass_block", "oak_planks", "glass", "tnt", "fire", "cobblestone"):
            await mc(t="cmd", c=f"execute at @p run fill ~-24 ~-6 ~-40 ~24 ~10 ~8 air replace minecraft:{block}")
        sam("con_iLastLines=0;", "cht_bGod=1;")
        # step off the teleporter onto the plaza, look down at the floor ahead and to the left
        sam("sc_iTestWalk=26;")
        await asyncio.sleep(2.5)
        sam("sc_fTestTurnH=1.0;", "sc_fTestTurnP=-0.7;", "sc_iTestTurn=22;")
        await asyncio.sleep(2.0)
        for block in ("stone_bricks", "gold_block", "grass_block", "oak_planks", "glass", "tnt", "fire", "cobblestone"):
            await mc(t="cmd", c=f"execute at @p run fill ~-24 ~-6 ~-40 ~24 ~10 ~16 air replace minecraft:{block}")
        sam("sc_Build();")
        await mc(t="slot", n=2)
        await asyncio.sleep(1.0)

        rec = subprocess.Popen([r"C:\Program Files\Git\bin\bash.exe", "-lc",
                                f'um win record --exe SeriousSam_Custom.exe --out "{a.out}" --seconds {a.seconds}'])
        await asyncio.sleep(1.0)
        t0 = time.time()

        async def at(t: float):
            await asyncio.sleep(max(0.0, t0 + t - time.time()))

        # 0-9 s: a wall of stone bricks across the plaza, block by block as the view sweeps right
        sam("sc_fTestTurnH=-0.32;", "sc_fTestTurnP=0;", "sc_iTestTurn=150;")
        for i in range(16):
            await at(0.4 + i * 0.48)
            await click()
        await at(8.4)
        sam("sc_fTestTurnP=0.7;", "sc_fTestTurnH=0.4;", "sc_iTestTurn=22;")
        await at(9.6)
        sam("sc_Build();")

        # 10-25 s: a Kleer (Serious Sam) behind the wall, Minecraft's zombies come for it
        await at(10.2)
        sam('sc_SpawnSam("Boneman");')
        await at(10.8)
        sam('sc_Spawn("zombie 5");')
        await at(16.0)
        sam('sc_SpawnSam("Boneman");')
        await at(16.4)
        sam('sc_Spawn("zombie 4");', 'sc_Spawn("skeleton 2");')

        rec.wait()
        sam("con_iLastLines=5;")
        print("take done:", a.out)


asyncio.run(main())
