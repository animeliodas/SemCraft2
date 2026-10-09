/* SemCraft 2 - Minecraft's blocks are solid in Serious Sam. See Blocks.h. */
#include "StdH.h"
#include "Blocks.h"

#include <map>
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

void Reset(BOOL bPurgeWorld) {
  _mapBlocks.clear();
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
  *pbActive = FALSE; // drawn by the editor only

  pen->Initialize();
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
};

void Step(void) {
  if (!_pNetwork->IsServer() || _bBroken) return;

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

CTString Status(void) {
  CTString str;
  str.PrintF("blocks: %d solid in Sam (%u created, %u removed, %d queued)%s", (INDEX)_mapBlocks.size(), _ctCreated, _ctRemoved,
    (INDEX)_aQueue.size(), _bBroken ? " BROKEN" : "");
  return str;
};

}; // namespace
