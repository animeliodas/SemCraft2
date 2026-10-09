@echo off
rem SemCraft 2 - Serious Sam: The Second Encounter with Minecraft inside.
rem Start Minecraft first: CurseForge, instance "SemCraft" (it opens the world semcraft2 by itself).
set "GAME=%~dp0Serious Sam Classic The Second Encounter"
if not exist "%GAME%\Bin\SeriousSam_Custom.exe" set "GAME=%~dp0..\Serious Sam Classic The Second Encounter"
start "" /D "%GAME%\Bin" "%GAME%\Bin\SeriousSam_Custom.exe"
