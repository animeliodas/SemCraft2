/* SemCraft 2 - draws Minecraft's frame into Serious Sam's.
 *
 * The world layer (colour + depth) is drawn right after Sam renders its world (OnRenderView), as a full-screen
 * quad whose fragment shader writes gl_FragDepth on Sam's depth scale, so Sam's own depth test hides Minecraft
 * behind Sam's walls and Minecraft hides Sam's things behind its blocks. Minecraft's picture is re-projected from
 * the camera it was rendered for onto Sam's current camera (rotation exactly, position through depth).
 * The overlay layer (hand + hotbar) is drawn over the finished game view (OnPostDraw).
 * Raw OpenGL with every touched state saved and restored, so Sam's own GL state cache stays valid.
 */
#ifndef SEMCRAFT2_COMPOSITOR_H
#define SEMCRAFT2_COMPOSITOR_H

#include "StdH.h"

namespace compositor {

// The camera a Minecraft frame was rendered for (Sam coordinates), as echoed in the frame.
struct Pose {
  FLOAT3D vPos;
  ANGLE3D aAngles;
};

// After Sam rendered the main game view with this (prepared) projection.
void DrawWorld(CPerspectiveProjection3D &ppr);
// Over the finished game view.
void DrawOverlay(void);
// Release GL objects and the shared memory (GL objects only if bWithGL, i.e. a context is current).
void Shutdown(BOOL bWithGL);
// Status line for the console.
CTString Status(void);
// Debug view: 0 composite, 1 Minecraft only (no depth test), 2 off.
extern INDEX sc_iCompositeMode;
// Depth bias (Sam window-depth units) subtracted from Minecraft's depth.
extern FLOAT sc_fDepthBias;
// How much Minecraft takes the light of Sam's picture around it (0 = Minecraft's own noon light).
extern FLOAT sc_fLightMatch;

}; // namespace

#endif
