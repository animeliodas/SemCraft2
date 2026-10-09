"""Install SemCraft 2 builds: the Sam plugin into the games' Bin/Plugins and the Fabric mod into a Minecraft instance.

    python tools/install.py [--tfe DIR] [--tse DIR] [--mc INSTANCE_DIR] [--uninstall]

Defaults are this machine's folders (the game copies next to the repository, the CurseForge instance "SemCraft").
Classics Patch 1.9.4 must already be installed in each game, and Fabric Loader + Fabric API in the instance.
Files from an older SemCraft attempt in the instance's mods folder are moved to mods_disabled (never deleted).
"""
import argparse
import shutil
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
ROOT = REPO.parent
DEFAULTS = {
    "tfe": ROOT / "Serious Sam Classic The First Encounter",
    "tse": ROOT / "Serious Sam Classic The Second Encounter",
    "mc": Path.home() / "curseforge" / "minecraft" / "Instances" / "SemCraft",
}
PLUGIN = {"tfe": REPO / "sam" / "dist" / "Release_TFE105" / "SemCraft2.dll",
          "tse": REPO / "sam" / "dist" / "Release_TSE107" / "SemCraft2.dll"}
JAR = REPO / "mc" / "build" / "libs" / "semcraft2-0.1.0.jar"


def install_plugin(game: str, folder: Path, uninstall: bool) -> None:
    plugins = folder / "Bin" / "Plugins"
    if not (folder / "Bin" / "ClassicsCore.dll").exists():
        print(f"{game}: Classics Patch isn't installed in {folder} (no Bin/ClassicsCore.dll); skipped")
        return
    target = plugins / "SemCraft2.dll"
    if uninstall:
        if target.exists():
            target.unlink()
            print(f"{game}: removed {target}")
        return
    if not PLUGIN[game].exists():
        print(f"{game}: no build at {PLUGIN[game]} (run sam/build.sh); skipped")
        return
    try:
        shutil.copy2(PLUGIN[game], target)
    except PermissionError:
        print(f"{game}: {target} is in use (close Serious Sam first); skipped")
        return
    print(f"{game}: installed {target}")


def install_mod(instance: Path, uninstall: bool) -> None:
    mods = instance / "mods"
    if not mods.is_dir():
        print(f"minecraft: no mods folder in {instance}; skipped")
        return
    target = mods / JAR.name
    if uninstall:
        if target.exists():
            target.unlink()
            print(f"minecraft: removed {target}")
        return
    disabled = instance / "mods_disabled"
    for old in mods.glob("semcraft-*.jar"):  # the first SemCraft attempt (mod id "semcraft")
        disabled.mkdir(exist_ok=True)
        shutil.move(str(old), str(disabled / old.name))
        print(f"minecraft: moved the old {old.name} to {disabled}")
    for old in mods.glob("semcraft2-*.jar"):
        old.unlink()
    if not JAR.exists():
        print(f"minecraft: no build at {JAR} (run ./gradlew build in mc); skipped")
        return
    shutil.copy2(JAR, target)
    print(f"minecraft: installed {target}")
    if not any(mods.glob("fabric-api*.jar")):
        print("minecraft: WARNING no fabric-api jar in the mods folder: install Fabric API 0.161.0+26.3")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--tfe", type=Path, default=DEFAULTS["tfe"])
    ap.add_argument("--tse", type=Path, default=DEFAULTS["tse"])
    ap.add_argument("--mc", type=Path, default=DEFAULTS["mc"])
    ap.add_argument("--uninstall", action="store_true")
    a = ap.parse_args()
    install_plugin("tfe", a.tfe, a.uninstall)
    install_plugin("tse", a.tse, a.uninstall)
    install_mod(a.mc, a.uninstall)


if __name__ == "__main__":
    sys.exit(main())
