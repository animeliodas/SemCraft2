"""Dev: which SemCraft2 subsystem makes a level hang Sam at its start? (Minecraft running.)

    uv run -q --with pillow --with websockets python tools/bisect_hang.py <level> <mask> [<mask> ...]

For each mask: start Sam (tools/dev.py sam tfe), set sc_iDisable=<mask>, StartMap(level), wait, and report whether the
game still answers.
"""
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent


def dev(*args: str) -> str:
    return subprocess.run([sys.executable, str(HERE / "dev.py"), *args], capture_output=True, text=True).stdout


def responding() -> str:
    return subprocess.run(["powershell", "-NoProfile", "-Command", "(Get-Process SeriousSam_Custom).Responding"],
                          capture_output=True, text=True).stdout.strip()


level = sys.argv[1]
for mask in sys.argv[2:]:
    dev("sam-stop")
    time.sleep(2)
    dev("sam", "tfe")
    dev("cmd", "tfe", f"sc_iDisable={mask};")
    time.sleep(3)
    dev("cmd", "tfe", f'StartMap("{level}");')
    states = []
    for _ in range(8):
        time.sleep(10)
        states.append(responding())
    print(f"mask {mask}: {' '.join(states)}", flush=True)
dev("sam-stop")
