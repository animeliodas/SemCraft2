"""Oracle for physics and transport through the SeriousBridge core (both games running; TFE on a flat level).

    uv run -q --with websockets --with pillow python tools/test_physics.py [--game tfe] [--only NAME,...] [--level 11_AlleyOfSphinxes]

  knockback  a Minecraft blow on a Sam monster's stand-in knocks the monster back in Sam (it used to only hurt it);
  boat       a Minecraft boat on Minecraft water carries Sam's player: mounted, steered forward by Sam's movement keys,
             he goes with it (picture in _shots/boat.png, third person), and gets off.
Each check on a fresh level. Prints PASS/FAIL per check.
"""
import argparse
import asyncio
import json
import re
import subprocess
import sys
from pathlib import Path

import websockets

from test_wall import GAMES, SamLog, clean, request, sam, start_level
from test_deeper import command_output, where

ALL = ("knockback", "boat")
ROOT = Path(__file__).resolve().parents[1]


async def kleer_distance(log) -> float | None:
    out = await log.after("sc_Monsters();", "monsters:")
    m = re.findall(r"monster Boneman \(\d+\) health [\d.-]+ at ([\d.]+) m", out)
    near = [float(x) for x in m if float(x) < 15.0]
    return near[0] if near else None


async def test_knockback(ws, log, results):
    sam("sc_fTestTurnH=0;", "sc_fTestTurnP=-2.5;", "sc_iTestTurn=10;")
    await asyncio.sleep(1.5)
    await log.after('sc_SpawnSam("Boneman");', "spawned", timeout=3.0)
    sam("sc_fTestTurnH=0;", "sc_fTestTurnP=2.5;", "sc_iTestTurn=10;")
    await asyncio.sleep(2.5)
    d0 = await kleer_distance(log)
    # a blow from the player, as a sword's
    await ws.send(json.dumps({"t": "cmd", "c": "execute as @p run damage @e[tag=sam_monster,limit=1,sort=nearest] 2 minecraft:player_attack by @p"}))
    await asyncio.sleep(1.0)
    d1 = await kleer_distance(log)
    ok = d0 is not None and d1 is not None and d1 - d0 > 0.7
    results.append(("a Minecraft blow knocks a Sam monster back", ok, f"{d0} m -> {d1} m from the player"))
    sam("cht_bKillAll=1;")


async def test_boat(ws, log, results):
    st = await request(ws, {"t": "status"}, "status", timeout=3.0)
    x, y, z = (int(v // 1) for v in st["player"]["mc"])
    # a canal along +z from where the player stands: two blocks of water, five wide, thirty long
    await ws.send(json.dumps({"t": "cmd", "c": f"fill {x - 2} {y} {z - 3} {x + 2} {y + 1} {z + 30} minecraft:water replace air"}))
    await asyncio.sleep(2.0)
    await ws.send(json.dumps({"t": "cmd", "c": f"summon minecraft:oak_boat {x + 0.5} {y + 1.5} {z + 0.5} {{Rotation:[0f,0f]}}"}))
    await asyncio.sleep(1.0)
    mark = len(log.text())
    await ws.send(json.dumps({"t": "cmd", "c": "ride @p mount @e[type=minecraft:oak_boat,limit=1,sort=nearest]"}))
    await asyncio.sleep(2.0)
    mounted = "riding a oak_boat" in log.text()[mark:]
    w0 = await where(log)
    sam("sc_iTestWalk=50;")  # forward, 2.5 s
    await asyncio.sleep(3.0)
    w1 = await where(log)
    went = (w1["p"][2] - w0["p"][2]) if w0 and w1 else 0.0
    # how it looks, from behind (third person: Steve in the boat)
    sam("sc_iTestButtons=128;")
    await asyncio.sleep(1.5)
    subprocess.run([sys.executable, str(ROOT / "tools" / "dev.py"), "shot", "tfe", str(ROOT.parent / "_shots" / "boat.png")], capture_output=True, timeout=60)
    sam("sc_iTestButtons=128;")
    await asyncio.sleep(1.0)
    mark = len(log.text())
    await ws.send(json.dumps({"t": "cmd", "c": "ride @p dismount"}))
    await asyncio.sleep(1.5)
    off = "off the oak_boat" in log.text()[mark:]
    results.append(("a Minecraft boat carries Sam's player, steered by his keys", mounted and went > 3.0 and off,
                    f"mounted {mounted}, went {went:+.1f} m along the canal (+z), got off {off}"))
    await ws.send(json.dumps({"t": "cmd", "c": f"fill {x - 2} {y} {z - 3} {x + 2} {y + 1} {z + 30} minecraft:air replace minecraft:water"}))
    await ws.send(json.dumps({"t": "cmd", "c": "kill @e[type=minecraft:oak_boat]"}))
    await asyncio.sleep(1.0)


async def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--game", default="tfe", choices=GAMES)
    ap.add_argument("--only", default=",".join(ALL))
    ap.add_argument("--level", default="11_AlleyOfSphinxes", help="started afresh before each check ('' = no restarts)")
    a = ap.parse_args()
    log = SamLog(a.game)
    results = []
    tests = {"knockback": test_knockback, "boat": test_boat}

    async with websockets.connect("ws://127.0.0.1:25610", max_size=None, max_queue=None, ping_interval=None) as ws:
        await ws.recv()
        for name in a.only.split(","):
            if a.level:
                await start_level(log, a.level)
            sam("sc_CloseNetricsa();", "cht_bGod=1;", "cht_bInvisible=1;", "con_iLastLines=0;")
            await asyncio.sleep(1.0)
            await clean(ws)
            try:
                await tests[name](ws, log, results)
            except Exception as e:  # one broken check mustn't hide the others
                results.append((name, False, f"{type(e).__name__}: {e}"))
        sam("cht_bInvisible=0;", "con_iLastLines=5;")

    for name, ok, detail in results:
        print(f"{'PASS' if ok else 'FAIL'}: {name} ({detail})")


if __name__ == "__main__":
    asyncio.run(main())
