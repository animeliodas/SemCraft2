"""Scripted showcase take: both games running (tools/dev.py mc-start, sam tfe 01_Hatshepsut), Sam's window recorded.

    uv run -q --with websockets python tools/director.py <out base path> [--seconds 34] [--no-record]

Everything goes through the plugin's dev hook (Sam console commands, test hooks for fire/turn/walk/weapon) and the
Minecraft link, so the user's mouse and keyboard are never touched. The level is restarted first.
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


class Mc:
    def __init__(self, ws):
        self.ws = ws

    async def send(self, **m) -> None:
        await self.ws.send(json.dumps(m))

    async def click(self, key: str = "use") -> None:
        await self.send(t="key", k=key, down=True)
        await asyncio.sleep(0.12)
        await self.send(t="key", k=key, down=False)


async def drain(ws) -> None:
    try:
        while True:
            await ws.recv()
    except Exception:
        pass


async def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--seconds", type=float, default=34.0)
    ap.add_argument("--level", default="01_Hatshepsut")
    ap.add_argument("--no-record", action="store_true")
    a = ap.parse_args()

    async with websockets.connect("ws://127.0.0.1:25610", max_size=None) as ws:
        mc = Mc(ws)
        asyncio.get_event_loop().create_task(drain(ws))

        # --- set up: fresh level, no mobs, a clean plaza, Sam immortal and armed, no console lines on screen
        await mc.send(t="cmd", c="kill @e[type=!minecraft:player]")
        sam(f'StartMap("{a.level}");')
        await asyncio.sleep(30)
        await mc.send(t="cmd", c="kill @e[type=!minecraft:player]")
        await mc.send(t="cmd", c="time set noon")
        # whatever earlier takes and tests built around the start goes
        await ws.send(json.dumps({"t": "status"}))
        await asyncio.sleep(0.5)
        for block in ("stone_bricks", "gold_block", "grass_block", "oak_planks", "glass", "tnt", "fire", "cobblestone", "dirt"):
            await mc.send(t="cmd", c=f"execute at @p run fill ~-24 ~-6 ~-40 ~24 ~10 ~8 air replace minecraft:{block}")
        sam("con_iLastLines=0;", "cht_bGod=1;", "cht_bGiveAll=1;")
        await asyncio.sleep(1.0)
        sam("sc_iTestSelect=4;")  # tommygun / minigun
        await asyncio.sleep(1.0)
        sam("sc_iTestSelect=4;")
        await asyncio.sleep(1.5)
        # a horde on the ramp ahead
        sam('sc_Spawn("zombie 7");', 'sc_Spawn("skeleton 2");')
        await asyncio.sleep(2.0)

        rec = None
        if not a.no_record:
            cmd = f'um win record --exe SeriousSam_Custom.exe --out "{a.out}" --seconds {a.seconds}'
            rec = subprocess.Popen([r"C:\Program Files\Git\bin\bash.exe", "-lc", cmd])
            await asyncio.sleep(1.0)
        t0 = time.time()

        async def at(t: float) -> None:
            await asyncio.sleep(max(0.0, t0 + t - time.time()))

        # 0-6 s: Minecraft's horde comes down Hatshepsut's ramp; the minigun sweeps it
        await at(1.5)
        sam("sc_fTestTurnH=-0.35;", "sc_fTestTurnP=0;", "sc_iTestTurn=20;", "sc_iTestFire=110;")
        await at(2.5)
        sam("sc_fTestTurnH=0.35;", "sc_iTestTurn=40;")
        await at(4.5)
        sam("sc_fTestTurnH=-0.35;", "sc_iTestTurn=20;")

        # 7-11 s: rocket into a second wave
        await at(7.0)
        sam("sc_iTestSelect=5;")
        sam('sc_Spawn("zombie 6");', 'sc_Spawn("creeper 2");')
        await at(9.0)
        sam("sc_iTestFire=2;")
        await at(10.5)
        sam("sc_iTestFire=2;")

        # 12-20 s: build mode, a stone brick wall across the plaza
        await at(12.5)
        sam("sc_Build();")
        await mc.send(t="slot", n=2)
        sam("sc_fTestTurnH=0;", "sc_fTestTurnP=-0.6;", "sc_iTestTurn=12;")
        await at(13.5)
        sam("sc_fTestTurnH=-0.5;", "sc_fTestTurnP=0;", "sc_iTestTurn=20;")
        for i in range(8):
            await at(14.0 + i * 0.55)
            await mc.click("use")
            if i == 3:
                sam("sc_fTestTurnH=0.5;", "sc_iTestTurn=40;")

        # 19-27 s: a Serious Sam monster meets Minecraft's mobs
        await at(19.5)
        sam("sc_fTestTurnP=0.6;", "sc_fTestTurnH=0;", "sc_iTestTurn=12;")
        await at(20.5)
        sam('sc_SpawnSam("Werebull");')
        await at(21.0)
        sam('sc_Spawn("zombie 5");')

        # 27-33 s: TNT
        await at(26.5)
        await mc.send(t="slot", n=5)
        await at(27.0)
        await mc.click("use")
        await at(27.8)
        await mc.send(t="slot", n=6)
        await at(28.3)
        await mc.click("use")
        await at(29.0)
        sam("sc_Build();")

        if rec is not None:
            rec.wait()
        sam("con_iLastLines=5;")
        print("take done:", a.out)


asyncio.run(main())
