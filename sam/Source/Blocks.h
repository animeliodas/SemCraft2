/* SemCraft 2 - Minecraft's blocks are solid in Serious Sam.
 *
 * Each solid block Minecraft reports near the player becomes an invisible, colliding ModelHolder2 (the editor's
 * collision cube, inactive so only the editor draws it). Sam's player, monsters and bullets then stop at built walls
 * and can stand on built floors. Serious Engine collides models as spheres, so block edges are rounded. */
#ifndef SEMCRAFT2_BLOCKS_H
#define SEMCRAFT2_BLOCKS_H

#include "StdH.h"
#include <string>

namespace blocks {

// From the link: blocks now solid / gone, Sam coordinates of their minimum corners (x, y, z triples).
void OnBlocks(const double *adSet, INDEX ctSet, const double *adClear, INDEX ctClear);
// Every simulation tick: create and remove a budget of block entities.
void Step(void);
// The world was loaded (or the game stopped): forget our entities, remove any a savegame brought back.
void Reset(BOOL bPurgeWorld);
CTString Status(void);

}; // namespace

#endif
