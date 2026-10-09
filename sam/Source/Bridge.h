/* SemCraft 2 - the Serious Sam half of the bridge: camera, level, player state, input and damage. */
#ifndef SEMCRAFT2_BRIDGE_H
#define SEMCRAFT2_BRIDGE_H

#include "StdH.h"

namespace bridge {

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
// Where the player's crosshair hits Sam's world (for test hooks); FALSE if nothing.
BOOL AimPoint(FLOAT3D &vHit);
CTString Status(void);

}; // namespace

#endif
