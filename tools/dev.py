"""SemCraft 2 dev harness: drive Serious Sam and Minecraft for tests without touching the user's mouse or keyboard.

    uv run --with pillow --with websockets python tools/dev.py <command> ...

Commands
    sam <tfe|tse> [level]      start Sam (SEMCRAFT2_DEV=1); after the menu is up, start `level` (e.g. 01_Hatshepsut)
    cmd <tfe|tse> "<console>"  run a Sam console command (dev hook: %TEMP%/SemCraft2/cmd.txt)
    shot <tfe|tse> <out.png>   Sam's finished frame (Sam + Minecraft) as PNG
    log <tfe|tse> [n]          last n lines of Sam's log
    mclog [n]                  last n lines of the dev Minecraft log (mc/run/logs/latest.log)
    mc '<json>'                send one JSON message to the Minecraft mod's link and print what comes back (1 s)
    wait-log <tfe|tse> <text> [seconds]   wait until Sam's log contains text
    ps                         SeriousSam/java processes (pid, name, title)
    kill <pid>                 stop one process by exact PID
    mc-start / mc-stop         the Minecraft dev client (gradlew runClient), found by its KnotClient command line
    sam-stop                   every SeriousSam_Custom process, by PID
"""
import json
import os
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]  # ...\SemCraft2
REPO = ROOT / "semcraft"
GAMES = {
    "tfe": ROOT / "Serious Sam Classic The First Encounter",
    "tse": ROOT / "Serious Sam Classic The Second Encounter",
}
DEV_DIR = Path(tempfile.gettempdir()) / "SemCraft2"
CMD_FILE = DEV_DIR / "cmd.txt"
MC_LOG = REPO / "mc" / "run" / "logs" / "latest.log"


def game_dir(name: str) -> Path:
    if name not in GAMES:
        sys.exit(f"unknown game {name!r}: tfe or tse")
    return GAMES[name]


def sam_log(name: str) -> Path:
    return game_dir(name) / "SeriousSam_Custom.log"


def read_log(path: Path) -> str:
    try:
        return path.read_text(encoding="cp1252", errors="replace")
    except FileNotFoundError:
        return ""


def send_console(cmd: str) -> None:
    DEV_DIR.mkdir(exist_ok=True)
    # the plugin renames cmd.txt away before reading it: append-or-create is enough
    with open(CMD_FILE, "a", encoding="ascii") as f:
        f.write(cmd + "\n")


def wait_log(name: str, text: str, seconds: float) -> bool:
    end = time.time() + seconds
    while time.time() < end:
        if text in read_log(sam_log(name)):
            return True
        time.sleep(0.5)
    return False


def start_sam(name: str, level: str | None) -> None:
    d = game_dir(name)
    exe = d / "Bin" / "SeriousSam_Custom.exe"
    log = sam_log(name)
    if log.exists():
        log.unlink()

    env = dict(os.environ, SEMCRAFT2_DEV="1")
    p = subprocess.Popen([str(exe)], cwd=str(d / "Bin"), env=env, creationflags=subprocess.DETACHED_PROCESS)
    print(f"started {exe.name} pid {p.pid}")
    if not wait_log(name, "[SemCraft2]^r loaded (built", 60):
        print("plugin didn't report loading within 60 s; see the log")
        return
    print("plugin loaded")
    if level:
        # let the menu come up fully before starting a level (starting during init races)
        time.sleep(8)
        send_console(f'StartMap("{level}");')
        print(f"asked for level {level}")


def shot(name: str, out: str) -> None:
    bmp = DEV_DIR / f"shot_{int(time.time() * 1000)}.bmp"
    send_console(f'sc_Shot("{bmp.as_posix()}");')
    end = time.time() + 10
    while time.time() < end and not bmp.exists():
        time.sleep(0.2)
    time.sleep(0.3)
    if not bmp.exists():
        sys.exit("no screenshot (is a game running and drawing?)")
    from PIL import Image

    Image.open(bmp).save(out)
    bmp.unlink()
    print(f"saved {out}")


def mc_send(message: str) -> None:
    import asyncio

    import websockets

    async def run() -> None:
        async with websockets.connect("ws://127.0.0.1:25610") as ws:
            await ws.send(message)
            end = time.time() + 1.0
            while time.time() < end:
                try:
                    reply = await asyncio.wait_for(ws.recv(), timeout=max(0.05, end - time.time()))
                    print(reply)
                except asyncio.TimeoutError:
                    break

    asyncio.run(run())


def ps() -> None:
    script = (
        "Get-Process | Where-Object { $_.ProcessName -match 'SeriousSam|java' } | "
        "Select-Object Id,ProcessName,MainWindowTitle | ConvertTo-Json -Compress"
    )
    out = subprocess.run(["powershell", "-NoProfile", "-Command",
                          "[Console]::OutputEncoding=[Text.Encoding]::UTF8; " + script],
                         capture_output=True, text=True, encoding="utf-8", errors="replace").stdout
    if not out.strip():
        print("none")
        return
    items = json.loads(out)
    if isinstance(items, dict):
        items = [items]
    for i in items:
        print(i["Id"], i["ProcessName"], repr(i["MainWindowTitle"]))


JDK = r"C:\Program Files\Eclipse Adoptium\jdk-25.0.2.10-hotspot"


def mc_client_pids() -> list[int]:
    script = ("Get-CimInstance Win32_Process -Filter \"Name='java.exe' or Name='javaw.exe'\" | "
              "Where-Object { $_.CommandLine -match 'KnotClient' } | ForEach-Object { $_.ProcessId }")
    out = subprocess.run(["powershell", "-NoProfile", "-Command", script], capture_output=True, text=True).stdout
    return [int(x) for x in out.split() if x.strip().isdigit()]


def mc_start() -> None:
    if mc_client_pids():
        print("Minecraft dev client already running:", mc_client_pids())
        return
    env = dict(os.environ, JAVA_HOME=JDK)
    try:
        MC_LOG.unlink()  # the old run's log would look like this one has started
    except FileNotFoundError:
        pass
    log = open(REPO / "mc" / "run-client.log", "w", encoding="utf-8")
    subprocess.Popen(["cmd", "/c", str(REPO / "mc" / "gradlew.bat"), "runClient", "--console=plain"], cwd=str(REPO / "mc"), env=env,
                     stdout=log, stderr=subprocess.STDOUT, creationflags=subprocess.CREATE_NEW_PROCESS_GROUP)
    end = time.time() + 240
    while time.time() < end:
        text = read_log(MC_LOG)
        if "Sam link listening" in text and ("opening world" in text or "creating world" in text) and mc_client_pids():
            time.sleep(5)
            print("Minecraft up, pid", mc_client_pids())
            return
        time.sleep(2)
    print("Minecraft didn't come up in 240 s; see mc/run-client.log")


def mc_stop() -> None:
    for pid in mc_client_pids():
        subprocess.run(["taskkill", "/PID", str(pid), "/F"], capture_output=True)
        print("stopped Minecraft pid", pid)


def sam_pids() -> list[int]:
    script = "Get-Process SeriousSam_Custom -ErrorAction SilentlyContinue | ForEach-Object { $_.Id }"
    out = subprocess.run(["powershell", "-NoProfile", "-Command", script], capture_output=True, text=True).stdout
    return [int(x) for x in out.split() if x.strip().isdigit()]


def sam_stop() -> None:
    for pid in sam_pids():
        subprocess.run(["taskkill", "/PID", str(pid), "/F"], capture_output=True)
        print("stopped Sam pid", pid)


def main() -> None:
    if len(sys.argv) < 2:
        print(__doc__)
        return
    c, a = sys.argv[1], sys.argv[2:]
    if c == "sam":
        start_sam(a[0], a[1] if len(a) > 1 else None)
    elif c == "cmd":
        send_console(a[1])
    elif c == "shot":
        shot(a[0], a[1])
    elif c == "log":
        n = int(a[1]) if len(a) > 1 else 40
        print("\n".join(read_log(sam_log(a[0])).splitlines()[-n:]))
    elif c == "mclog":
        n = int(a[0]) if a else 40
        print("\n".join(read_log(MC_LOG).splitlines()[-n:]))
    elif c == "wait-log":
        ok = wait_log(a[0], a[1], float(a[2]) if len(a) > 2 else 30)
        print("found" if ok else "not found")
        sys.exit(0 if ok else 1)
    elif c == "mc":
        mc_send(a[0])
    elif c == "ps":
        ps()
    elif c == "kill":
        subprocess.run(["taskkill", "/PID", a[0], "/F"])
    elif c == "mc-start":
        mc_start()
    elif c == "mc-stop":
        mc_stop()
    elif c == "sam-stop":
        sam_stop()
    else:
        print(__doc__)


if __name__ == "__main__":
    main()
