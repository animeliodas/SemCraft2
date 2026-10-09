/* SemCraft 2 - Serious Sam's weapons hurt Minecraft's mobs.
 *
 * Sam's bullets are entities that live for one tick inside the weapon code, so hitscan shots are counted from the
 * player's ammo going down (CPlayerWeapons properties, read by property ID). Rockets, grenades, laser bolts and
 * cannonballs are followed as entities until they are gone, then their explosion is sent where they were last.
 * Minecraft traces the shots itself (Sam's level is blocks there too, so walls stop them). */
#ifndef SEMCRAFT2_WEAPONS_H
#define SEMCRAFT2_WEAPONS_H

#include "StdH.h"
#include <string>

namespace weapons {

// Every simulation tick (server side), with the local player. Sends "shot"/"boom" messages through pfnSend.
void Step(CEntity *penPlayer, void (*pfnSend)(const std::string &));
// Minecraft caught projectile ulId in one of its mobs (and exploded it there).
void OnProjectileHit(ULONG ulId);
// The level changed or the game stopped: forget tracked projectiles and ammo counts.
void Reset(void);
// Diagnostics.
CTString Status(void);

}; // namespace

#endif
