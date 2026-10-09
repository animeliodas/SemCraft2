/* SemCraft 2 - writes the loaded level's brush triangles to a .tri file for the Minecraft mod (docs/CONTRACT.md). */
#ifndef SEMCRAFT2_LEVELEXPORT_H
#define SEMCRAFT2_LEVELEXPORT_H

#include "StdH.h"
#include <string>

namespace levelx {

struct Exported {
  std::string strFile;  // full path of the .tri file
  std::string strHash;  // content hash (hex)
  std::string strName;  // world file name
  INDEX ctTris;
  BOOL bValid;
};

// Export the world's brushes; returns what was written (bValid = FALSE on failure).
Exported Export(CWorld *pwo, const CTFileName &fnmWorld);

}; // namespace

#endif
