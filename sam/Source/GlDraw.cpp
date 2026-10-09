/* SemCraft 2 - raw OpenGL for the compositor. See GlDraw.h. No engine headers here. */
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>
#include <stdio.h>

#include "GlDraw.h"

namespace gldraw {

// ---------------------------------------------------------------------------------------------------------
// OpenGL 2.0 entry points (opengl32.lib only exports 1.1)
// ---------------------------------------------------------------------------------------------------------
typedef char GLchar;
#define GL_FRAGMENT_SHADER      0x8B30
#define GL_VERTEX_SHADER        0x8B31
#define GL_COMPILE_STATUS       0x8B81
#define GL_LINK_STATUS          0x8B82
#define GL_TEXTURE0             0x84C0
#define GL_R32F                 0x822E
#define GL_RED_                 0x1903
#define GL_CLAMP_TO_EDGE_       0x812F
#define GL_PIXEL_UNPACK_BUFFER  0x88EC
#define GL_PIXEL_UNPACK_BUFFER_BINDING 0x88EF
#define GL_CURRENT_PROGRAM      0x8B8D
#define GL_ACTIVE_TEXTURE       0x84E0

typedef GLuint (APIENTRY *PFN_CreateShader)(GLenum);
typedef void (APIENTRY *PFN_ShaderSource)(GLuint, GLsizei, const GLchar **, const GLint *);
typedef void (APIENTRY *PFN_CompileShader)(GLuint);
typedef void (APIENTRY *PFN_GetShaderiv)(GLuint, GLenum, GLint *);
typedef void (APIENTRY *PFN_GetShaderInfoLog)(GLuint, GLsizei, GLsizei *, GLchar *);
typedef GLuint (APIENTRY *PFN_CreateProgram)(void);
typedef void (APIENTRY *PFN_AttachShader)(GLuint, GLuint);
typedef void (APIENTRY *PFN_LinkProgram)(GLuint);
typedef void (APIENTRY *PFN_GetProgramiv)(GLuint, GLenum, GLint *);
typedef void (APIENTRY *PFN_GetProgramInfoLog)(GLuint, GLsizei, GLsizei *, GLchar *);
typedef void (APIENTRY *PFN_UseProgram)(GLuint);
typedef GLint (APIENTRY *PFN_GetUniformLocation)(GLuint, const GLchar *);
typedef void (APIENTRY *PFN_Uniform1i)(GLint, GLint);
typedef void (APIENTRY *PFN_Uniform1f)(GLint, GLfloat);
typedef void (APIENTRY *PFN_Uniform2f)(GLint, GLfloat, GLfloat);
typedef void (APIENTRY *PFN_Uniform3f)(GLint, GLfloat, GLfloat, GLfloat);
typedef void (APIENTRY *PFN_Uniform4f)(GLint, GLfloat, GLfloat, GLfloat, GLfloat);
typedef void (APIENTRY *PFN_UniformMatrix3fv)(GLint, GLsizei, GLboolean, const GLfloat *);
typedef void (APIENTRY *PFN_ActiveTexture)(GLenum);
typedef void (APIENTRY *PFN_BindBuffer)(GLenum, GLuint);

static PFN_CreateShader       p_glCreateShader;
static PFN_ShaderSource       p_glShaderSource;
static PFN_CompileShader      p_glCompileShader;
static PFN_GetShaderiv        p_glGetShaderiv;
static PFN_GetShaderInfoLog   p_glGetShaderInfoLog;
static PFN_CreateProgram      p_glCreateProgram;
static PFN_AttachShader       p_glAttachShader;
static PFN_LinkProgram        p_glLinkProgram;
static PFN_GetProgramiv       p_glGetProgramiv;
static PFN_GetProgramInfoLog  p_glGetProgramInfoLog;
static PFN_UseProgram         p_glUseProgram;
static PFN_GetUniformLocation p_glGetUniformLocation;
static PFN_Uniform1i          p_glUniform1i;
static PFN_Uniform1f          p_glUniform1f;
static PFN_Uniform2f          p_glUniform2f;
static PFN_Uniform3f          p_glUniform3f;
static PFN_Uniform4f          p_glUniform4f;
static PFN_UniformMatrix3fv   p_glUniformMatrix3fv;
static PFN_ActiveTexture      p_glActiveTexture;
static PFN_BindBuffer         p_glBindBuffer;

template<class T> static bool Load(T &pfn, const char *strName) {
  pfn = (T)wglGetProcAddress(strName);
  return pfn != NULL;
};

// ---------------------------------------------------------------------------------------------------------
// Shaders
// ---------------------------------------------------------------------------------------------------------
static const char *_strVertex =
  "#version 120\n"
  "void main() { gl_Position = vec4(gl_Vertex.xy, 0.0, 1.0); }\n";

// World layer: re-project Minecraft's picture onto Sam's current camera and write Sam-scale depth.
static const char *_strWorld =
  "#version 120\n"
  "uniform sampler2D uColor;\n"
  "uniform sampler2D uDepth;\n"
  "uniform vec4 uVp;\n"
  "uniform vec4 uTanCur;\n"
  "uniform vec2 uTanMc;\n"
  "uniform mat3 uRot;\n"
  "uniform vec3 uT;\n"
  "uniform vec2 uMcPlanes;\n"
  "uniform vec4 uSam;\n"
  "uniform float uBias;\n"
  "uniform float uMode;\n"
  "uniform sampler2D uSamLight;\n" // Sam's picture before Minecraft, mipmapped: its blur is the light around
  "uniform float uLight;\n"         // how much Minecraft takes Sam's light (0..1)
  "void main() {\n"
  "  vec2 uv = (gl_FragCoord.xy - uVp.xy) / uVp.zw;\n"
  "  vec3 rc =vec3(mix(uTanCur.x, uTanCur.y, uv.x), mix(uTanCur.z, uTanCur.w, uv.y), -1.0);\n"
  "  vec3 rm = uRot * rc;\n"
  "  if (rm.z > -1e-4) discard;\n"
  "  vec2 sm = rm.xy / -rm.z;\n"
  "  vec2 uvm = sm / uTanMc * 0.5 + 0.5;\n"
  "  if (uvm.x < 0.0 || uvm.y < 0.0 || uvm.x > 1.0 || uvm.y > 1.0) discard;\n"
  "  float d = texture2D(uDepth, uvm).r;\n"
  "  if (d <= 0.0) discard;\n"
  "  vec4 c = texture2D(uColor, uvm);\n"
  "  if (c.a < 0.004) discard;\n"
  "  float n = uMcPlanes.x, f = uMcPlanes.y;\n"
  "  float zm = n * f / (n + d * (f - n));\n"
  "  vec3 pc = transpose(uRot) * vec3(sm * zm, -zm) + uT;\n"
  "  float zc = max(-pc.z, uSam.x);\n"
  "  float sn = uSam.x, sf = uSam.y;\n"
  "  float ndc = (sf + sn) / (sf - sn) - 2.0 * sf * sn / ((sf - sn) * zc);\n"
  "  float win = mix(uSam.z, uSam.w, ndc * 0.5 + 0.5) - uBias;\n"
  "  gl_FragDepth = uMode > 0.5 ? 0.0 : clamp(win, 0.0, 1.0);\n"
  "  vec3 L = texture2D(uSamLight, uv, 6.0).rgb;\n"
  "  float lum = dot(L, vec3(0.2126, 0.7152, 0.0722));\n"
  "  float gain = clamp(lum / 0.40, 0.22, 1.25);\n"
  "  vec3 tint = clamp(mix(vec3(1.0), L / max(lum, 1e-3), 0.45), 0.6, 1.5);\n"
  "  c.rgb = mix(c.rgb, c.rgb * gain * tint, uLight);\n"
  "  gl_FragColor = c;\n"
  "}\n";

// Overlay layer: Minecraft's hand and HUD, premultiplied, straight on top.
static const char *_strOverlay =
  "#version 120\n"
  "uniform sampler2D uColor;\n"
  "uniform vec4 uVp;\n"
  "void main() {\n"
  "  vec2 uv = (gl_FragCoord.xy - uVp.xy) / uVp.zw;\n"
  "  vec4 c = texture2D(uColor, uv);\n"
  "  if (c.a < 0.004) discard;\n"
  "  gl_FragColor = c;\n"
  "}\n";

// ---------------------------------------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------------------------------------
static HGLRC _hglrc = NULL;
static bool _bFailed = false;
static GLuint _progWorld = 0, _progOverlay = 0;
static GLuint _atex[3] = { 0, 0, 0 }; // colour, depth, overlay
static int _texW = 0, _texH = 0;
static bool _bHaveFrame = false;
static LogFunc _pLog = NULL;
static char _strRenderer[128] = "";

static GLint _uwColor, _uwDepth, _uwVp, _uwTanCur, _uwTanMc, _uwRot, _uwT, _uwMcPlanes, _uwSam, _uwBias, _uwMode, _uwSamLight, _uwLight;
// Sam's picture (for its light), mipmapped
static GLuint _texSam = 0;
static int _samW = 0, _samH = 0;
typedef void (APIENTRY *PFN_GenerateMipmap)(GLenum);
static PFN_GenerateMipmap p_glGenerateMipmap = NULL;
static GLint _uoColor, _uoVp;

// saved state of the current pass
static GLint _iSavedProgram, _iSavedActiveTex, _iSavedUnpackPBO;

static void Log(const char *strFormat, const char *strArg) {
  if (_pLog == NULL) return;
  char str[2300];
  _snprintf(str, sizeof(str), strFormat, strArg);
  str[sizeof(str) - 1] = 0;
  _pLog(str);
};

static GLuint Compile(GLenum eType, const char *strSource) {
  GLuint sh = p_glCreateShader(eType);
  p_glShaderSource(sh, 1, &strSource, NULL);
  p_glCompileShader(sh);

  GLint iOK = 0;
  p_glGetShaderiv(sh, GL_COMPILE_STATUS, &iOK);
  if (!iOK) {
    char strLog[2048] = { 0 };
    p_glGetShaderInfoLog(sh, sizeof(strLog) - 1, NULL, strLog);
    Log("shader compile failed:\n%s", strLog);
    return 0;
  }
  return sh;
};

static GLuint Link(const char *strFragment) {
  GLuint vs = Compile(GL_VERTEX_SHADER, _strVertex);
  GLuint fs = Compile(GL_FRAGMENT_SHADER, strFragment);
  if (vs == 0 || fs == 0) return 0;

  GLuint prog = p_glCreateProgram();
  p_glAttachShader(prog, vs);
  p_glAttachShader(prog, fs);
  p_glLinkProgram(prog);

  GLint iOK = 0;
  p_glGetProgramiv(prog, GL_LINK_STATUS, &iOK);
  if (!iOK) {
    char strLog[2048] = { 0 };
    p_glGetProgramInfoLog(prog, sizeof(strLog) - 1, NULL, strLog);
    Log("shader link failed:\n%s", strLog);
    return 0;
  }
  return prog;
};

bool Ready(LogFunc pLog) {
  _pLog = pLog;
  HGLRC hglrc = wglGetCurrentContext();
  if (hglrc == NULL) return false;

  // Sam recreated its context (video mode change): our old objects died with it
  if (hglrc != _hglrc) {
    _hglrc = hglrc;
    _progWorld = _progOverlay = 0;
    _atex[0] = _atex[1] = _atex[2] = 0;
    _texW = _texH = 0;
    _texSam = 0;
    _samW = _samH = 0;
    _bHaveFrame = false;
    _bFailed = false;
  }

  if (_progWorld != 0) return true;
  if (_bFailed) return false;
  _bFailed = true; // until proven otherwise

  const bool bOK = Load(p_glCreateShader, "glCreateShader") && Load(p_glShaderSource, "glShaderSource")
    && Load(p_glCompileShader, "glCompileShader") && Load(p_glGetShaderiv, "glGetShaderiv")
    && Load(p_glGetShaderInfoLog, "glGetShaderInfoLog") && Load(p_glCreateProgram, "glCreateProgram")
    && Load(p_glAttachShader, "glAttachShader") && Load(p_glLinkProgram, "glLinkProgram")
    && Load(p_glGetProgramiv, "glGetProgramiv") && Load(p_glGetProgramInfoLog, "glGetProgramInfoLog")
    && Load(p_glUseProgram, "glUseProgram") && Load(p_glGetUniformLocation, "glGetUniformLocation")
    && Load(p_glUniform1i, "glUniform1i") && Load(p_glUniform1f, "glUniform1f")
    && Load(p_glUniform2f, "glUniform2f") && Load(p_glUniform3f, "glUniform3f")
    && Load(p_glUniform4f, "glUniform4f") && Load(p_glUniformMatrix3fv, "glUniformMatrix3fv")
    && Load(p_glActiveTexture, "glActiveTexture") && Load(p_glBindBuffer, "glBindBuffer");

  if (!bOK) {
    Log("%sOpenGL 2.0 isn't available: Minecraft can't be drawn (is Sam in Direct3D mode?)", "");
    return false;
  }

  _progWorld = Link(_strWorld);
  _progOverlay = Link(_strOverlay);
  if (_progWorld == 0 || _progOverlay == 0) return false;

  _uwColor = p_glGetUniformLocation(_progWorld, "uColor");
  _uwDepth = p_glGetUniformLocation(_progWorld, "uDepth");
  _uwVp = p_glGetUniformLocation(_progWorld, "uVp");
  _uwTanCur = p_glGetUniformLocation(_progWorld, "uTanCur");
  _uwTanMc = p_glGetUniformLocation(_progWorld, "uTanMc");
  _uwRot = p_glGetUniformLocation(_progWorld, "uRot");
  _uwT = p_glGetUniformLocation(_progWorld, "uT");
  _uwMcPlanes = p_glGetUniformLocation(_progWorld, "uMcPlanes");
  _uwSam = p_glGetUniformLocation(_progWorld, "uSam");
  _uwBias = p_glGetUniformLocation(_progWorld, "uBias");
  _uwMode = p_glGetUniformLocation(_progWorld, "uMode");
  _uwSamLight = p_glGetUniformLocation(_progWorld, "uSamLight");
  _uwLight = p_glGetUniformLocation(_progWorld, "uLight");
  Load(p_glGenerateMipmap, "glGenerateMipmap");
  _uoColor = p_glGetUniformLocation(_progOverlay, "uColor");
  _uoVp = p_glGetUniformLocation(_progOverlay, "uVp");

  const char *strRenderer = (const char *)glGetString(GL_RENDERER);
  _snprintf(_strRenderer, sizeof(_strRenderer), "%s", strRenderer != NULL ? strRenderer : "?");
  _strRenderer[sizeof(_strRenderer) - 1] = 0;
  Log("compositor ready (%s)", _strRenderer);

  _bFailed = false;
  return true;
};

bool HasFrame(void) { return _bHaveFrame; };
void ForgetFrame(void) { _bHaveFrame = false; };
bool ContextCurrent(void) { return _hglrc != NULL && wglGetCurrentContext() == _hglrc; };
const char *Renderer(void) { return _strRenderer; };

void Begin(void) {
  glGetIntegerv(GL_CURRENT_PROGRAM, &_iSavedProgram);
  glGetIntegerv(GL_ACTIVE_TEXTURE, &_iSavedActiveTex);
  glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &_iSavedUnpackPBO);
  glPushAttrib(GL_ALL_ATTRIB_BITS);
  glPushClientAttrib(GL_CLIENT_ALL_ATTRIB_BITS);
  glMatrixMode(GL_TEXTURE);    glPushMatrix(); glLoadIdentity();
  glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
  glMatrixMode(GL_MODELVIEW);  glPushMatrix(); glLoadIdentity();
  if (_iSavedUnpackPBO != 0) p_glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
};

void End(void) {
  p_glUseProgram((GLuint)_iSavedProgram);
  glMatrixMode(GL_TEXTURE);    glPopMatrix();
  glMatrixMode(GL_PROJECTION); glPopMatrix();
  glMatrixMode(GL_MODELVIEW);  glPopMatrix();
  glPopClientAttrib();
  glPopAttrib();
  p_glActiveTexture((GLenum)_iSavedActiveTex);
  if (_iSavedUnpackPBO != 0) p_glBindBuffer(GL_PIXEL_UNPACK_BUFFER, (GLuint)_iSavedUnpackPBO);
};

static void EnsureTextures(int w, int h) {
  if (_atex[0] != 0 && _texW == w && _texH == h) return;
  if (_atex[0] == 0) glGenTextures(3, _atex);

  for (int i = 0; i < 3; i++) {
    p_glActiveTexture(GL_TEXTURE0 + i);
    glBindTexture(GL_TEXTURE_2D, _atex[i]);
    const GLint iFilter = (i == 1) ? GL_NEAREST : GL_LINEAR;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, iFilter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, iFilter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE_);

    if (i == 1) {
      glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, w, h, 0, GL_RED_, GL_FLOAT, NULL);
    } else {
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    }
  }
  _texW = w;
  _texH = h;
};

void Upload(int w, int h, const void *pColor, const void *pDepth, const void *pOverlay) {
  EnsureTextures(w, h);

  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
  glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
  glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
  glPixelStorei(GL_UNPACK_SWAP_BYTES, GL_FALSE);
  glPixelStorei(GL_UNPACK_LSB_FIRST, GL_FALSE);

  p_glActiveTexture(GL_TEXTURE0 + 0);
  glBindTexture(GL_TEXTURE_2D, _atex[0]);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pColor);
  p_glActiveTexture(GL_TEXTURE0 + 1);
  glBindTexture(GL_TEXTURE_2D, _atex[1]);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RED_, GL_FLOAT, pDepth);
  p_glActiveTexture(GL_TEXTURE0 + 2);
  glBindTexture(GL_TEXTURE_2D, _atex[2]);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pOverlay);
  _bHaveFrame = true;
};

void GetViewport(int aiViewport[4]) {
  GLint ai[4];
  glGetIntegerv(GL_VIEWPORT, ai);
  for (int i = 0; i < 4; i++) aiViewport[i] = ai[i];
};

static void FullScreenQuad(void) {
  glBegin(GL_QUADS);
  glVertex2f(-1.0f, -1.0f);
  glVertex2f( 1.0f, -1.0f);
  glVertex2f( 1.0f,  1.0f);
  glVertex2f(-1.0f,  1.0f);
  glEnd();
};

static void CommonState(void) {
  glDisable(GL_CULL_FACE);
  glDisable(GL_ALPHA_TEST);
  glDisable(GL_FOG);
  glDisable(GL_LIGHTING);
  glDisable(GL_SCISSOR_TEST);
  glDisable(GL_STENCIL_TEST);
  glDisable(GL_CLIP_PLANE0);
  glDisable(GL_TEXTURE_2D);
  glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
};

// Copy what Sam drew in the viewport into a mipmapped texture: its blurred levels are Sam's light around a pixel.
static void CaptureSamLight(const float afViewport[4]) {
  const int x = (int)afViewport[0], y = (int)afViewport[1], w = (int)afViewport[2], h = (int)afViewport[3];
  if (w <= 0 || h <= 0 || p_glGenerateMipmap == NULL) return;

  p_glActiveTexture(GL_TEXTURE0 + 3);
  if (_texSam == 0) glGenTextures(1, &_texSam);
  glBindTexture(GL_TEXTURE_2D, _texSam);
  if (w != _samW || h != _samH) {
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE_);
    _samW = w;
    _samH = h;
  }
  glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, x, y, w, h);
  p_glGenerateMipmap(GL_TEXTURE_2D);
};

void DrawWorld(const WorldParams &p) {
  if (!_bHaveFrame || _progWorld == 0) return;

  const bool bLight = p.fLight > 0.0f && p_glGenerateMipmap != NULL;
  if (bLight) CaptureSamLight(p.afViewport);

  p_glUseProgram(_progWorld);
  p_glUniform1i(_uwColor, 0);
  p_glUniform1i(_uwDepth, 1);
  p_glUniform4f(_uwVp, p.afViewport[0], p.afViewport[1], p.afViewport[2], p.afViewport[3]);
  p_glUniform4f(_uwTanCur, p.afTanCur[0], p.afTanCur[1], p.afTanCur[2], p.afTanCur[3]);
  p_glUniform2f(_uwTanMc, p.afTanMc[0], p.afTanMc[1]);
  p_glUniformMatrix3fv(_uwRot, 1, GL_TRUE, p.afRot);
  p_glUniform3f(_uwT, p.afT[0], p.afT[1], p.afT[2]);
  p_glUniform2f(_uwMcPlanes, p.afMcPlanes[0], p.afMcPlanes[1]);
  p_glUniform4f(_uwSam, p.afSam[0], p.afSam[1], p.afSam[2], p.afSam[3]);
  p_glUniform1f(_uwBias, p.fBias);
  p_glUniform1f(_uwMode, p.fMode);
  p_glUniform1i(_uwSamLight, 3);
  p_glUniform1f(_uwLight, bLight ? p.fLight : 0.0f);

  p_glActiveTexture(GL_TEXTURE0 + 0);
  glBindTexture(GL_TEXTURE_2D, _atex[0]);
  p_glActiveTexture(GL_TEXTURE0 + 1);
  glBindTexture(GL_TEXTURE_2D, _atex[1]);
  p_glActiveTexture(GL_TEXTURE0 + 3);
  glBindTexture(GL_TEXTURE_2D, _texSam);

  CommonState();
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LEQUAL);
  glDepthMask(GL_TRUE);
  glDepthRange(0.0, 1.0);

  FullScreenQuad();
};

void DrawOverlay(const int aiViewport[4]) {
  if (!_bHaveFrame || _progOverlay == 0) return;

  glViewport(aiViewport[0], aiViewport[1], aiViewport[2], aiViewport[3]);
  p_glUseProgram(_progOverlay);
  p_glUniform1i(_uoColor, 0);
  p_glUniform4f(_uoVp, (GLfloat)aiViewport[0], (GLfloat)aiViewport[1], (GLfloat)aiViewport[2], (GLfloat)aiViewport[3]);

  p_glActiveTexture(GL_TEXTURE0 + 0);
  glBindTexture(GL_TEXTURE_2D, _atex[2]);

  CommonState();
  glDisable(GL_DEPTH_TEST);
  glDepthMask(GL_FALSE);

  FullScreenQuad();
};

void ReadPixels(int x, int y, int w, int h, void *pOut) {
  glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);
  glPixelStorei(GL_PACK_ALIGNMENT, 4);
  glPixelStorei(GL_PACK_ROW_LENGTH, 0);
  glPixelStorei(GL_PACK_SKIP_ROWS, 0);
  glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
  glReadPixels(x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pOut);
  glPopClientAttrib();
};

void Release(void) {
  if (ContextCurrent() && _atex[0] != 0) glDeleteTextures(3, _atex);
  if (ContextCurrent() && _texSam != 0) glDeleteTextures(1, &_texSam);
  _texSam = 0;
  _atex[0] = _atex[1] = _atex[2] = 0;
  _progWorld = _progOverlay = 0;
  _hglrc = NULL;
  _bHaveFrame = false;
};

}; // namespace
