/* SemCraft 2 - the raw OpenGL half of the compositor.
 * Compiled without the engine headers (the engine has its own GL typedefs), so only plain types cross here.
 * Every GL state touched is saved and restored, so Sam's own GL state cache stays valid. */
#ifndef SEMCRAFT2_GLDRAW_H
#define SEMCRAFT2_GLDRAW_H

namespace gldraw {

typedef void (*LogFunc)(const char *strMessage);

struct WorldParams {
  float afViewport[4]; // Sam's viewport x, y, w, h (pixels)
  float afTanCur[4];   // Sam's frustum tangents: left, right, bottom, top
  float afTanMc[2];    // Minecraft frame's half tangents x, y
  float afRot[9];      // row-major: Sam view space -> Minecraft frame view space
  float afT[3];        // Minecraft frame camera in Sam view space
  float afMcPlanes[2]; // Minecraft near, far (reversed Z)
  float afSam[4];      // Sam near, far, depth range near, far
  float fBias;         // subtracted from the window depth
  float fMode;         // 1: no depth test (Minecraft only)
  float fLight;        // 0..1: how much Minecraft takes the light of Sam's picture around it
};

// Make sure our GL objects exist in the current context. FALSE if GL 2.0 isn't there (or no context).
bool Ready(LogFunc pLog);
// TRUE once the textures hold a frame (reset when Sam recreates its GL context).
bool HasFrame(void);
void ForgetFrame(void);
// Begin/end a pass: saves and restores Sam's GL state.
void Begin(void);
void End(void);
// Inside a pass: upload a frame (colour RGBA8, depth float32, overlay RGBA8; rows bottom-up).
void Upload(int w, int h, const void *pColor, const void *pDepth, const void *pOverlay);
// Inside a pass: the current viewport.
void GetViewport(int aiViewport[4]);
// Inside a pass: draw the world layer with depth.
void DrawWorld(const WorldParams &p);
// Inside a pass: draw the overlay layer over the given viewport.
void DrawOverlay(const int aiViewport[4]);
// Is the context we made our objects in current?
bool ContextCurrent(void);
// Delete our objects if their context is current; forget them either way.
void Release(void);
// Read the current draw buffer (RGBA8, rows bottom-up) into pOut (w*h*4 bytes).
void ReadPixels(int x, int y, int w, int h, void *pOut);
// Renderer name (for the log).
const char *Renderer(void);

}; // namespace

#endif
