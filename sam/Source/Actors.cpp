/* SemCraft 2 - Serious Sam's monsters in Minecraft. See Actors.h. */
#include "StdH.h"
#include "Actors.h"

#include <vector>
#include <map>
#include <stdio.h>

namespace actors {

// CEnemyBase (EnemyBase.es, class 310): m_bTemplate is property 86 (spawner templates are never alive monsters)
static const ULONG PROP_TEMPLATE = (310UL << 8) | 86;
// monsters further than this from the player aren't mirrored
static const FLOAT RANGE = 96.0f;

struct Hit {
  ULONG ulId;
  FLOAT fDamage;
  BOOL bExplosion;
};

static std::vector<Hit> _aHits;
// the monsters last sent, by entity ID (to find them again when a hit comes back)
static std::map<ULONG, CEntityPointer> _mapSent;
static ULONG _ctTicks = 0;
static ULONG _ctHitsApplied = 0;

void Reset(void) {
  _aHits.clear();
  _mapSent.clear();
};

void OnHit(ULONG ulId, FLOAT fDamage, BOOL bExplosion) {
  if (_aHits.size() < 256) {
    Hit h = { ulId, fDamage, bExplosion };
    _aHits.push_back(h);
  }
};

static BOOL IsTemplate(CEntity *pen) {
  CEntityProperty *pep = IWorld::PropertyForId(LibClassHolder(pen), PROP_TEMPLATE);
  return pep != NULL && *(BOOL *)((UBYTE *)pen + pep->ep_slOffset);
};

static BOOL SafeInflict(CEntity *penTarget, CEntity *penInflictor, INDEX iType, FLOAT fDamage, const FLOAT3D &vHit, const FLOAT3D &vDir) {
  __try {
    penInflictor->InflictDirectDamage(penTarget, penInflictor, (DamageType)iType, fDamage, vHit, vDir);
    return TRUE;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return FALSE;
  }
};

void Step(CEntity *penPlayer, void (*pfnSend)(const std::string &)) {
  _ctTicks++;

  // Minecraft's hits on the monsters: from the player (so the monster turns on them, as with Sam's own weapons)
  for (size_t i = 0; i < _aHits.size(); i++) {
    const Hit &h = _aHits[i];
    std::map<ULONG, CEntityPointer>::iterator it = _mapSent.find(h.ulId);
    if (it == _mapSent.end() || penPlayer == NULL) continue;

    CEntity *pen = it->second;
    if (pen == NULL || (pen->GetFlags() & ENF_DELETED) || !(pen->GetFlags() & ENF_ALIVE)) continue;

    FLOAT3D vDir = pen->GetPlacement().pl_PositionVector - penPlayer->GetPlacement().pl_PositionVector;
    if (vDir.Length() < 0.01f) vDir = FLOAT3D(0, 0, -1);
    vDir.Normalize();
    if (SafeInflict(pen, penPlayer, h.bExplosion ? DMT_EXPLOSION : DMT_CLOSERANGE, h.fDamage, pen->GetPlacement().pl_PositionVector, vDir)) {
      _ctHitsApplied++;
    }
  }
  _aHits.clear();

  if (penPlayer == NULL || (_ctTicks & 1) != 0) return;

  // the living monsters near the player: [id, x, y, z (feet), height, width, health]
  const FLOAT3D vPlayer = penPlayer->GetPlacement().pl_PositionVector;
  std::string str = "{\"t\":\"actors\",\"l\":[";
  INDEX ct = 0;
  _mapSent.clear();

  FOREACHINDYNAMICCONTAINER(IWorld::GetWorld()->wo_cenEntities, CEntity, iten) {
    CEntity *pen = &*iten;
    const ULONG ulFlags = pen->GetFlags();
    if ((ulFlags & ENF_DELETED) || !(ulFlags & ENF_ALIVE) || pen->IsPredictor()) continue;
    if (!IsDerivedFromClass(pen, "Enemy Base")) continue;

    const FLOAT fHealth = ((CLiveEntity *)pen)->en_fHealth;
    if (fHealth <= 0) continue;

    const FLOAT3D vPos = pen->GetPlacement().pl_PositionVector;
    if ((vPos - vPlayer).Length() > RANGE || IsTemplate(pen)) continue;

    FLOATaabbox3D box;
    pen->GetBoundingBox(box);
    const FLOAT3D vSize = box.Size();

    char strOne[200];
    _snprintf(strOne, sizeof(strOne), "%s[%u,%.3f,%.3f,%.3f,%.2f,%.2f,%.1f]", ct == 0 ? "" : ",", pen->en_ulID,
      vPos(1), box.minvect(2), vPos(3), vSize(2), Max(vSize(1), vSize(3)), fHealth);
    strOne[sizeof(strOne) - 1] = 0;
    str += strOne;
    _mapSent[pen->en_ulID] = pen;
    ct++;
  }

  str += "]}";
  pfnSend(str);
};

CTString Status(void) {
  CTString str;
  str.PrintF("monsters: %d mirrored, %u Minecraft hits applied", (INDEX)_mapSent.size(), _ctHitsApplied);
  return str;
};

}; // namespace
