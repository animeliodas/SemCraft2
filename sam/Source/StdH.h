/* SemCraft 2 plugin precompiled header */
#ifndef SEMCRAFT2_STDH_H
#define SEMCRAFT2_STDH_H

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif

// Link the library for direct use (same as Extensions/Sample)
#include <Core/Core.h>
// (no ExtPackets.h: its packets carry C++ objects across the DLL boundary)

#include <Engine/Brushes/Brush.h>
#include <Engine/Brushes/BrushArchive.h>
#include <Engine/Math/Projection.h>
#include <Engine/Math/Geometry.h>
#include <Engine/Graphics/DrawPort.h>

#ifdef SEMCRAFT_GAME_TSE
  // (no Terrain.h: its templates do not compile with a modern compiler)
#endif

#endif
