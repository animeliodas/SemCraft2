/* SemCraft 2 - Minecraft's elytra on Serious Sam's player.
 *
 * Minecraft says whether its player wears an elytra (and holds a firework). Then Sam's player can glide: jump again in
 * the air and the wings open; every tick his velocity goes through Minecraft's fall-flying physics (the same numbers,
 * blocks per tick = metres per tick) along where he looks, with Sam's gravity off. A firework (right mouse button while
 * gliding, holding one) boosts like Minecraft's. Landing, water or taking the elytra off folds the wings. */
#ifndef SEMCRAFT2_GLIDE_H
#define SEMCRAFT2_GLIDE_H

#include "StdH.h"

namespace glide {

// From Minecraft: is an elytra worn, is a firework in hand.
void OnGear(BOOL bElytra, BOOL bFirework);
// Every player action: a jump pressed in the air may open the wings; while gliding, walking is off.
void OnAction(CPlayerAction &pa, BOOL bFresh);
// Every simulation tick.
void Step(CEntity *penPlayer);
// A firework: about a second of thrust (only while gliding).
BOOL Boost(void);
BOOL Gliding(void);
BOOL FireworkInHand(void);
// test hook: hold jump for this many player actions
extern INDEX sc_iTestJump;
// dev: log this many glide samples (every 5 ticks)
extern INDEX sc_iGlideLog;
void Reset(void);
CTString Status(void);

}; // namespace

#endif
