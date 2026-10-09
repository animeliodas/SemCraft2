/* SemCraft 2 - Serious Sam's weapons in Minecraft. See Weapons.h. */
#include "StdH.h"
#include "Weapons.h"

#include <vector>
#include <stdio.h>

namespace weapons {

// Entity class IDs and property numbers (the .es files of TFE 1.05 / TSE 1.07; property ID = class << 8 | number)
static const ULONG CLASS_PLAYER = 401, CLASS_WEAPONS = 402, CLASS_PROJECTILE = 501, CLASS_CANNONBALL = 506;
static const ULONG PROP(ULONG ulClass, ULONG ulNumber) { return (ulClass << 8) | ulNumber; };

#ifdef SEMCRAFT_GAME_TFE
  enum { W_KNIFE = 1, W_COLT = 2, W_DOUBLECOLT = 3, W_SINGLESHOTGUN = 4, W_DOUBLESHOTGUN = 5, W_TOMMYGUN = 6, W_MINIGUN = 7,
    W_CHAINSAW = -1, W_SNIPER = -1 };
#else
  enum { W_KNIFE = 1, W_COLT = 2, W_DOUBLECOLT = 3, W_SINGLESHOTGUN = 4, W_DOUBLESHOTGUN = 5, W_TOMMYGUN = 6, W_MINIGUN = 7,
    W_CHAINSAW = 10, W_SNIPER = 13 };
#endif

// ProjectileType (Projectile.es)
enum { PRT_ROCKET = 0, PRT_GRENADE = 1, PRT_FLAME = 2, PRT_LASER_RAY = 3 };

// ammo counters followed from tick to tick
enum EAmmo { A_COLT, A_BULLETS, A_SHELLS, A_ROCKETS, A_GRENADES, A_NAPALM, A_ELECTRICITY, A_IRONBALLS, A_SNIPER, A_COUNT };
static const ULONG _aulAmmoProp[A_COUNT] = { 215, 40, 42, 44, 46, 48, 50, 52, 54 };

static INDEX _aiLast[A_COUNT];
static BOOL _bHaveLast = FALSE;
static INDEX _iLastWeapon = -1;
static INDEX _ctMeleeCooldown = 0;
static CEntity *_penLastWeapons = NULL;

struct Tracked {
  CEntityPointer pen;
  ULONG ulId;
  FLOAT3D vLast;
  FLOAT fRadius, fDamage;
  BOOL bBreak;
  const char *strKind;
};
static std::vector<Tracked> _aTracked;
static ULONG _ulNextId = 1;
// projectiles Minecraft says hit one of its mobs (exploded there): removed from Sam quietly
static std::vector<ULONG> _aulHit;

// stats
static ULONG _ctShots = 0, _ctBooms = 0;

// Pointer to a property's value in an entity, or NULL if the class has no such property.
static void *PropPtr(CEntity *pen, ULONG ulID) {
  if (pen == NULL) return NULL;
  CEntityProperty *pep = IWorld::PropertyForId(LibClassHolder(pen), ulID);
  if (pep == NULL) return NULL;
  return (UBYTE *)pen + pep->ep_slOffset;
};

static INDEX PropIndex(CEntity *pen, ULONG ulID, INDEX iDefault) {
  INDEX *pi = (INDEX *)PropPtr(pen, ulID);
  return pi != NULL ? *pi : iDefault;
};

static CEntity *PropEntity(CEntity *pen, ULONG ulID) {
  CEntityPointer *pep = (CEntityPointer *)PropPtr(pen, ulID);
  return pep != NULL ? (CEntity *)(*pep) : NULL;
};

void Reset(void) {
  _bHaveLast = FALSE;
  _iLastWeapon = -1;
  _penLastWeapons = NULL;
  _aTracked.clear();
};

static void SendShot(void (*pfnSend)(const std::string &), const FLOAT3D &vOrigin, const FLOAT3D &vDir, FLOAT fDamage, INDEX ctRays, FLOAT fSpread) {
  char str[256];
  _snprintf(str, sizeof(str), "{\"t\":\"shot\",\"o\":[%.3f,%.3f,%.3f],\"d\":[%.4f,%.4f,%.4f],\"dmg\":%.1f,\"n\":%d,\"spread\":%.2f}",
    vOrigin(1), vOrigin(2), vOrigin(3), vDir(1), vDir(2), vDir(3), fDamage, ctRays, fSpread);
  str[sizeof(str) - 1] = 0;
  pfnSend(str);
  _ctShots += ctRays;
};

static void SendBoom(void (*pfnSend)(const std::string &), const Tracked &t) {
  char str[256];
  _snprintf(str, sizeof(str), "{\"t\":\"boom\",\"p\":[%.3f,%.3f,%.3f],\"r\":%.1f,\"dmg\":%.1f,\"break\":%s,\"src\":\"%s\"}",
    t.vLast(1), t.vLast(2), t.vLast(3), t.fRadius, t.fDamage, t.bBreak ? "true" : "false", t.strKind);
  str[sizeof(str) - 1] = 0;
  pfnSend(str);
  _ctBooms++;
};

// New projectiles: the player's rockets, grenades, laser bolts, flames and cannonballs, or (bEnemies) the monsters'.
static void FindProjectiles(CEntity *penPlayer, BOOL bEnemies) {
  CWorld *pwo = IWorld::GetWorld();

  FOREACHINDYNAMICCONTAINER(pwo->wo_cenEntities, CEntity, iten) {
    CEntity *pen = &*iten;
    if ((pen->GetFlags() & ENF_DELETED) || pen->IsPredictor()) continue;

    const BOOL bProjectile = IsOfClass(pen, "Projectile");
    const BOOL bCannonBall = !bProjectile && IsOfClass(pen, "Cannon ball");
    if (!bProjectile && !bCannonBall) continue;

    CEntity *penLauncher = PropEntity(pen, PROP(bProjectile ? CLASS_PROJECTILE : CLASS_CANNONBALL, 1));
    if (bEnemies ? (penLauncher == penPlayer || penLauncher == NULL || !bProjectile) : penLauncher != penPlayer) continue;

    BOOL bKnown = FALSE;
    for (size_t i = 0; i < _aTracked.size(); i++) {
      if ((CEntity *)_aTracked[i].pen == pen) { bKnown = TRUE; break; }
    }
    if (bKnown) continue;

    Tracked t;
    t.pen = pen;
    t.ulId = _ulNextId++;
    t.vLast = pen->GetPlacement().pl_PositionVector;
    if (bEnemies) {
      // a monster's shot (fireballs, lava bombs, ...): Minecraft's mobs in its way get hurt
      t.fRadius = 1.5f; t.fDamage = 15.0f; t.bBreak = FALSE; t.strKind = "monster";
    } else if (bCannonBall) {
      t.fRadius = 7.0f; t.fDamage = 250.0f; t.bBreak = TRUE; t.strKind = "cannonball";
    } else {
      switch (PropIndex(pen, PROP(CLASS_PROJECTILE, 2), PRT_ROCKET)) {
        case PRT_GRENADE:   t.fRadius = 5.0f; t.fDamage = 100.0f; t.bBreak = TRUE;  t.strKind = "grenade"; break;
        case PRT_FLAME:     t.fRadius = 1.5f; t.fDamage = 8.0f;   t.bBreak = FALSE; t.strKind = "flame"; break;
        case PRT_LASER_RAY: t.fRadius = 1.2f; t.fDamage = 20.0f;  t.bBreak = FALSE; t.strKind = "laser"; break;
        case PRT_ROCKET:    t.fRadius = 5.0f; t.fDamage = 100.0f; t.bBreak = TRUE;  t.strKind = "rocket"; break;
        default: continue; // something else (an enemy's projectile type)
      }
    }
    _aTracked.push_back(t);
  }
};

void OnProjectileHit(ULONG ulId) {
  _aulHit.push_back(ulId);
};

void Step(CEntity *penPlayer, void (*pfnSend)(const std::string &)) {
  // projectiles Minecraft caught in one of its mobs: it exploded there, so Sam's copy goes away without a second boom
  for (size_t h = 0; h < _aulHit.size(); h++) {
    for (size_t i = 0; i < _aTracked.size(); i++) {
      if (_aTracked[i].ulId != _aulHit[h]) continue;
      CEntity *pen = _aTracked[i].pen;
      if (pen != NULL && !(pen->GetFlags() & ENF_DELETED)) pen->Destroy();
      _aTracked.erase(_aTracked.begin() + i);
      break;
    }
  }
  _aulHit.clear();

  // projectiles in flight: when one is gone, it exploded where it was last
  for (INDEX i = (INDEX)_aTracked.size() - 1; i >= 0; i--) {
    Tracked &t = _aTracked[i];
    CEntity *pen = t.pen;

    if (pen == NULL || (pen->GetFlags() & ENF_DELETED)) {
      SendBoom(pfnSend, t);
      _aTracked.erase(_aTracked.begin() + i);
    } else {
      t.vLast = pen->GetPlacement().pl_PositionVector;
    }
  }

  // where they are now, for Minecraft to catch them in its mobs
  if (!_aTracked.empty()) {
    std::string str = "{\"t\":\"proj\",\"l\":[";
    for (size_t i = 0; i < _aTracked.size(); i++) {
      const Tracked &t = _aTracked[i];
      char strOne[160];
      _snprintf(strOne, sizeof(strOne), "%s[%u,%.3f,%.3f,%.3f,%.1f,%.1f,%d,%d]", i == 0 ? "" : ",", t.ulId,
        t.vLast(1), t.vLast(2), t.vLast(3), t.fRadius, t.fDamage, t.bBreak ? 1 : 0, strcmp(t.strKind, "flame") == 0 ? 1 : 0);
      strOne[sizeof(strOne) - 1] = 0;
      str += strOne;
    }
    str += "]}";
    pfnSend(str);
  }

  // the monsters' projectiles, five times a second (they live long enough)
  static ULONG ctScan = 0;
  if (penPlayer != NULL && (++ctScan % 4) == 0) FindProjectiles(penPlayer, TRUE);

  if (penPlayer == NULL || !(penPlayer->GetFlags() & ENF_ALIVE)) {
    _bHaveLast = FALSE;
    return;
  }

  CEntity *penWeapons = PropEntity(penPlayer, PROP(CLASS_PLAYER, 16));
  if (penWeapons == NULL) return;

  // a new weapons entity (respawn, level change): start counting afresh
  if (penWeapons != _penLastWeapons) {
    _penLastWeapons = penWeapons;
    _bHaveLast = FALSE;
  }

  INDEX aiNow[A_COUNT];
  for (INDEX a = 0; a < A_COUNT; a++) {
    aiNow[a] = PropIndex(penWeapons, PROP(CLASS_WEAPONS, _aulAmmoProp[a]), 0);
  }

  const INDEX iWeapon = PropIndex(penWeapons, PROP(CLASS_WEAPONS, 4), 0);
  const BOOL bFiring = PropIndex(penWeapons, PROP(CLASS_WEAPONS, 2), 0) != 0;

  // where the shots come from: the player's eyes, along the view
  const CPlacement3D plView = IWorld::GetViewpoint((CPlayerEntity *)penPlayer, FALSE);
  FLOATmatrix3D m;
  MakeRotationMatrix(m, plView.pl_OrientationAngle);
  const FLOAT3D vDir(-m(1, 3), -m(2, 3), -m(3, 3));
  const FLOAT3D vEye = plView.pl_PositionVector;

  // Sam's bullets really start at the gun (offset from the eyes, parallel to the view; the crosshair shows where they
  // land), and the weapons remember the last one's start (m_vBulletSource): shots go from there when it's sane
  FLOAT3D vGun = vEye;
  CEntityProperty *pepSource = IWorld::PropertyForId(LibClassHolder(penWeapons), PROP(CLASS_WEAPONS, 35));
  if (pepSource != NULL && pepSource->ep_eptType == CEntityProperty::EPT_FLOAT3D) {
    const FLOAT3D vSource = *(FLOAT3D *)((UBYTE *)penWeapons + pepSource->ep_slOffset);
    if ((vSource - vEye).Length() < 2.0f) vGun = vSource;
  }

  if (_bHaveLast && iWeapon == _iLastWeapon) {
    INDEX aiUsed[A_COUNT];
    for (INDEX a = 0; a < A_COUNT; a++) aiUsed[a] = Max(_aiLast[a] - aiNow[a], (INDEX)0);

    if (iWeapon == W_COLT || iWeapon == W_DOUBLECOLT) {
      if (aiUsed[A_COLT] > 0) SendShot(pfnSend, vGun, vDir, 20.0f, aiUsed[A_COLT], 0.3f);

    } else if (iWeapon == W_SINGLESHOTGUN) {
      if (aiUsed[A_SHELLS] > 0) SendShot(pfnSend, vGun, vDir, 10.0f, 7 * aiUsed[A_SHELLS], 4.0f);

    } else if (iWeapon == W_DOUBLESHOTGUN) {
      if (aiUsed[A_SHELLS] > 0) SendShot(pfnSend, vGun, vDir, 10.0f, 7 * aiUsed[A_SHELLS], 6.0f);

    } else if (iWeapon == W_TOMMYGUN || iWeapon == W_MINIGUN) {
      if (aiUsed[A_BULLETS] > 0) SendShot(pfnSend, vGun, vDir, 10.0f, aiUsed[A_BULLETS], 0.8f);

    } else if (iWeapon == W_SNIPER) {
      if (aiUsed[A_SNIPER] > 0) SendShot(pfnSend, vGun, vDir, 80.0f, aiUsed[A_SNIPER], 0.0f);
    }

    // anything that launches projectiles: look for the new ones
    if (aiUsed[A_ROCKETS] > 0 || aiUsed[A_GRENADES] > 0 || aiUsed[A_ELECTRICITY] > 0 || aiUsed[A_IRONBALLS] > 0 || aiUsed[A_NAPALM] > 0) {
      FindProjectiles(penPlayer, FALSE);
    }
  }

  // close range: the knife slashes twice a second, the chainsaw bites ten times
  if (_ctMeleeCooldown > 0) _ctMeleeCooldown--;
  if (bFiring && _ctMeleeCooldown == 0 && (iWeapon == W_KNIFE || iWeapon == W_CHAINSAW)) {
    const BOOL bSaw = iWeapon == W_CHAINSAW;
    char str[256];
    _snprintf(str, sizeof(str), "{\"t\":\"shot\",\"o\":[%.3f,%.3f,%.3f],\"d\":[%.4f,%.4f,%.4f],\"dmg\":%.1f,\"n\":3,\"spread\":15,\"range\":3}",
      vEye(1), vEye(2), vEye(3), vDir(1), vDir(2), vDir(3), bSaw ? 10.0f : 50.0f);
    str[sizeof(str) - 1] = 0;
    pfnSend(str);
    _ctMeleeCooldown = bSaw ? 2 : 10;
  }

  for (INDEX a = 0; a < A_COUNT; a++) _aiLast[a] = aiNow[a];
  _iLastWeapon = iWeapon;
  _bHaveLast = TRUE;
};

CTString Status(void) {
  CTString str;
  str.PrintF("weapons: %u rays and %u explosions sent, %d projectiles in flight, weapon %d", _ctShots, _ctBooms, (INDEX)_aTracked.size(), _iLastWeapon);
  return str;
};

}; // namespace
