/* SemCraft 2 - Minecraft's blocks are solid in Serious Sam. See Blocks.h. */
#include "StdH.h"
#include "Blocks.h"

#include <Engine/Math/Object3D.h>
#include <Engine/Math/Float.h>
#include <Engine/Brushes/Brush.h>

#include <map>
#include <set>
#include <deque>

namespace blocks {

// ModelHolder2 (class 210) properties, the same in TFE and TSE
static const ULONG CLASS_MODELHOLDER2 = 210;
static ULONG PROP(ULONG ulNumber) { return (CLASS_MODELHOLDER2 << 8) | ulNumber; };
enum { P_MODEL = 1, P_STRETCHALL = 3, P_STRETCHX = 4, P_STRETCHY = 5, P_STRETCHZ = 6, P_NAME = 7, P_COLLIDING = 8,
  P_SHADOWS = 11, P_ACTIVE = 26 };

// the name our block entities carry (to find ones a savegame brought back)
static const char *BLOCK_NAME = "SemCraft2 block";
// at most this many block entities (the closest ones win while Minecraft streams them)
static const INDEX MAX_BLOCKS = 1500;
// entities created / removed per tick
static const INDEX BUDGET = 64;

struct Key {
  INDEX x, y, z;
  bool operator<(const Key &o) const { return x != o.x ? x < o.x : (y != o.y ? y < o.y : z < o.z); };
};

struct Change {
  Key k;
  BOOL bSet;
};

static std::map<Key, CEntityPointer> _mapBlocks;
static std::deque<Change> _aQueue;
static BOOL _bPurge = FALSE;
static BOOL _bBroken = FALSE;
static ULONG _ctCreated = 0, _ctRemoved = 0;

// 1 = Minecraft's blocks are invisible brushes (flat faces, exact edges), 0 = colliding models (rounded)
INDEX sc_iBlockBrushes = 1;

// --- brushes: one WorldBase per chunk of CHUNK^3 cells, rebuilt in place when its cells change
static const INDEX CHUNK = 8;
static const INDEX CHUNK_REBUILDS = 4; // per tick
// WorldBase (class 100) property 1: its name, to find ours a savegame brought back
static const ULONG PROP_WORLDBASE_NAME = (100 << 8) | 1;
static const char *BRUSH_NAME = "SemCraft2 blocks";

struct Chunk {
  CEntityPointer pen;
  BOOL bDirty;
};
static std::set<Key> _setCells;
static std::map<Key, Chunk> _mapChunks;
static ULONG _ctRebuilds = 0, _ctFaces = 0;

static INDEX FloorDiv(INDEX a, INDEX b) { return a >= 0 ? a / b : -((-a + b - 1) / b); };
static Key ChunkOf(const Key &k) { Key c = { FloorDiv(k.x, CHUNK), FloorDiv(k.y, CHUNK), FloorDiv(k.z, CHUNK) }; return c; };

static void *PropPtr(CEntity *pen, ULONG ulID) {
  CEntityProperty *pep = IWorld::PropertyForId(LibClassHolder(pen), ulID);
  return pep != NULL ? (UBYTE *)pen + pep->ep_slOffset : NULL;
};

void OnBlocks(const double *adSet, INDEX ctSet, const double *adClear, INDEX ctClear) {
  for (INDEX i = 0; i + 2 < ctClear; i += 3) {
    Change c = { { (INDEX)floor(adClear[i] + 0.5), (INDEX)floor(adClear[i + 1] + 0.5), (INDEX)floor(adClear[i + 2] + 0.5) }, FALSE };
    _aQueue.push_back(c);
  }
  for (INDEX i = 0; i + 2 < ctSet; i += 3) {
    Change c = { { (INDEX)floor(adSet[i] + 0.5), (INDEX)floor(adSet[i + 1] + 0.5), (INDEX)floor(adSet[i + 2] + 0.5) }, TRUE };
    _aQueue.push_back(c);
  }
  while (_aQueue.size() > 20000) _aQueue.pop_front();
};

void Abandon(void) {
  for (std::map<Key, CEntityPointer>::iterator it = _mapBlocks.begin(); it != _mapBlocks.end(); ++it) it->second.ep_pen = NULL;
  for (std::map<Key, Chunk>::iterator it = _mapChunks.begin(); it != _mapChunks.end(); ++it) it->second.pen.ep_pen = NULL;
  _mapBlocks.clear();
  _mapChunks.clear();
};

void Reset(BOOL bPurgeWorld) {
  _mapBlocks.clear();
  _setCells.clear();
  _mapChunks.clear();
  _aQueue.clear();
  _bPurge = bPurgeWorld;
};

// Create an entity; any exception (a missing class file throws, entity code may fault) gives NULL.
static CEntity *CreateEntitySEH(const CPlacement3D &pl, const CTFileName &fnmClass) {
  __try {
    return IWorld::GetWorld()->CreateEntity_t(pl, fnmClass);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return NULL;
  }
};

// One invisible colliding cube with its minimum corner at k.
static BOOL CreateBlock(const Key &k) {
  static const CTFileName fnmClass = CTFILENAME("Classes\\ModelHolder2.ecl");
  const CPlacement3D pl(FLOAT3D((FLOAT)k.x, (FLOAT)k.y, (FLOAT)k.z), ANGLE3D(0, 0, 0));
  CEntity *pen = CreateEntitySEH(pl, fnmClass);
  if (pen == NULL) return FALSE;

  CTFileName *pfnModel = (CTFileName *)PropPtr(pen, PROP(P_MODEL));
  CTString *pstrName = (CTString *)PropPtr(pen, PROP(P_NAME));
  BOOL *pbColliding = (BOOL *)PropPtr(pen, PROP(P_COLLIDING));
  BOOL *pbActive = (BOOL *)PropPtr(pen, PROP(P_ACTIVE));
  INDEX *piShadows = (INDEX *)PropPtr(pen, PROP(P_SHADOWS));
  FLOAT *pfStretch = (FLOAT *)PropPtr(pen, PROP(P_STRETCHALL));
  if (pfnModel == NULL || pbColliding == NULL || pbActive == NULL) {
    pen->Destroy();
    return FALSE;
  }

  // the editor's collision cube: its collision box is (0,0,0)-(1,1,1), exactly the block
  *pfnModel = CTFILENAME("Models\\Editor\\CollisionBox.mdl");
  if (pstrName != NULL) *pstrName = BLOCK_NAME;
  if (pfStretch != NULL) *pfStretch = 1.0f;
  if (piShadows != NULL) *piShadows = 0; // ST_NONE
  *pbColliding = TRUE;
  // a real model, not an editor one: rays (Sam's bullets, monsters' sight) skip editor models outside the editor, and
  // TSE's ModelHolder2 only collides when active. Its colour mask then hides every surface.
  *pbActive = TRUE;

  pen->Initialize();
  CModelObject *pmo = pen->GetModelObject();
  if (pmo != NULL) pmo->mo_ColorMask = 0;
  _mapBlocks[k] = pen;
  _ctCreated++;
  return TRUE;
};

static void RemoveBlock(const Key &k) {
  std::map<Key, CEntityPointer>::iterator it = _mapBlocks.find(k);
  if (it == _mapBlocks.end()) return;

  CEntity *pen = it->second;
  if (pen != NULL && !(pen->GetFlags() & ENF_DELETED)) pen->Destroy();
  _mapBlocks.erase(it);
  _ctRemoved++;
};

// Block entities a savegame (or an earlier session) left in the world.
static void Purge(void) {
  CDynamicContainer<CEntity> cenOld;
  FOREACHINDYNAMICCONTAINER(IWorld::GetWorld()->wo_cenEntities, CEntity, iten) {
    CEntity *pen = &*iten;
    if ((pen->GetFlags() & ENF_DELETED) || !IsOfClass(pen, "ModelHolder2")) continue;
    CTString *pstrName = (CTString *)PropPtr(pen, PROP(P_NAME));
    if (pstrName != NULL && *pstrName == BLOCK_NAME) cenOld.Add(pen);
  }

  INDEX ct = 0;
  FOREACHINDYNAMICCONTAINER(cenOld, CEntity, iten) {
    iten->Destroy();
    ct++;
  }
  if (ct > 0) CPrintF("[SemCraft2] removed %d old block entities\n", ct);

  // block brushes a savegame brought back: brushes aren't deleted on the fly, so they're adopted as their chunk's and
  // rebuilt from the blocks Minecraft reports now
  INDEX ctBrushes = 0;
  FOREACHINDYNAMICCONTAINER(IWorld::GetWorld()->wo_cenEntities, CEntity, iten) {
    CEntity *pen = &*iten;
    if ((pen->GetFlags() & ENF_DELETED) || pen->en_RenderType != CEntity::RT_BRUSH || !IsOfClass(pen, "WorldBase")) continue;
    CTString *pstrName = (CTString *)PropPtr(pen, PROP_WORLDBASE_NAME);
    if (pstrName == NULL || *pstrName != BRUSH_NAME) continue;
    const FLOAT3D &v = pen->GetPlacement().pl_PositionVector;
    const Key kChunk = { FloorDiv((INDEX)floor(v(1) + 0.5f), CHUNK), FloorDiv((INDEX)floor(v(2) + 0.5f), CHUNK), FloorDiv((INDEX)floor(v(3) + 0.5f), CHUNK) };
    Chunk &ch = _mapChunks[kChunk];
    ch.pen = pen;
    ch.bDirty = TRUE;
    ctBrushes++;
  }
  if (ctBrushes > 0) CPrintF("[SemCraft2] %d block brushes from before adopted\n", ctBrushes);
};

// The faces of the chunk's cells that touch air (in the chunk's own coordinates), as invisible polygons of one sector.
static INDEX BuildObject(const Key &kChunk, CObject3D &ob) {
  CSetFPUPrecision sfp(FPT_53BIT);
  CObjectSector *posc = ob.ob_aoscSectors.New(1);
  // a solid thing, like a door: its polygons face outwards (a room's face inwards); and it's never drawn
  posc->osc_ulFlags[0] = BSCF_OPENSECTOR | BSCF_INVISIBLE;
  posc->osc_ulFlags[1] = 0;
  posc->osc_ulFlags[2] = 0;
  CObjectMaterial *pom = posc->osc_aomtMaterials.New(1);
  pom->omt_Name = CTString(""); // no texture: the polygons are never drawn

  // the six faces: outward normal and the quad's corners on the unit cube, counter-clockwise seen from outside
  static const INDEX aiN[6][3] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
  static const DOUBLE aavQuad[6][4][3] = {
    { { 1, 0, 0 }, { 1, 1, 0 }, { 1, 1, 1 }, { 1, 0, 1 } },
    { { 0, 0, 0 }, { 0, 0, 1 }, { 0, 1, 1 }, { 0, 1, 0 } },
    { { 0, 1, 0 }, { 0, 1, 1 }, { 1, 1, 1 }, { 1, 1, 0 } },
    { { 0, 0, 0 }, { 1, 0, 0 }, { 1, 0, 1 }, { 0, 0, 1 } },
    { { 0, 0, 1 }, { 1, 0, 1 }, { 1, 1, 1 }, { 0, 1, 1 } },
    { { 0, 0, 0 }, { 0, 1, 0 }, { 1, 1, 0 }, { 1, 0, 0 } },
  };

  INDEX ctFaces = 0;
  for (INDEX x = 0; x < CHUNK; x++) for (INDEX y = 0; y < CHUNK; y++) for (INDEX z = 0; z < CHUNK; z++) {
    const Key k = { kChunk.x * CHUNK + x, kChunk.y * CHUNK + y, kChunk.z * CHUNK + z };
    if (_setCells.find(k) == _setCells.end()) continue;
    for (INDEX f = 0; f < 6; f++) {
      const Key kn = { k.x + aiN[f][0], k.y + aiN[f][1], k.z + aiN[f][2] };
      if (_setCells.find(kn) != _setCells.end()) continue; // inside: a neighbour covers it
      DOUBLE3D av[4];
      for (INDEX v = 0; v < 4; v++) {
        av[v] = DOUBLE3D(x + aavQuad[f][v][0], y + aavQuad[f][v][1], z + aavQuad[f][v][2]);
      }
      if (posc->CreatePolygon(4, av, *pom, BPOF_INVISIBLE | BPOF_DOESNOTCASTSHADOW | BPOF_DETAILPOLYGON, FALSE) != NULL) ctFaces++;
    }
  }

  // an emptied chunk keeps its brush (brushes aren't deleted on the fly) but an empty sector brings the game down:
  // it gets a centimetre cube that nothing collides with
  if (ctFaces == 0) {
    for (INDEX f = 0; f < 6; f++) {
      DOUBLE3D av[4];
      for (INDEX v = 0; v < 4; v++) {
        av[v] = DOUBLE3D(aavQuad[f][v][0] * 0.01, aavQuad[f][v][1] * 0.01, aavQuad[f][v][2] * 0.01);
      }
      posc->CreatePolygon(4, av, *pom, BPOF_INVISIBLE | BPOF_DOESNOTCASTSHADOW | BPOF_DETAILPOLYGON | BPOF_PASSABLE | BPOF_SHOOTTHRU, FALSE);
    }
  }
  return ctFaces;
};

// The engine side of a rebuild: the brush from the object (in double precision), then its boxes, collision and links.
static void Import(CEntity *pen, CObject3D &ob) {
  {
    CSetFPUPrecision sfp(FPT_53BIT);
    pen->en_pbrBrush->FromObject3D_t(ob);
  }
  pen->en_pbrBrush->CalculateBoundingBoxes();
  pen->FindCollisionInfo();
  pen->UpdateSpatialRange();
  pen->FindSectorsAroundEntity();
};

// Engine code may throw or fault: the bridge turns blocks off instead of taking the game down.
static BOOL ImportSEH(CEntity *pen, CObject3D &ob) {
  __try {
    Import(pen, ob);
    return TRUE;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return FALSE;
  }
};

static CEntity *CreateChunkBrush(const Key &kChunk) {
  static const CTFileName fnmClass = CTFILENAME("Classes\\WorldBase.ecl");
  const CPlacement3D pl(FLOAT3D((FLOAT)(kChunk.x * CHUNK), (FLOAT)(kChunk.y * CHUNK), (FLOAT)(kChunk.z * CHUNK)), ANGLE3D(0, 0, 0));
  CEntity *pen = CreateEntitySEH(pl, fnmClass);
  if (pen == NULL) return NULL;
  CTString *pstrName = (CTString *)PropPtr(pen, PROP_WORLDBASE_NAME);
  if (pstrName != NULL) *pstrName = BRUSH_NAME;
  pen->Initialize();
  return pen->en_pbrBrush != NULL ? pen : NULL;
};

// A rebuild frees the brush's polygons, and moving entities keep pointers to polygons: the one they stand on and a
// cache of those near them. Those pointing into this brush are dropped first (the engine refills them as needed).
static void ForgetPolygonsOf(CBrush3D *pbr) {
  FOREACHINDYNAMICCONTAINER(IWorld::GetWorld()->wo_cenEntities, CEntity, iten) {
    CEntity *pen = &*iten;
    if ((pen->GetFlags() & ENF_DELETED) || !(pen->en_ulPhysicsFlags & EPF_MOVABLE)) continue;
    CMovableEntity *pmen = (CMovableEntity *)pen;

    CBrushPolygon *pbpo = pmen->en_pbpoStandOn;
    if (pbpo != NULL && pbpo->bpo_pbscSector->bsc_pbmBrushMip->bm_pbrBrush == pbr) {
      pmen->en_pbpoStandOn = NULL;
    }

    CStaticStackArray<CBrushPolygon *> &apbpo = pmen->en_apbpoNearPolygons;
    for (INDEX i = 0; i < apbpo.Count(); i++) {
      if (apbpo[i]->bpo_pbscSector->bsc_pbmBrushMip->bm_pbrBrush == pbr) {
        apbpo.PopAll();
        pmen->en_boxNearCached = FLOATaabbox3D(); // empty: the cache is gathered again on the next move
        break;
      }
    }
  }
};

static BOOL RebuildChunk(const Key &kChunk, Chunk &ch) {
  CEntity *pen = ch.pen;
  if (pen == NULL || (pen->GetFlags() & ENF_DELETED)) {
    pen = CreateChunkBrush(kChunk);
    if (pen == NULL) return FALSE;
    ch.pen = pen;
  } else {
    ForgetPolygonsOf(pen->en_pbrBrush);
  }

  CObject3D ob;
  _ctFaces += BuildObject(kChunk, ob);
  _ctRebuilds++;
  return ImportSEH(pen, ob);
};

static void StepBrushes(void) {
  // every change waiting goes into the cell set; their chunks (and the neighbours sharing a face) are rebuilt
  while (!_aQueue.empty()) {
    const Change c = _aQueue.front();
    _aQueue.pop_front();
    BOOL bChanged = c.bSet ? _setCells.insert(c.k).second : _setCells.erase(c.k) > 0;
    if (!bChanged) continue;
    for (INDEX d = 0; d < 7; d++) {
      static const INDEX aiD[7][3] = { { 0, 0, 0 }, { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
      const Key kn = { c.k.x + aiD[d][0], c.k.y + aiD[d][1], c.k.z + aiD[d][2] };
      _mapChunks[ChunkOf(kn)].bDirty = TRUE;
    }
  }

  INDEX ct = 0;
  for (std::map<Key, Chunk>::iterator it = _mapChunks.begin(); it != _mapChunks.end() && ct < CHUNK_REBUILDS; ++it) {
    if (!it->second.bDirty) continue;
    it->second.bDirty = FALSE;
    ct++;
    if (!RebuildChunk(it->first, it->second)) {
      _bBroken = TRUE;
      CPrintF("^cff0000[SemCraft2]^r building block brushes failed; Minecraft blocks won't be solid in Sam this session\n");
      return;
    }
  }
};

void Step(void) {
  if (!_pNetwork->IsServer() || _bBroken) return;

  if (sc_iBlockBrushes) {
    if (_bPurge) {
      _bPurge = FALSE;
      Purge();
    }
    StepBrushes();
    return;
  }

  if (_bPurge) {
    _bPurge = FALSE;
    Purge();
  }

  for (INDEX i = 0; i < BUDGET && !_aQueue.empty(); i++) {
    const Change c = _aQueue.front();
    _aQueue.pop_front();

    if (!c.bSet) {
      RemoveBlock(c.k);
      continue;
    }

    if (_mapBlocks.find(c.k) != _mapBlocks.end() || (INDEX)_mapBlocks.size() >= MAX_BLOCKS) continue;
    if (!CreateBlock(c.k)) {
      _bBroken = TRUE;
      CPrintF("^cff0000[SemCraft2]^r creating block entities failed; Minecraft blocks won't be solid in Sam this session\n");
      return;
    }
  }
};

// Dev: how a brush's polygons face. For each polygon of the first mip: does its plane normal point away from the
// brush's centre (out) or towards it (in), and do its edges, in order, wind counter-clockwise around that normal?
void DescribeBrush(CEntity *pen, const char *strWhat) {
  if (pen == NULL || pen->en_RenderType != CEntity::RT_BRUSH || pen->en_pbrBrush == NULL) {
    CPrintF("[SemCraft2] brush %s: none\n", strWhat);
    return;
  }
  CBrushMip *pbm = pen->en_pbrBrush->GetFirstMip();
  if (pbm == NULL) return;
  FLOAT3D vCentre(0, 0, 0);
  INDEX ctV = 0;
  FOREACHINDYNAMICARRAY(pbm->bm_abscSectors, CBrushSector, itbsc) {
    for (INDEX iVtx = 0; iVtx < itbsc->bsc_abvxVertices.Count(); iVtx++) { vCentre += itbsc->bsc_abvxVertices[iVtx].bvx_vRelative; ctV++; }
  }
  if (ctV > 0) vCentre /= (FLOAT)ctV;
  INDEX ctOut = 0, ctIn = 0, ctCCW = 0, ctCW = 0, ctPolys = 0, ctSectors = 0;
  ULONG ulFlagsOr = 0, ulSectorFlags = 0;
  FOREACHINDYNAMICARRAY(pbm->bm_abscSectors, CBrushSector, itbsc2) {
    ctSectors++;
    ulSectorFlags |= itbsc2->bsc_ulFlags;
    for (INDEX iPol = 0; iPol < itbsc2->bsc_abpoPolygons.Count(); iPol++) {
      CBrushPolygon &bpo = itbsc2->bsc_abpoPolygons[iPol];
      ctPolys++;
      ulFlagsOr |= bpo.bpo_ulFlags;
      const FLOATplane3D &pl = bpo.bpo_pbplPlane->bpl_plRelative;
      const FLOAT3D vN(pl(1), pl(2), pl(3));
      FLOAT3D vMid(0, 0, 0), vWind(0, 0, 0);
      const INDEX ctE = bpo.bpo_abpePolygonEdges.Count();
      for (INDEX e = 0; e < ctE; e++) {
        CBrushVertex *pv0, *pv1;
        bpo.bpo_abpePolygonEdges[e].GetVertices(pv0, pv1);
        CBrushVertex *pw0, *pw1;
        bpo.bpo_abpePolygonEdges[(e + 1) % ctE].GetVertices(pw0, pw1);
        vMid += pv0->bvx_vRelative;
        vWind += (pv1->bvx_vRelative - pv0->bvx_vRelative) * (pw1->bvx_vRelative - pw0->bvx_vRelative);
      }
      if (ctE > 0) vMid /= (FLOAT)ctE;
      if ((vMid - vCentre) % vN >= 0) ctOut++; else ctIn++;
      if (vWind % vN >= 0) ctCCW++; else ctCW++;
    }
  }
  CPrintF("[SemCraft2] brush %s: %d sectors (flags %08x), %d polygons (flags or %08x): normals out %d / in %d, edges ccw %d / cw %d\n",
    strWhat, ctSectors, ulSectorFlags, ctPolys, ulFlagsOr, ctOut, ctIn, ctCCW, ctCW);
};

// Dev: the chunk brush nearest to a point.
CEntity *NearestBrush(const FLOAT3D &v) {
  CEntity *penBest = NULL;
  FLOAT fBest = 1e30f;
  for (std::map<Key, Chunk>::iterator it = _mapChunks.begin(); it != _mapChunks.end(); ++it) {
    CEntity *pen = it->second.pen;
    if (pen == NULL) continue;
    const FLOAT3D vC = pen->GetPlacement().pl_PositionVector + FLOAT3D(CHUNK * 0.5f, CHUNK * 0.5f, CHUNK * 0.5f);
    const FLOAT f = (vC - v).Length();
    if (f < fBest) { fBest = f; penBest = pen; }
  }
  return penBest;
};

CTString Status(void) {
  CTString str;
  if (sc_iBlockBrushes) {
    str.PrintF("blocks: %d solid in Sam as brushes (%d chunks, %u rebuilds, %u faces, %d queued)%s", (INDEX)_setCells.size(),
      (INDEX)_mapChunks.size(), _ctRebuilds, _ctFaces, (INDEX)_aQueue.size(), _bBroken ? " BROKEN" : "");
  } else {
    str.PrintF("blocks: %d solid in Sam (%u created, %u removed, %d queued)%s", (INDEX)_mapBlocks.size(), _ctCreated, _ctRemoved,
      (INDEX)_aQueue.size(), _bBroken ? " BROKEN" : "");
  }
  return str;
};

}; // namespace
