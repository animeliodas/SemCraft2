@echo off
rem SemCraft 2 - Serious Sam: The First Encounter with Minecraft inside.
rem Start Minecraft first: CurseForge, instance "SemCraft" (it opens the world semcraft2 by itself).
set "GAME=%~dp0Serious Sam Classic The First Encounter"
if not exist "%GAME%\Bin\SeriousSam_Custom.exe" set "GAME=%~dp0..\Serious Sam Classic The First Encounter"
start "" /D "%GAME%\Bin" "%GAME%\Bin\SeriousSam_Custom.exe"
