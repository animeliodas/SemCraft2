/* SemCraft 2 - Minecraft's elytra on Serious Sam's player. See Glide.h. */
#include "StdH.h"
#include "Glide.h"

namespace glide {

// Minecraft's numbers (LivingEntity fall flying, FireworkRocketEntity), per tick; both games tick 20 times a second
static const DOUBLE GRAVITY = 0.08;
static const INDEX BOOST_TICKS = 22; // a flight-duration-1 rocket

static BOOL _bElytra = FALSE, _bFirework = FALSE;
static BOOL _bGliding = FALSE;
static BOOL _bJumpWasDown = FALSE, _bJumpPressed = FALSE;
static ULONG _ulPhysicsBefore = 0;
static INDEX _ctBoost = 0;
static ULONG _ctGlides = 0, _ctBoosts = 0, _ctTicks = 0;
static DOUBLE _dDistance = 0, _dDrop = 0;
static FLOAT3D _vStart(0, 0, 0);

INDEX sc_iTestJump = 0;
INDEX sc_iGlideLog = 0;
// what the glide wanted Sam to move at (Minecraft's velocity) and what it gave Sam's physics for that, last tick
static FLOAT3D _vSet(0, 0, 0), _vSent(0, 0, 0);
static BOOL _bHaveSet = FALSE;
// Sam's own pull on the player per tick (blocks/tick; its gravity keeps working on a player), measured while gliding
static DOUBLE _dSamPull = 0;

void OnGear(BOOL bElytra, BOOL bFirework) {
  _bElytra = bElytra;
  _bFirework = bFirework;
};

BOOL Gliding(void) { return _bGliding; };
BOOL FireworkInHand(void) { return _bFirework; };

void Reset(void) {
  _bGliding = FALSE;
  _ctBoost = 0;
  _bJumpPressed = FALSE;
};

void OnAction(CPlayerAction &pa, BOOL bFresh) {
  if (sc_iTestJump > 0) {
    if (bFresh) sc_iTestJump--;
    pa.pa_vTranslation(2) = 10.0f;
  }
  if (bFresh) {
    const BOOL bJump = pa.pa_vTranslation(2) > 0.01f;
    if (bJump && !_bJumpWasDown) _bJumpPressed = TRUE;
    _bJumpWasDown = bJump;
  }
  // gliding: no walking, no jumping in the air; the look steers
  if (_bGliding) pa.pa_vTranslation = FLOAT3D(0, 0, 0);
};

BOOL Boost(void) {
  if (!_bGliding) return FALSE;
  _ctBoost = BOOST_TICKS;
  _ctBoosts++;
  return TRUE;
};

static void Stop(CMovableEntity *pmen) {
  if (!_bGliding) return;
  _bGliding = FALSE;
  _ctBoost = 0;
  if (pmen != NULL) {
    pmen->en_ulPhysicsFlags = (pmen->en_ulPhysicsFlags & ~EPF_TRANSLATEDBYGRAVITY) | (_ulPhysicsBefore & EPF_TRANSLATEDBYGRAVITY);
    const FLOAT3D v = pmen->GetPlacement().pl_PositionVector;
    _dDistance = sqrt((v(1) - _vStart(1)) * (v(1) - _vStart(1)) + (v(3) - _vStart(3)) * (v(3) - _vStart(3)));
    _dDrop = _vStart(2) - v(2);
  }
  CPrintF("[SemCraft2] elytra folded: %.1f m flown, %.1f m down\n", _dDistance, _dDrop);
};

void Step(CEntity *penPlayer) {
  _ctTicks++;
  if (penPlayer == NULL || !(penPlayer->GetFlags() & ENF_ALIVE) || !(penPlayer->en_ulPhysicsFlags & EPF_MOVABLE)) {
    _bGliding = FALSE;
    _bJumpPressed = FALSE;
    return;
  }
  CMovableEntity *pmen = (CMovableEntity *)penPlayer;
  const BOOL bOnGround = pmen->en_penReference != NULL;

  if (!_bGliding) {
    // a jump pressed while falling, with an elytra on: the wings open
    if (_bJumpPressed && _bElytra && !bOnGround) {
      _bGliding = TRUE;
      _ulPhysicsBefore = pmen->en_ulPhysicsFlags;
      pmen->en_ulPhysicsFlags &= ~EPF_TRANSLATEDBYGRAVITY;
      _vStart = pmen->GetPlacement().pl_PositionVector;
      _bHaveSet = FALSE;
      _ctGlides++;
      CPrintF("[SemCraft2] elytra open\n");
    }
    _bJumpPressed = FALSE;
    if (!_bGliding) return;
  }
  _bJumpPressed = FALSE;

  if (bOnGround || !_bElytra) {
    Stop(pmen);
    return;
  }

  // where Sam looks (Sam's axes are Minecraft's: y up, looking along -z at heading 0)
  const CPlacement3D plView = IWorld::GetViewpoint((CPlayerEntity *)penPlayer, FALSE);
  FLOATmatrix3D m;
  MakeRotationMatrix(m, plView.pl_OrientationAngle);
  const DOUBLE lx = -m(1, 3), ly = -m(2, 3), lz = -m(3, 3);

  // the velocity Sam's physics left after last tick (collisions included), in metres (blocks) per tick
  const FLOAT3D vNow = pmen->en_vCurrentTranslationAbsolute;
  DOUBLE vx = vNow(1) / 20.0, vy = vNow(2) / 20.0, vz = vNow(3) / 20.0;
  // nothing hit (the horizontal velocity is what was given): Sam's physics only added its own gravity, which is measured
  // and taken out, and the glide goes on from its own velocity; after a hit it goes on from what Sam's physics left
  if (_bHaveSet && fabs(vx - _vSent(1)) < 0.002 && fabs(vz - _vSent(3)) < 0.002) {
    _dSamPull = vy - _vSent(2);
    vx = _vSet(1);
    vy = _vSet(2);
    vz = _vSet(3);
  }
  if (sc_iGlideLog > 0 && (_ctTicks % 5) == 0) {
    sc_iGlideLog--;
    CPrintF("[SemCraft2] glide: set (%.3f %.3f %.3f) last tick, now (%.3f %.3f %.3f) b/t, y %.2f\n", _vSet(1), _vSet(2), _vSet(3),
      vx, vy, vz, pmen->GetPlacement().pl_PositionVector(2));
  }

  // LivingEntity.updateFallFlyingMovement, Minecraft 26.3 (pitch: Minecraft's xRot, positive = down)
  const DOUBLE dPitch = -asin(Clamp(ly, -1.0, 1.0));
  const DOUBLE dHorLook = sqrt(lx * lx + lz * lz);
  const DOUBLE dHorVel = sqrt(vx * vx + vz * vz);
  DOUBLE dCos = cos(dPitch);
  dCos = dCos * dCos; // * min(1, |look| / 0.4) with |look| = 1
  vy += GRAVITY * (-1.0 + dCos * 0.75);
  if (vy < 0.0 && dHorLook > 0.0) {
    const DOUBLE k = vy * -0.1 * dCos;
    vx += lx * k / dHorLook;
    vy += k;
    vz += lz * k / dHorLook;
  }
  if (dPitch < 0.0 && dHorLook > 0.0) {
    const DOUBLE k = dHorVel * -sin(dPitch) * 0.04;
    vx += -lx * k / dHorLook;
    vy += k * 3.2;
    vz += -lz * k / dHorLook;
  }
  if (dHorLook > 0.0) {
    vx += (lx / dHorLook * dHorVel - vx) * 0.1;
    vz += (lz / dHorLook * dHorVel - vz) * 0.1;
  }
  vx *= 0.99;
  vy *= 0.98;
  vz *= 0.99;

  // a firework: FireworkRocketEntity pulls the flyer along the look
  if (_ctBoost > 0) {
    _ctBoost--;
    vx += lx * 0.1 + (lx * 1.5 - vx) * 0.5;
    vy += ly * 0.1 + (ly * 1.5 - vy) * 0.5;
    vz += lz * 0.1 + (lz * 1.5 - vz) * 0.5;
  }

  // Sam's physics will pull the player down by its gravity this tick: give it that much more to start with
  _vSet = FLOAT3D((FLOAT)vx, (FLOAT)vy, (FLOAT)vz);
  _vSent = FLOAT3D((FLOAT)vx, (FLOAT)(vy - _dSamPull), (FLOAT)vz);
  _bHaveSet = TRUE;
  pmen->en_vCurrentTranslationAbsolute = _vSent * 20.0f;
  // Sam's physics steers a player for a moment after a jump (towards the input, which is nothing while gliding): the
  // jump that opened the wings must not brake the glide
  pmen->en_tmJumped = -1000.0f;
  pmen->AddToMovers();
};

CTString Status(void) {
  CTString str;
  str.PrintF("elytra: %s%s%s, %u glides, %u boosts, last %.1f m flown / %.1f m down", _bElytra ? "worn" : "none",
    _bFirework ? " (firework in hand)" : "", _bGliding ? " GLIDING" : "", _ctGlides, _ctBoosts, _dDistance, _dDrop);
  return str;
};

}; // namespace
