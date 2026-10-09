/* SemCraft 2 - Minecraft's frame composited into Serious Sam's. See Compositor.h and docs/CONTRACT.md.
 * This half reads the shared memory and does the camera maths; GlDraw.cpp does the OpenGL. */
#include "StdH.h"
#include "Compositor.h"
#include "GlDraw.h"

#include <emmintrin.h>

namespace compositor {

INDEX sc_iCompositeMode = 0;
FLOAT sc_fDepthBias = 0.0f;
FLOAT sc_fLightMatch = 0.85f;

static const ULONG MAGIC = 0x32464353; // "SCF2"
static HANDLE _hMap = NULL;
static const UBYTE *_pubHeader = NULL;
static const UBYTE *_apubSlot[3] = { NULL, NULL, NULL };     // slot data
static const UBYTE *_apubSlotView[3] = { NULL, NULL, NULL }; // view bases (for unmapping)
static ULONG64 _ullStride = 0;
static INDEX _ctMaxW = 0, _ctMaxH = 0;
static DWORD _tmNextOpen = 0;
static ULONG _ulLastPublish = 0xFFFFFFFF;

// the uploaded frame
static INDEX _frmW = 0, _frmH = 0;
static FLOAT _frmNear = 0.05f, _frmFar = 1000.0f, _frmVFov = 70.0f, _frmAspect = 1.0f;
static ULONG _ulFrmFlags = 0;
static Pose _frmPose;
static LONGLONG _llFrmPublishNs = 0;

// stats
static ULONG _ctUploads = 0, _ctTorn = 0, _ctDrawn = 0, _ctStale = 0;

// viewport of the game view composited this frame (for the overlay)
static int _aiViewport[4] = { 0, 0, 0, 0 };
static BOOL _bViewThisFrame = FALSE;

static void LogLine(const char *str) {
  CPrintF("[SemCraft2] %s\n", str);
};

static LONGLONG NowNs(void) {
  static LARGE_INTEGER liFreq = { 0 };
  if (liFreq.QuadPart == 0) QueryPerformanceFrequency(&liFreq);

  LARGE_INTEGER li;
  QueryPerformanceCounter(&li);
  // the same clock as Java's System.nanoTime() on Windows
  return (LONGLONG)((double)li.QuadPart * 1.0e9 / (double)liFreq.QuadPart);
};

// ---------------------------------------------------------------------------------------------------------
// Shared memory
// ---------------------------------------------------------------------------------------------------------
static void CloseShm(void) {
  for (INDEX i = 0; i < 3; i++) {
    if (_apubSlotView[i] != NULL) UnmapViewOfFile(_apubSlotView[i]);
    _apubSlotView[i] = _apubSlot[i] = NULL;
  }
  if (_pubHeader != NULL) UnmapViewOfFile(_pubHeader);
  if (_hMap != NULL) CloseHandle(_hMap);
  _pubHeader = NULL;
  _hMap = NULL;
  _ulLastPublish = 0xFFFFFFFF;
};

static inline ULONG U32(const UBYTE *p, ULONG ulOffset) { return *(volatile const ULONG *)(p + ulOffset); };
static inline FLOAT F32(const UBYTE *p, ULONG ulOffset) { return *(volatile const FLOAT *)(p + ulOffset); };
static inline LONGLONG I64(const UBYTE *p, ULONG ulOffset) {
  // one aligned 8-byte load (atomic on x86)
  __m128i v = _mm_loadl_epi64((const __m128i *)(p + ulOffset));
  LONGLONG ll;
  _mm_storel_epi64((__m128i *)&ll, v);
  return ll;
};

static BOOL OpenShm(void) {
  if (_pubHeader != NULL) {
    if (U32(_pubHeader, 0) == MAGIC) return TRUE;
    CloseShm();
  }

  const DWORD tmNow = GetTickCount();
  if ((LONG)(tmNow - _tmNextOpen) < 0) return FALSE;
  _tmNextOpen = tmNow + 1000;

  _hMap = OpenFileMappingW(FILE_MAP_READ, FALSE, L"Local\\SemCraft2Frame");
  if (_hMap == NULL) return FALSE;

  _pubHeader = (const UBYTE *)MapViewOfFile(_hMap, FILE_MAP_READ, 0, 0, 4096);
  if (_pubHeader == NULL) { CloseShm(); return FALSE; }

  if (U32(_pubHeader, 0) != MAGIC || U32(_pubHeader, 4) != 1 || U32(_pubHeader, 8) != 4096 || U32(_pubHeader, 12) != 3) {
    CPrintF("^cff8000[SemCraft2]^r frame memory has an unexpected header, not using it\n");
    CloseShm();
    return FALSE;
  }

  _ullStride = (ULONG64)I64(_pubHeader, 16);
  _ctMaxW = U32(_pubHeader, 24);
  _ctMaxH = U32(_pubHeader, 28);

  if (_ctMaxW > 4096 || _ctMaxH > 4096 || _ullStride < (ULONG64)_ctMaxW * _ctMaxH * 12) {
    CPrintF("^cff8000[SemCraft2]^r frame memory sizes don't add up, not using it\n");
    CloseShm();
    return FALSE;
  }

  CPrintF("^c00ff00[SemCraft2]^r Minecraft frames attached (up to %dx%d)\n", _ctMaxW, _ctMaxH);
  return TRUE;
};

// The data of slot i, mapped on first use: three smaller views fit a fragmented 32-bit address space better.
static const UBYTE *SlotData(INDEX iSlot) {
  if (_apubSlot[iSlot] != NULL) return _apubSlot[iSlot];

  SYSTEM_INFO si;
  GetSystemInfo(&si);
  const ULONG64 ullOffset = 4096 + _ullStride * iSlot;
  const ULONG64 ullAligned = ullOffset - (ullOffset % si.dwAllocationGranularity);
  const SIZE_T size = (SIZE_T)(ullOffset - ullAligned + _ullStride);

  const UBYTE *pView = (const UBYTE *)MapViewOfFile(_hMap, FILE_MAP_READ, (DWORD)(ullAligned >> 32), (DWORD)ullAligned, size);
  if (pView == NULL) {
    CPrintF("^cff8000[SemCraft2]^r couldn't map frame slot %d (%d MB): error %d\n", iSlot, (INDEX)(size >> 20), (INDEX)GetLastError());
    return NULL;
  }

  _apubSlotView[iSlot] = pView;
  _apubSlot[iSlot] = pView + (SIZE_T)(ullOffset - ullAligned);
  return _apubSlot[iSlot];
};

// Upload the newest published frame, if there's a new one (inside a GL pass).
static void Upload(void) {
  if (!OpenShm()) return;

  const ULONG ulPublish = U32(_pubHeader, 32);
  if (ulPublish == _ulLastPublish && gldraw::HasFrame()) return;

  const INDEX iSlot = (INDEX)U32(_pubHeader, 40);
  if (iSlot < 0 || iSlot > 2) return;

  const UBYTE *pDesc = _pubHeader + 256 + 128 * iSlot;
  const ULONG ulSeq0 = U32(pDesc, 0);
  if (ulSeq0 & 1) return; // being written: next frame

  const INDEX w = U32(pDesc, 24), h = U32(pDesc, 28);
  if (w <= 0 || h <= 0 || w > _ctMaxW || h > _ctMaxH) return;

  const UBYTE *pData = SlotData(iSlot);
  if (pData == NULL) return;

  // the descriptor before the pixels (they must belong together)
  const FLOAT fNear = F32(pDesc, 32), fFar = F32(pDesc, 36), fVFov = F32(pDesc, 40);
  const ULONG ulFlags = U32(pDesc, 44);
  Pose pose;
  pose.vPos = FLOAT3D(F32(pDesc, 48), F32(pDesc, 52), F32(pDesc, 56));
  pose.aAngles = ANGLE3D(F32(pDesc, 60), F32(pDesc, 64), F32(pDesc, 68));
  const LONGLONG llPublishNs = I64(pDesc, 80);
  const FLOAT fAspect = F32(pDesc, 88);

  const SIZE_T n = (SIZE_T)w * h * 4;
  gldraw::Upload(w, h, pData, pData + n, pData + 2 * n);

  // Minecraft wrote into this slot while we read it: the textures are a mix, skip them
  if (U32(pDesc, 0) != ulSeq0) {
    _ctTorn++;
    gldraw::ForgetFrame();
    return;
  }

  static BOOL bLoggedFirst = FALSE;
  if (!bLoggedFirst) {
    bLoggedFirst = TRUE;
    CPrintF("[SemCraft2] first Minecraft frame: %dx%d, near %.3f far %.1f vfov %.2f aspect %.3f flags %d\n",
      w, h, fNear, fFar, fVFov, fAspect, (INDEX)ulFlags);
  }

  _ulLastPublish = ulPublish;
  _frmW = w; _frmH = h;
  _frmNear = fNear; _frmFar = fFar; _frmVFov = fVFov;
  _frmAspect = (fAspect > 0.1f) ? fAspect : (FLOAT)w / (FLOAT)h;
  _ulFrmFlags = ulFlags;
  _frmPose = pose;
  _llFrmPublishNs = llPublishNs;
  _ctUploads++;
};

// ---------------------------------------------------------------------------------------------------------
// Passes
// ---------------------------------------------------------------------------------------------------------
static void RowMajor(const FLOATmatrix3D &m, float af[9]) {
  for (INDEX r = 0; r < 3; r++) {
    for (INDEX c = 0; c < 3; c++) {
      af[r * 3 + c] = m(r + 1, c + 1);
    }
  }
};

void DrawWorld(CPerspectiveProjection3D &ppr) {
  _bViewThisFrame = FALSE;
  if (sc_iCompositeMode >= 2) return;
  if (!gldraw::Ready(&LogLine)) return;

  gldraw::Begin();
  Upload();
  gldraw::GetViewport(_aiViewport);
  _bViewThisFrame = TRUE;

  if (!gldraw::HasFrame() || NowNs() - _llFrmPublishNs > 500000000LL) {
    if (gldraw::HasFrame()) _ctStale++;
    gldraw::End();
    return;
  }

  gldraw::WorldParams p;
  for (INDEX i = 0; i < 4; i++) p.afViewport[i] = (float)_aiViewport[i];

  // Sam's frustum, the way the engine hands it to glFrustum (DrawPort.cpp)
  p.afTanCur[0] = ppr.pr_plClipL(3) / ppr.pr_plClipL(1);
  p.afTanCur[1] = ppr.pr_plClipR(3) / ppr.pr_plClipR(1);
  p.afTanCur[2] = ppr.pr_plClipD(3) / ppr.pr_plClipD(2);
  p.afTanCur[3] = ppr.pr_plClipU(3) / ppr.pr_plClipU(2);

  const FLOAT fTanY = tanf(_frmVFov * 0.5f * PI / 180.0f);
  p.afTanMc[0] = fTanY * _frmAspect;
  p.afTanMc[1] = fTanY;

  // view space -> world, for Sam's current camera and the one Minecraft rendered for
  FLOATmatrix3D mCur, mMc;
  MakeRotationMatrix(mCur, ppr.pr_ViewerPlacement.pl_OrientationAngle);
  MakeRotationMatrix(mMc, _frmPose.aAngles);

  const FLOATmatrix3D mMcT = !mMc; // transposed: world -> view
  const FLOATmatrix3D mCurT = !mCur;

  RowMajor(mMcT * mCur, p.afRot); // current view -> Minecraft's view
  const FLOAT3D vT = (_frmPose.vPos - ppr.pr_ViewerPlacement.pl_PositionVector) * mCurT; // (vector * matrix = matrix applied)
  p.afT[0] = vT(1); p.afT[1] = vT(2); p.afT[2] = vT(3);

  p.afMcPlanes[0] = _frmNear;
  p.afMcPlanes[1] = _frmFar;

  FLOAT fFar = ppr.pr_FarClipDistance;
  if (fFar < 0) fFar = 1E5f;
  p.afSam[0] = ppr.pr_NearClipDistance;
  p.afSam[1] = fFar;
  p.afSam[2] = ppr.pr_fDepthBufferNear;
  p.afSam[3] = ppr.pr_fDepthBufferFar;
  p.fBias = sc_fDepthBias;
  p.fMode = (float)sc_iCompositeMode;
  p.fLight = Clamp(sc_fLightMatch, 0.0f, 1.0f);

  gldraw::DrawWorld(p);
  _ctDrawn++;
  gldraw::End();
};

void DrawOverlay(void) {
  if (!_bViewThisFrame) return;
  _bViewThisFrame = FALSE;
  if (sc_iCompositeMode >= 2 || !gldraw::HasFrame() || !gldraw::ContextCurrent()) return;
  if (NowNs() - _llFrmPublishNs > 500000000LL) return;

  gldraw::Begin();
  gldraw::DrawOverlay(_aiViewport);
  gldraw::End();
};

void Shutdown(BOOL bWithGL) {
  if (bWithGL) gldraw::Release();
  CloseShm();
};

CTString Status(void) {
  CTString str;
  str.PrintF("frames: %s, uploaded %u, drawn %u, torn %u, stale %u, last %dx%d vfov %.1f, GL: %s",
    _pubHeader != NULL ? "attached" : "not attached", _ctUploads, _ctDrawn, _ctTorn, _ctStale, _frmW, _frmH, _frmVFov, gldraw::Renderer());
  return str;
};

}; // namespace
