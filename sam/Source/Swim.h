/* SemCraft 2 - Minecraft's water and lava move Serious Sam's player.
 *
 * Minecraft says how deep its water (or lava) is where Sam's player stands, whether his eyes are under and which way
 * it flows. Then he swims as Minecraft's player would (LivingEntity.travelInWater / travelInLava, blocks per tick =
 * metres per tick): input accelerates him slowly, the fluid drags, jump swims up, crouch dives, he sinks slowly
 * otherwise and the current carries him. Walking on the bottom is Sam's own walk, slowed, with the current added.
 * Sam's own water (a swimmable sector) stays Sam's. */
#ifndef SEMCRAFT2_SWIM_H
#define SEMCRAFT2_SWIM_H

#include "StdH.h"

namespace swim {

// From Minecraft: fluid heights at the feet (blocks, 0 = none), eyes under, the flow (unit vector or zero).
void OnFluid(FLOAT fWater, FLOAT fLava, BOOL bEyes, const FLOAT3D &vFlow);
// Is the player in Minecraft's water or lava (fresh news)?
BOOL InFluid(void);
// Every player action: the input is taken over while swimming.
void OnAction(CPlayerAction &pa, BOOL bFresh);
// Every simulation tick.
void Step(CEntity *penPlayer);
void Reset(void);
CTString Status(void);

}; // namespace

#endif
