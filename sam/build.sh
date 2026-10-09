#!/bin/bash
# Build SemCraft2.dll (Serious Sam plugin) with the VS2022 v143 toolset against the Classics Patch 1.9.4 SDK.
# Usage: ./build.sh [Release_TFE105|Release_TSE107|all]
# SDK: ../../_deps/SuperProject (git clone --branch 1.9.4 --recurse-submodules https://github.com/SamClassicPatch/SuperProject.git)
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
SP_POSIX="$(cd "$HERE/../../_deps/SuperProject" && pwd)"
SP="$(cygpath -w "$SP_POSIX")\\"
MSB="/c/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/MSBuild/Current/Bin/MSBuild.exe"
CFGS="${1:-all}"
[ "$CFGS" = all ] && CFGS="Release_TFE105 Release_TSE107"

for CFG in $CFGS; do
  # import library of ClassicsCore.dll where the linker looks for it (pragma in Core/Core.h)
  mkdir -p "$SP_POSIX/Bin/$CFG"
  cp -n "$SP_POSIX/API/lib/classicscore.lib" "$SP_POSIX/Bin/$CFG/ClassicsCore.lib" 2>/dev/null || true
  "$MSB" "$(cygpath -w "$HERE/SemCraft2.vcxproj")" -p:Configuration="$CFG" -p:Platform=Win32 \
    -p:PlatformToolset=v143 -p:SuperProjectDir="$SP" -p:SolutionDir="$SP" -m -v:m -nologo
  echo "== built $CFG: $HERE/dist/$CFG/SemCraft2.dll"
done
