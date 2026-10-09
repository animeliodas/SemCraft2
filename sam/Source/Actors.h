/* SemCraft 2 - Serious Sam's monsters in Minecraft.
 *
 * Living monsters near the player are listed for Minecraft ten times a second; Minecraft keeps an invisible stand-in
 * ("proxy") for each, which its hostile mobs hunt and its weapons hit. The hits come back here and are applied to
 * the real monster. */
#ifndef SEMCRAFT2_ACTORS_H
#define SEMCRAFT2_ACTORS_H

#include "StdH.h"
#include <string>

namespace actors {

// Every simulation tick: apply Minecraft's hits, and every other tick send the monsters near the player.
void Step(CEntity *penPlayer, void (*pfnSend)(const std::string &));
// Minecraft hit monster ulId for fDamage Sam hit points (applied on the next tick).
void OnHit(ULONG ulId, FLOAT fDamage, BOOL bExplosion);
void Reset(void);
CTString Status(void);

}; // namespace

#endif
