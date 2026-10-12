"""Oracle for the deeper port (both games running; Sam on a flat level, e.g. tools/dev.py sam tfe +11_AlleyOfSphinxes).

    uv run -q --with websockets python tools/test_deeper.py [--game tfe] [--only NAME,...]

  fireworks  only a firework in a hand can be lit (one in the inventory isn't enough), and it's used up there;
  bow        an arrow from the player's bow hurts a Sam monster (aiming at one used to "talk" to its stand-in);
  reset      entering a level clears Minecraft's blocks and mobs ("world"), a new game the inventory too ("all");
  chat       Minecraft's chat opens over Sam and runs a command typed there;
  chest      a barrel Minecraft opens (right click in build mode) puts Sam into screen mode, and closes again;
  piston     a piston pushes Sam's player;
  water      in Minecraft's water Sam's player sinks slowly and swims up;
  view       a view turned (as a screen's mouse or a test turns it) doesn't jump when a new game starts.
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

from test_wall import GAMES, SamLog, clean, request, sam, start_level

MC_LOG = Path(__file__).resolve().parents[1] / "mc" / "run" / "logs" / "latest.log"
ALL = ("fireworks", "bow", "reset", "chat", "chest", "piston", "water", "view")


def mc_text() -> str:
    return MC_LOG.read_text(encoding="utf-8", errors="replace")


async def mc_after(ws, message: dict, want: str, timeout: float = 2.0) -> str:
    """Send a message to Minecraft and return its log written after, once it contains `want`."""
    mark = len(mc_text())
    await ws.send(json.dumps(message))
    end = time.time() + timeout
    while time.time() < end:
        await asyncio.sleep(0.2)
        if want in mc_text()[mark:]:
            await asyncio.sleep(0.2)
            break
    return mc_text()[mark:]


async def command_output(ws, command: str) -> str:
    """Run a Minecraft command as the player (Sam's chat path) and return its output lines."""
    text = await mc_after(ws, {"t": "chat", "m": command}, "command output:")
    return "\n".join(l.split("command output: ", 1)[1] for l in text.splitlines() if "command output: " in l)


async def block_is(ws, x: int, y: int, z: int, block: str) -> bool:
    return "passed" in (await command_output(ws, f"/execute if block {x} {y} {z} {block}")).lower()


async def where(log: SamLog) -> dict:
    out = await log.after("sc_Where();", "where:", timeout=1.5)
    m = re.findall(r"where: ([\d.-]+) ([\d.-]+) ([\d.-]+) heading ([\d.-]+) pitch ([\d.-]+)(.*)", out)
    if not m:
        return {}
    v = m[-1]
    return {"p": tuple(float(x) for x in v[:3]), "heading": float(v[3]), "pitch": float(v[4]), "flags": v[5]}


async def status(log: SamLog, key: str) -> str:
    out = await log.after("sc_Status();", key, timeout=2.0)
    m = re.findall(re.escape(key) + r"[^;\n]*", out)
    return m[-1] if m else ""


async def feet_block(ws) -> tuple:
    st = await request(ws, {"t": "status"}, "status", timeout=3.0)
    x, y, z = st["player"]["mc"]
    return int(math.floor(x)), int(math.floor(y)), int(math.floor(z))


async def wait_landed(log: SamLog) -> None:
    for _ in range(40):
        await asyncio.sleep(0.5)
        if "GLIDING" not in await status(log, "elytra:"):
            return


async def test_fireworks(ws, log, results):
    await ws.send(json.dumps({"t": "cmd", "c": "item replace entity @p armor.chest with minecraft:elytra"}))
    await ws.send(json.dumps({"t": "cmd", "c": "clear @p minecraft:firework_rocket"}))
    await ws.send(json.dumps({"t": "cmd", "c": "item replace entity @p weapon.offhand with minecraft:firework_rocket 16"}))
    await asyncio.sleep(1.5)
    sam('sc_Move("0 40 0");')
    await asyncio.sleep(0.4)
    sam("sc_iTestJump=2;")
    await asyncio.sleep(1.2)
    mark = len(mc_text())
    out = await log.after("sc_Firework();", "firework:", timeout=2.0)
    await asyncio.sleep(1.0)
    used = re.findall(r"firework from the off hand: (\d+) left", mc_text()[mark:])
    ok = "firework: lit" in out and used and int(used[-1]) == 15
    results.append(("a firework in the off hand boosts and is used up there", bool(ok),
                    f"{'lit' if 'firework: lit' in out else 'not lit'}, off hand now {used[-1] if used else '?'}"))

    # none in a hand, some in the inventory: no boost
    await ws.send(json.dumps({"t": "cmd", "c": "item replace entity @p weapon.offhand with minecraft:air"}))
    await ws.send(json.dumps({"t": "slot", "n": 0}))
    await ws.send(json.dumps({"t": "cmd", "c": "item replace entity @p inventory.5 with minecraft:firework_rocket 16"}))
    await asyncio.sleep(1.0)
    line = await status(log, "elytra:")
    out = await log.after("sc_Firework();", "firework:", timeout=2.0)
    ok = "firework: lit" not in out and "none in hand" in line
    results.append(("fireworks only in the inventory don't boost", ok, line.strip()))
    await wait_landed(log)


async def aim_at(ws, target, log) -> None:
    for _ in range(3):
        c = await request(ws, {"t": "status"}, "client", timeout=3.0)
        eye, (yaw, pitch) = c["eye"], c["rot"]
        dx, dy, dz = target[0] - eye[0], target[1] - eye[1], target[2] - eye[2]
        want_yaw = math.degrees(math.atan2(-dx, dz))
        want_pitch = -math.degrees(math.atan2(dy, math.hypot(dx, dz)))
        dyaw = (want_yaw - yaw + 180) % 360 - 180
        dpitch = want_pitch - pitch
        if abs(dyaw) < 0.5 and abs(dpitch) < 0.5:
            return
        sam("sc_fTestTurnH=%.4f;" % (-dyaw / 10), "sc_fTestTurnP=%.4f;" % (-dpitch / 10), "sc_iTestTurn=10;")
        await asyncio.sleep(1.5)


async def kleer(log) -> tuple:
    """The Kleers within 10 m: their ids and their health in all."""
    out = await log.after("sc_Monsters();", "monsters:")
    m = re.findall(r"monster Boneman \((\d+)\) health ([\d.-]+) at ([\d.]+) m", out)
    near = [x for x in m if float(x[2]) < 10.0]
    return ([int(x[0]) for x in near], sum(float(x[1]) for x in near)) if near else (None, None)


async def test_bow(ws, log, results):
    # a Kleer where the crosshair meets the ground a few metres ahead (one from an earlier run will do: only one near)
    out = ""
    kid, before = await kleer(log)
    if kid is None:
        sam("sc_fTestTurnH=0;", "sc_fTestTurnP=-2.5;", "sc_iTestTurn=10;")
        await asyncio.sleep(1.5)
        out = await log.after('sc_SpawnSam("Boneman");', "spawned", timeout=3.0)
        sam("sc_fTestTurnH=0;", "sc_fTestTurnP=2.5;", "sc_iTestTurn=10;")
        await asyncio.sleep(2.0)
        kid, before = await kleer(log)
    text = await command_output(ws, "/data get entity @e[tag=sam_monster,limit=1,sort=nearest] Pos")
    m = re.findall(r"\[([-\d.]+)d, ([-\d.]+)d, ([-\d.]+)d\]", text)
    if kid is None or not m:
        results.append(("the bow's arrow hurts a Sam monster", False, f"no Kleer or no stand-in ({out.strip()[-120:]})"))
        return
    p = tuple(float(v) for v in m[-1])
    await aim_at(ws, (p[0], p[1] + 1.1, p[2]), log)
    await ws.send(json.dumps({"t": "cmd", "c": "item replace entity @p hotbar.8 with minecraft:bow"}))
    await ws.send(json.dumps({"t": "cmd", "c": "give @p minecraft:arrow 16"}))
    await asyncio.sleep(0.5)
    sam("sc_Build();")
    await asyncio.sleep(0.5)
    await ws.send(json.dumps({"t": "slot", "n": 8}))
    await asyncio.sleep(0.3)
    await ws.send(json.dumps({"t": "key", "k": "use", "down": True}))
    await asyncio.sleep(1.3)
    await ws.send(json.dumps({"t": "key", "k": "use", "down": False}))
    await asyncio.sleep(1.5)
    sam("sc_Build();")
    _, after = await kleer(log)
    ok = after is None or after < before
    results.append(("the bow's arrow hurts a Sam monster", ok, f"Kleers {kid} health {before} -> {after if after is not None else 'dead'}"))
    sam("sc_fTestTurnH=0;", "sc_fTestTurnP=%.3f;" % (-(await where(log)).get("pitch", 0.0) / 10), "sc_iTestTurn=10;")  # level again
    # the hurt Kleer goes for the player (and so would the rest of the level): no monsters left for the next checks
    sam("cht_bKillAll=1;")
    await asyncio.sleep(1.5)


async def test_reset(ws, log, results):
    bx, by, bz = await feet_block(ws)
    spots = [(bx + 3, by + 2, bz + 3), (bx - 3, by + 2, bz + 3), (bx + 3, by + 2, bz - 3)]
    for s in spots:
        await ws.send(json.dumps({"t": "cmd", "c": "setblock %d %d %d minecraft:gold_block" % s}))
    await ws.send(json.dumps({"t": "cmd", "c": f"summon minecraft:zombie {bx + 4} {by + 3} {bz} {{NoAI:1b,Tags:[\"semtest\"]}}"}))
    await ws.send(json.dumps({"t": "cmd", "c": "give @p minecraft:diamond 5"}))
    await asyncio.sleep(1.5)
    placed = [await block_is(ws, *s, "minecraft:gold_block") for s in spots]
    zombie = "passed" in (await command_output(ws, "/execute if entity @e[tag=semtest]")).lower()

    mark = len(log.text())
    sam('sc_Reset("world");')
    await asyncio.sleep(2.0)
    gone = [not await block_is(ws, *s, "minecraft:gold_block") for s in spots]
    zombie_after = "passed" in (await command_output(ws, "/execute if entity @e[tag=semtest]")).lower()
    diamonds = await command_output(ws, "/clear @p minecraft:diamond 0")
    kept = "found 5" in diamonds.lower()
    told = "Minecraft world reset (world" in log.text()[mark:]
    results.append(('reset "world": blocks and mobs gone, inventory kept', all(placed) and zombie and all(gone) and not zombie_after and kept and told,
                    f"blocks placed {sum(placed)}/3 gone {sum(gone)}/3, zombie {zombie}->{zombie_after}, diamonds: {diamonds.strip()}, Sam told {told}"))

    sam('sc_Reset("all");')
    await asyncio.sleep(2.5)
    diamonds = await command_output(ws, "/clear @p minecraft:diamond 0")
    client = await request(ws, {"t": "status"}, "client", timeout=3.0)
    kit = client.get("hotbar", [])
    ok = "no items" in diamonds.lower() and len(kit) == 9 and kit[0].startswith("minecraft:diamond_sword") and kit[5].startswith("minecraft:tnt")
    results.append(('reset "all": the inventory is the starter kit again', ok, f"diamonds: {diamonds.strip()}, hotbar {kit}"))


async def test_chat(ws, log, results):
    word = f"semtest-chat-{int(time.time())}"
    sam(f'sc_Chat("/say {word}");')
    await asyncio.sleep(1.0)
    client = await request(ws, {"t": "status"}, "client", timeout=3.0)
    opened = client.get("screen") == "ChatScreen" and "screen: open" in await status(log, "screen:")
    mark = len(mc_text())
    await ws.send(json.dumps({"t": "keydown", "sc": 40, "kc": 13, "mods": 0}))  # Enter, as Sam forwards it
    await asyncio.sleep(1.5)
    said = word in mc_text()[mark:]
    client = await request(ws, {"t": "status"}, "client", timeout=3.0)
    closed = client.get("screen") == "" and "screen: none" in await status(log, "screen:")
    results.append(("Minecraft's chat opens over Sam and runs a command", opened and said and closed,
                    f"opened {opened}, command ran {said}, closed again {closed}"))


async def test_chest(ws, log, results):
    # away from the bow check's Kleer: its stand-in would be in the way
    sam("sc_fTestTurnH=9;", "sc_fTestTurnP=0;", "sc_iTestTurn=10;")
    await asyncio.sleep(1.5)
    client = await request(ws, {"t": "status"}, "client", timeout=3.0)
    eye, (yaw, pitch) = client["eye"], client["rot"]
    cp = math.cos(math.radians(pitch))
    d = (-math.sin(math.radians(yaw)) * cp, -math.sin(math.radians(pitch)), math.cos(math.radians(yaw)) * cp)
    bx, by, bz = (int(math.floor(eye[i] + d[i] * 3.0)) for i in range(3))
    await ws.send(json.dumps({"t": "cmd", "c": f"setblock {bx} {by} {bz} minecraft:barrel"}))
    await asyncio.sleep(1.0)
    sam("sc_Build();")
    await asyncio.sleep(0.5)
    await ws.send(json.dumps({"t": "slot", "n": 0}))
    mark = len(log.text())
    await ws.send(json.dumps({"t": "key", "k": "use", "down": True}))
    await asyncio.sleep(0.3)
    await ws.send(json.dumps({"t": "key", "k": "use", "down": False}))
    await asyncio.sleep(1.5)
    client = await request(ws, {"t": "status"}, "client", timeout=3.0)
    opened = "Minecraft opened" in log.text()[mark:] and "screen: open" in await status(log, "screen:")
    what = client.get("screen")
    sam("sc_Inventory(0);")  # Sam closes it (as Escape would)
    await asyncio.sleep(1.5)
    client = await request(ws, {"t": "status"}, "client", timeout=3.0)
    closed = client.get("screen") == "" and "screen: none" in await status(log, "screen:")
    sam("sc_Build();")
    await ws.send(json.dumps({"t": "cmd", "c": f"setblock {bx} {by} {bz} minecraft:air"}))
    results.append(("a barrel Minecraft opens works from Sam, and closes", opened and closed, f"MC screen {what!r}, Sam followed {opened}, closed {closed}"))


async def test_piston(ws, log, results):
    bx, by, bz = await feet_block(ws)
    w0 = await where(log)
    # body height: a piston one block west, pushing east, with a redstone block behind it
    y = by + 1
    await ws.send(json.dumps({"t": "cmd", "c": f"setblock {bx - 1} {y} {bz} minecraft:piston[facing=east]"}))
    await asyncio.sleep(1.0)
    await ws.send(json.dumps({"t": "cmd", "c": f"setblock {bx - 2} {y} {bz} minecraft:redstone_block"}))
    await asyncio.sleep(1.5)
    w1 = await where(log)
    dx = w1["p"][0] - w0["p"][0] if w0 and w1 else 0.0
    results.append(("a piston pushes Sam's player", 0.4 < dx < 1.6, f"moved {dx:+.2f} m along +x"))
    for p in ((bx - 2, y, bz), (bx - 1, y, bz), (bx, y, bz)):
        await ws.send(json.dumps({"t": "cmd", "c": "setblock %d %d %d minecraft:air" % p}))
    await asyncio.sleep(1.0)

    # and a Sam monster (a Kleer a few metres ahead; the player is invisible to it): its stand-in is pushed, then the monster
    if (await kleer(log))[0] is None:
        sam("sc_fTestTurnH=0;", "sc_fTestTurnP=-2.5;", "sc_iTestTurn=10;")
        await asyncio.sleep(1.5)
        await log.after('sc_SpawnSam("Boneman");', "spawned", timeout=3.0)
        sam("sc_fTestTurnH=0;", "sc_fTestTurnP=2.5;", "sc_iTestTurn=10;")
        await asyncio.sleep(2.5)
    text = await command_output(ws, "/data get entity @e[tag=sam_monster,limit=1,sort=nearest] Pos")
    m = re.findall(r"\[([-\d.]+)d, ([-\d.]+)d, ([-\d.]+)d\]", text)
    if not m:
        results.append(("a piston pushes a Sam monster", False, "no Sam monster near"))
        return
    px, py, pz = (int(math.floor(float(v))) for v in m[-1])
    before = await status(log, "monsters:")
    await ws.send(json.dumps({"t": "cmd", "c": f"setblock {px - 1} {py + 1} {pz} minecraft:piston[facing=east]"}))
    await asyncio.sleep(1.0)
    await ws.send(json.dumps({"t": "cmd", "c": f"setblock {px - 2} {py + 1} {pz} minecraft:redstone_block"}))
    await asyncio.sleep(1.5)
    after = await status(log, "monsters:")
    n0, n1 = (int(re.search(r"(\d+) piston pushes", s).group(1)) if re.search(r"(\d+) piston pushes", s) else -1 for s in (before, after))
    results.append(("a piston pushes a Sam monster", n1 > n0 >= 0, f"monster pushes {n0} -> {n1}"))
    for p in ((px - 2, py + 1, pz), (px - 1, py + 1, pz), (px, py + 1, pz)):
        await ws.send(json.dumps({"t": "cmd", "c": "setblock %d %d %d minecraft:air" % p}))
    sam("cht_bKillAll=1;")
    await asyncio.sleep(1.0)


async def test_water(ws, log, results):
    bx, by, bz = await feet_block(ws)
    # a pool 7 x 7, 6 deep, from the body up
    await ws.send(json.dumps({"t": "cmd", "c": f"fill {bx - 3} {by + 1} {bz - 3} {bx + 3} {by + 6} {bz + 3} minecraft:water replace air"}))
    await asyncio.sleep(1.5)
    sam('sc_Move("0 4 0");')
    await asyncio.sleep(0.3)
    w0 = await where(log)
    await asyncio.sleep(1.0)
    w1 = await where(log)
    line = await status(log, "fluid:")
    drop = w0["p"][1] - w1["p"][1] if w0 and w1 else 99.0
    results.append(("in Minecraft's water Sam's player sinks slowly", "SWIMMING" in line and 0.0 <= drop < 1.5, f"{drop:.2f} m in 1 s; {line.strip()}"))

    sam("sc_iTestJump=30;")
    await asyncio.sleep(1.2)
    w2 = await where(log)
    rise = w2["p"][1] - w1["p"][1] if w1 and w2 else -99.0
    results.append(("jump swims him up", rise > 0.5, f"{rise:+.2f} m"))
    await asyncio.sleep(1.0)
    await ws.send(json.dumps({"t": "cmd", "c": f"fill {bx - 3} {by + 1} {bz - 3} {bx + 3} {by + 6} {bz + 3} minecraft:air replace minecraft:water"}))
    await asyncio.sleep(2.0)


async def test_view(ws, log, results, level="11_AlleyOfSphinxes"):
    async def view():  # the camera Minecraft gets from Sam (what the player sees): yaw, pitch
        await asyncio.sleep(0.5)
        return tuple((await request(ws, {"t": "status"}, "client", timeout=3.0))["rot"])

    await start_level(log, level)
    v0 = await view()
    sam("sc_fTestTurnH=3;", "sc_fTestTurnP=-1.5;", "sc_iTestTurn=10;")
    await asyncio.sleep(1.5)
    v1 = await view()
    await start_level(log, level)
    v2 = await view()
    turned = abs((v1[0] - v0[0] + 180) % 360 - 180) > 20 and abs(v1[1] - v0[1]) > 10
    same = abs((v2[0] - v0[0] + 180) % 360 - 180) < 1.0 and abs(v2[1] - v0[1]) < 1.0
    results.append(("a turned view starts the next game as the level says", turned and same,
                    f"yaw/pitch at the start {v0[0]:.1f}/{v0[1]:.1f}, turned {v1[0]:.1f}/{v1[1]:.1f}, next game {v2[0]:.1f}/{v2[1]:.1f}"))


async def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--game", default="tfe", choices=GAMES)
    ap.add_argument("--only", default=",".join(ALL))
    ap.add_argument("--level", default="11_AlleyOfSphinxes", help="started afresh before each check ('' = no restarts)")
    a = ap.parse_args()
    log = SamLog(a.game)
    results = []
    tests = {"fireworks": test_fireworks, "bow": test_bow, "reset": test_reset, "chat": test_chat, "chest": test_chest,
             "piston": test_piston, "water": test_water, "view": test_view}

    async with websockets.connect("ws://127.0.0.1:25610", max_size=None, max_queue=None, ping_interval=None) as ws:
        await ws.recv()
        sam("sc_CloseNetricsa();", "cht_bGod=1;", "cht_bInvisible=1;", "con_iLastLines=0;")
        await clean(ws)
        home = (await where(log)).get("p")
        for i, name in enumerate(a.only.split(",")):
            if a.level and i > 0:
                # one check at a time on a fresh level: no monsters woken or hurt by the one before, the player at the start
                await start_level(log, a.level)
                sam("cht_bGod=1;", "cht_bInvisible=1;", "con_iLastLines=0;")
                await asyncio.sleep(1.0)
                home = (await where(log)).get("p")
            try:
                await tests[name](ws, log, results)
            except Exception as e:  # one broken check mustn't hide the others
                results.append((name, False, f"{type(e).__name__}: {e}"))
            # back where the run started (a glide ends far away, maybe against a wall)
            p = (await where(log)).get("p")
            if home and p and math.dist(home, p) > 0.5:
                sam(f'sc_Move("{home[0] - p[0]:.3f} {home[1] - p[1] + 0.2:.3f} {home[2] - p[2]:.3f}");')
                await asyncio.sleep(2.0)
        sam("cht_bInvisible=0;", "con_iLastLines=5;")

    for name, ok, detail in results:
        print(f"{'PASS' if ok else 'FAIL'}: {name} ({detail})")


if __name__ == "__main__":
    asyncio.run(main())
