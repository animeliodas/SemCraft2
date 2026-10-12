/* SemCraft 2 - the Serious Sam half of the bridge: camera, level, player state, input and damage. */
#ifndef SEMCRAFT2_BRIDGE_H
#define SEMCRAFT2_BRIDGE_H

#include "StdH.h"

namespace bridge {

// The local player's entity, or NULL (none yet: while a level starts the player list can hold an empty slot).
CEntity *LocalPlayer(void);

void Startup(void);
void Shutdown(void);

// Rendering: the main game view was just rendered with this (prepared) projection: send the camera.
void OnView(CPerspectiveProjection3D &ppr, CEntity *penViewer);
// Every rendered frame: link messages, input.
void OnFrame(void);
// Every simulation tick: apply Minecraft's hits, report the player's state.
void OnStep(void);
// A world was loaded.
void OnWorldLoad(CWorld *pwo, const CTFileName &fnmWorld);
void OnGameStop(void);
// Player actions on their way to the player entity (build mode keeps Sam's weapon quiet).
// bFresh: a new action (not one resent for reliability).
void OnPlayerAction(CPlayerAction &pa, BOOL bFresh);

// Test hooks: hold fire for this many player actions; select weapon number N once.
extern INDEX sc_iTestFire;
extern INDEX sc_iTestSelect;
extern INDEX sc_iTestWalk;
// Test hook: turn by sc_fTestTurnH/P degrees per action, for sc_iTestTurn actions.
extern INDEX sc_iTestTurn;
extern FLOAT sc_fTestTurnH, sc_fTestTurnP;
// Build mode on/off.
void SetBuildMode(BOOL bOn);
BOOL BuildMode(void);
// Raw message to Minecraft (console/test hooks).
void SendRaw(const char *strJson);
// A chat line for Minecraft ("/..." = a command there). FALSE if Minecraft isn't linked.
BOOL OnChat(const char *strText);
// A Minecraft screen (the inventory) is open over Sam: where its cursor is (0..1 of the view).
BOOL ScreenCursor(FLOAT &fX, FLOAT &fY);
// cursor speed multiplier (persistent console var)
extern FLOAT sc_fCursorSpeed;
// dev: subsystems switched off (see Bridge.cpp)
extern INDEX sc_iDisable;
// Dev/test: open/close the inventory, click at a point of it (0..1), type text into it.
void DevScreen(BOOL bOpen);
void DevClick(FLOAT fX, FLOAT fY, INDEX iButton);
void DevType(const char *strText);
// Where the player's crosshair hits Sam's world (for test hooks); FALSE if nothing.
BOOL AimPoint(FLOAT3D &vHit);
CTString Status(void);

}; // namespace

#endif
