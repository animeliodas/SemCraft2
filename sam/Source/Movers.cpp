/* SemCraft 2 - Serious Sam's moving brushes followed in Minecraft. See Movers.h. */
#include "StdH.h"
#include "Movers.h"
#include "Bridge.h"

#include <map>
#include <vector>
#include <stdio.h>

namespace movers {

// By entity ID, looked up through the current world's brushes each time: no pointers outlive a world.
struct Mover {
  ULONG ulID;
  FLOAT3D vPos;
  FLOATmatrix3D mRot;
  BOOL bSent;
};

static std::vector<Mover> _aMovers;
static std::map<ULONG, size_t> _mapIndex;
static ULONG _ctTicks = 0, _ctSent = 0;
// a moving brush is sent at most this often (ticks), and everything again this often
static const ULONG EVERY = 2, RESEND = 100;
// placements per message
static const INDEX PER_MESSAGE = 48;

void Reset(void) {
  _aMovers.clear();
  _mapIndex.clear();
};

void Add(CEntity *pen) {
  Mover m;
  m.ulID = pen->en_ulID;
  _mapIndex[m.ulID] = _aMovers.size();
  m.vPos = FLOAT3D(0, 0, 0);
  m.mRot.Diagonal(1.0f);
  m.bSent = FALSE;
  _aMovers.push_back(m);
};

// ETrigger of the game's entities (EntitiesV/Global.h: event code 7, then who caused it), made here because the plugin
// doesn't link the Entities library. The engine copies it through MakeCopy and deletes the copy through the virtual
// destructor, both of this class.
class CTriggerEvent : public CEntityEvent {
public:
  CEntityPointer penCaused;
  CTriggerEvent(void) : CEntityEvent(0x00000007) {};
  CEntityEvent *MakeCopy(void) { return new CTriggerEvent(*this); };
};

static BOOL SendEventSEH(CEntity *pen, const CEntityEvent &ee) {
  __try {
    pen->SendEvent(ee);
    return TRUE;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return FALSE;
  }
};

CEntity *FindMover(INDEX i) {
  CWorld *pwo = IWorld::GetWorld();
  if (pwo == NULL || i < 0 || i >= (INDEX)_aMovers.size()) return NULL;
  const ULONG ulID = _aMovers[i].ulID;
  CEntity *penFound = NULL;
  CDynamicArray<CBrush3D> &abr = pwo->wo_baBrushes.ba_abrBrushes;
  abr.Lock();
  for (INDEX iBrush = 0; iBrush < abr.Count() && penFound == NULL; iBrush++) {
    CEntity *pen = abr[iBrush].br_penEntity;
    if (pen != NULL && !(pen->GetFlags() & ENF_DELETED) && pen->en_ulID == ulID) penFound = pen;
  }
  abr.Unlock();
  return penFound;
};

BOOL TriggerEntity(CEntity *pen, CEntity *penCaused) {
  if (pen == NULL) return FALSE;
  CTriggerEvent ee;
  ee.penCaused = penCaused;
  return SendEventSEH(pen, ee);
};

BOOL Trigger(INDEX i, CEntity *penCaused) {
  return TriggerEntity(FindMover(i), penCaused);
};

void ResendAll(void) {
  for (size_t i = 0; i < _aMovers.size(); i++) _aMovers[i].bSent = FALSE;
};

void Step(void (*pfnSend)(const std::string &)) {
  _ctTicks++;
  if (_ctTicks % RESEND == 0) ResendAll();
  if (_ctTicks % EVERY != 0) return;

  CWorld *pwo = IWorld::GetWorld();
  if (pwo == NULL || _aMovers.empty()) return;

  std::string str;
  INDEX ct = 0;
  CDynamicArray<CBrush3D> &abr = pwo->wo_baBrushes.ba_abrBrushes;
  abr.Lock();
  for (INDEX iBrush = 0; iBrush < abr.Count(); iBrush++) {
    CEntity *pen = abr[iBrush].br_penEntity;
    if (pen == NULL || (pen->GetFlags() & ENF_DELETED)) continue;
    std::map<ULONG, size_t>::const_iterator it = _mapIndex.find(pen->en_ulID);
    if (it == _mapIndex.end()) continue;
    const size_t i = it->second;
    Mover &m = _aMovers[i];

    const FLOAT3D &v = pen->GetPlacement().pl_PositionVector;
    const FLOATmatrix3D &r = pen->en_mRotation;
    BOOL bMoved = !m.bSent || (v - m.vPos).Length() > 0.01f;
    for (INDEX a = 1; a <= 3 && !bMoved; a++) {
      for (INDEX b = 1; b <= 3; b++) {
        if (Abs(r(a, b) - m.mRot(a, b)) > 0.0005f) { bMoved = TRUE; break; }
      }
    }
    if (!bMoved) continue;

    m.vPos = v;
    m.mRot = r;
    m.bSent = TRUE;

    // [index, x, y, z, m11, m12, m13, m21, m22, m23, m31, m32, m33]: absolute = position + M * relative
    char strOne[384];
    _snprintf(strOne, sizeof(strOne), "%s[%u,%.3f,%.3f,%.3f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f]", ct == 0 ? "" : ",",
      (ULONG)i, v(1), v(2), v(3), r(1, 1), r(1, 2), r(1, 3), r(2, 1), r(2, 2), r(2, 3), r(3, 1), r(3, 2), r(3, 3));
    strOne[sizeof(strOne) - 1] = 0;
    str += strOne;
    ct++;
    _ctSent++;

    if (ct >= PER_MESSAGE) {
      pfnSend("{\"t\":\"movers\",\"l\":[" + str + "]}");
      str.clear();
      ct = 0;
    }
  }
  abr.Unlock();

  if (ct > 0) pfnSend("{\"t\":\"movers\",\"l\":[" + str + "]}");
};

// The box of a brush's solid polygons where they are now (GetBoundingBox of a brush is its bounding sphere's box).
static BOOL SolidBox(CEntity *pen, FLOATaabbox3D &box) {
  if (pen == NULL || pen->en_RenderType != CEntity::RT_BRUSH || pen->en_pbrBrush == NULL) return FALSE;
  CBrushMip *pbm = pen->en_pbrBrush->GetFirstMip();
  if (pbm == NULL) return FALSE;
  BOOL bAny = FALSE;
  FOREACHINDYNAMICARRAY(pbm->bm_abscSectors, CBrushSector, itbsc) {
    for (INDEX iPol = 0; iPol < itbsc->bsc_abpoPolygons.Count(); iPol++) {
      CBrushPolygon &bpo = itbsc->bsc_abpoPolygons[iPol];
      if (bpo.bpo_ulFlags & BPOF_PASSABLE) continue;
      for (INDEX k = 0; k < bpo.bpo_apbvxTriangleVertices.Count(); k++) {
        const FLOAT3D &v = bpo.bpo_apbvxTriangleVertices[k]->bvx_vAbsolute;
        if (!bAny) box = FLOATaabbox3D(v); else box |= FLOATaabbox3D(v);
        bAny = TRUE;
      }
    }
  }
  return bAny;
};

void List(void) {
  for (size_t i = 0; i < _aMovers.size(); i++) {
    const Mover &m = _aMovers[i];
    FLOATaabbox3D box;
    CEntity *pen = FindMover((INDEX)i);
    SolidBox(pen, box);
    CPrintF("[SemCraft2] mover %d (entity %u): at (%.1f, %.1f, %.1f) box (%.1f, %.1f, %.1f)-(%.1f, %.1f, %.1f)%s\n", (INDEX)i, m.ulID,
      m.vPos(1), m.vPos(2), m.vPos(3), box.minvect(1), box.minvect(2), box.minvect(3), box.maxvect(1), box.maxvect(2), box.maxvect(3),
      m.bSent ? "" : " (not sent yet)");
  }
};

CTString Status(void) {
  // the nearest one to the local player, for tests
  CEntity *penPlayer = bridge::LocalPlayer();
  FLOAT fNearest = 1e9f;
  FLOAT3D vNearest(0, 0, 0);
  if (penPlayer != NULL) {
    const FLOAT3D vPlayer = penPlayer->GetPlacement().pl_PositionVector;
    for (size_t i = 0; i < _aMovers.size(); i++) {
      if (!_aMovers[i].bSent) continue;
      const FLOAT fDist = (_aMovers[i].vPos - vPlayer).Length();
      if (fDist < fNearest) { fNearest = fDist; vNearest = _aMovers[i].vPos; }
    }
  }

  CTString str;
  str.PrintF("movers: %d moving brushes, %u placements sent, nearest %.1f m at (%.1f, %.1f, %.1f)", (INDEX)_aMovers.size(),
    _ctSent, fNearest, vNearest(1), vNearest(2), vNearest(3));
  return str;
};

}; // namespace
