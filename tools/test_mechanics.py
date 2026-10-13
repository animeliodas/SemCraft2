"""Oracle for Serious Sam's mechanics in Minecraft (both games running; TFE on a flat level).

    uv run -q --with websockets python tools/test_mechanics.py [--game tfe] [--only NAME,...] [--level 11_AlleyOfSphinxes]

  hearts  Sam's health above his level's top health shows as more Minecraft hearts (160 -> 16), and they go again;
  armor   Sam's armour shows on Minecraft's armour bar (120 -> 12 points);
  steve   Sam's third person view shows Steve: Minecraft's camera detaches, the hand goes (shot in _shots/steve.png);
  pearl   an ender pearl thrown in build mode carries Sam's player to where it lands;
  score   a Minecraft mob Sam kills counts in Sam's score;
  powers  (TSE) Sam's powerups are Minecraft effects: invisibility, resistance, strength, speed;
  bomb    (TSE) Sam's serious bomb kills Minecraft's monsters around too.
Each check on a fresh level. Prints PASS/FAIL per check.
"""
import argparse
import asyncio
import json
import math
import re
import subprocess
import sys
from pathlib import Path

import websockets

from test_wall import GAMES, SamLog, clean, request, sam, start_level
from test_deeper import where

ALL = ("hearts", "armor", "steve", "pearl", "score")
TSE_ONLY = ("powers", "bomb")
ROOT = Path(__file__).resolve().parents[1]


async def mc_player(ws) -> dict:
    return (await request(ws, {"t": "status"}, "status", timeout=3.0))["player"]


async def client(ws) -> dict:
    return await request(ws, {"t": "status"}, "client", timeout=3.0)


async def test_hearts(ws, log, results):
    # the level's top health: 100, or 200 on Tourist and Easy (then twenty hearts from the start)
    top = (await request(ws, {"t": "status"}, "status", timeout=3.0))["sam"]["top"]
    high = top + 60
    sam(f'sc_SetPlayer("hp {high}");')
    await asyncio.sleep(1.5)
    p1 = await mc_player(ws)
    sam('sc_SetPlayer("hp 60");')
    await asyncio.sleep(1.5)
    p2 = await mc_player(ws)
    want1 = math.ceil(high / 5 / 2) * 2
    want2 = math.ceil(top / 5 / 2) * 2
    ok = abs(p1["maxHealth"] - want1) < 2.1 and abs(p1["health"] - high / 5) < 1.0 and abs(p2["maxHealth"] - want2) < 0.1 and abs(p2["health"] - 12) < 1.0
    results.append(("health above the top is more hearts, and they go again", ok,
                    f"top {top:.0f}; Sam {high:.0f}: {p1['health']:.0f}/{p1['maxHealth']:.0f} MC (want {want1}); Sam 60: {p2['health']:.0f}/{p2['maxHealth']:.0f} (want {want2})"))
    sam(f'sc_SetPlayer("hp {top}");')


async def test_armor(ws, log, results):
    sam('sc_SetPlayer("armor 120");')
    await asyncio.sleep(1.5)
    a = (await client(ws)).get("armor")
    sam('sc_SetPlayer("armor 0");')
    await asyncio.sleep(1.5)
    b = (await client(ws)).get("armor")
    results.append(("Sam's armour is Minecraft's armour bar", a == 12 and b == 0, f"armour 120 -> {a} points, 0 -> {b}"))


async def test_steve(ws, log, results):
    before = (await client(ws)).get("thirdPerson")
    sam("sc_iTestButtons=128;")  # Player.es PLACT_3RD_PERSON_VIEW
    await asyncio.sleep(2.0)
    after = (await client(ws)).get("thirdPerson")
    shot = ROOT.parent / "_shots" / "steve.png"
    subprocess.run([sys.executable, str(ROOT / "tools" / "dev.py"), "shot", "tfe", str(shot)], capture_output=True, timeout=60)
    sam("sc_iTestButtons=128;")
    await asyncio.sleep(2.0)
    back = (await client(ws)).get("thirdPerson")
    results.append(("third person shows Steve (Minecraft's camera detaches)", before is False and after is True and back is False,
                    f"first person {before}, third person {after}, back {back}; picture {shot.name if shot.exists() else 'none'}"))


async def test_pearl(ws, log, results):
    await ws.send(json.dumps({"t": "cmd", "c": "item replace entity @p hotbar.8 with minecraft:ender_pearl 16"}))
    sam("sc_fTestTurnH=0;", "sc_fTestTurnP=-2;", "sc_iTestTurn=10;")  # 20 degrees down: it lands some metres ahead
    await asyncio.sleep(1.5)
    w0 = await where(log)
    sam("sc_Build();")
    await asyncio.sleep(0.5)
    await ws.send(json.dumps({"t": "slot", "n": 8}))
    await asyncio.sleep(0.3)
    await ws.send(json.dumps({"t": "key", "k": "use", "down": True}))
    await asyncio.sleep(0.2)
    await ws.send(json.dumps({"t": "key", "k": "use", "down": False}))
    await asyncio.sleep(3.0)
    sam("sc_Build();")
    w1 = await where(log)
    text = log.text()
    went = math.dist((w0["p"][0], w0["p"][2]), (w1["p"][0], w1["p"][2])) if w0 and w1 else 0.0
    results.append(("an ender pearl carries Sam's player where it lands", went > 3.0 and "ender pearl: to" in text, f"{went:.1f} m"))


async def test_score(ws, log, results):
    st = await mc_player(ws)
    x, y, z = st["mc"]
    await ws.send(json.dumps({"t": "cmd", "c": f"summon minecraft:zombie {x + 4:.1f} {y:.1f} {z:.1f} {{NoAI:1b,Tags:[\"semscore\"]}}"}))
    await asyncio.sleep(1.0)
    mark = len(log.text())
    await ws.send(json.dumps({"t": "cmd", "c": "execute as @p run damage @e[tag=semscore,limit=1] 100 minecraft:player_attack by @p"}))
    await asyncio.sleep(2.0)
    m = re.findall(r"Minecraft (\w+) killed: \+(\d+) score", log.text()[mark:])
    results.append(("a Minecraft mob Sam kills scores for him", bool(m) and m[-1][0] == "zombie", f"{m[-1] if m else 'nothing'}"))


async def test_powers(ws, log, results):
    sam('sc_SetPlayer("power 0 20 power 1 20 power 2 20 power 3 20");')
    await asyncio.sleep(2.5)
    on = (await mc_player(ws))["effects"]
    sam('sc_SetPlayer("power 0 0 power 1 0 power 2 0 power 3 0");')
    await asyncio.sleep(2.5)
    off = (await mc_player(ws))["effects"]
    want = ("invisibility", "resistance", "strength", "speed")
    ok = all(w in on for w in want) and not any(w in off for w in want)
    results.append(("powerups are Minecraft effects while they last", ok, f"on: {on!r}; off: {off!r}"))


async def test_bomb(ws, log, results):
    st = await mc_player(ws)
    x, y, z = st["mc"]
    for i in range(3):
        await ws.send(json.dumps({"t": "cmd", "c": f"summon minecraft:zombie {x + 6 + i * 2:.1f} {y:.1f} {z + 4:.1f} {{NoAI:1b,Tags:[\"sembomb\"]}}"}))
    await asyncio.sleep(1.0)
    before = (await request(ws, {"t": "status"}, "status", timeout=3.0))["mobs"]
    sam('sc_SetPlayer("bombs 1");')
    await asyncio.sleep(0.5)
    sam("sc_iTestButtons=8192;")  # Player.es (TSE) PLACT_FIREBOMB
    await asyncio.sleep(4.0)
    after = (await request(ws, {"t": "status"}, "status", timeout=3.0))["mobs"]
    told = "serious bomb: Minecraft's monsters" in log.text()
    results.append(("the serious bomb kills Minecraft's monsters too", told and after <= before - 3, f"mobs {before} -> {after}, Sam told {told}"))


async def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--game", default="tfe", choices=GAMES)
    ap.add_argument("--only", default=",".join(ALL))
    ap.add_argument("--level", default="11_AlleyOfSphinxes", help="started afresh before each check ('' = no restarts)")
    a = ap.parse_args()
    log = SamLog(a.game)
    results = []
    tests = {"hearts": test_hearts, "armor": test_armor, "steve": test_steve, "pearl": test_pearl, "score": test_score,
             "powers": test_powers, "bomb": test_bomb}

    async with websockets.connect("ws://127.0.0.1:25610", max_size=None, max_queue=None, ping_interval=None) as ws:
        await ws.recv()
        for name in a.only.split(","):
            if a.level:
                await start_level(log, a.level)
            sam("sc_CloseNetricsa();", "cht_bGod=1;", "cht_bInvisible=1;", "con_iLastLines=0;")
            await asyncio.sleep(1.0)
            try:
                await tests[name](ws, log, results)
            except Exception as e:  # one broken check mustn't hide the others
                results.append((name, False, f"{type(e).__name__}: {e}"))
        sam("cht_bInvisible=0;", "con_iLastLines=5;")

    for name, ok, detail in results:
        print(f"{'PASS' if ok else 'FAIL'}: {name} ({detail})")


if __name__ == "__main__":
    asyncio.run(main())
