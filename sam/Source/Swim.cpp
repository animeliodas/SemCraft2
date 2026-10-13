/* SemCraft 2 - Minecraft's water and lava move Serious Sam's player. See Swim.h. */
#include "StdH.h"
#include "Swim.h"
#include "Glide.h"
#include "Physics.h"

namespace swim {

// Minecraft 26.3 per tick (LivingEntity.travelInFluid, Entity.updateFluidHeightAndDoFluidPushing); both games tick 20
// times a second. Sam's player is quicker than Minecraft's, so his strokes are a bit stronger than its 0.02.
static const DOUBLE WATER_ACCEL = 0.035, LAVA_ACCEL = 0.02;
static const DOUBLE WATER_DRAG = 0.8, LAVA_DRAG = 0.5, VERTICAL_DRAG = 0.8;
static const DOUBLE WATER_SINK = 0.005, LAVA_SINK = 0.02; // gravity / 16, gravity / 4
static const DOUBLE SWIM_UP = 0.04, JUMP_OUT = 0.3, WATER_PUSH = 0.014, LAVA_PUSH = 0.0023;
// deeper than this the feet are off the bottom when jumping (Minecraft's fluidJumpThreshold)
static const FLOAT JUMP_THRESHOLD = 0.4f;
// falling into water: no faster than this (Minecraft resets the fall distance in water; Sam's landing would hurt)
static const DOUBLE MAX_SINK = 0.4;
// walking on the bottom: Sam's walk, slowed
static const FLOAT WALK_SCALE = 0.35f;
// news older than this many ticks: out of the fluid
static const ULONG STALE_TICKS = 10;

static FLOAT _fWater = 0, _fLava = 0;
static BOOL _bEyes = FALSE;
static FLOAT3D _vFlow(0, 0, 0);
static ULONG _ulNewsTick = 0, _ctTicks = 0;
static BOOL _bSwimming = FALSE, _bOnBottom = FALSE;
// the player's input (relative: x right, y up, z back; Sam's speeds), taken while swimming
static FLOAT3D _vInput(0, 0, 0);
// velocity control as in Glide.cpp: what the swim wanted, what Sam's physics got for it, Sam's own pull per tick
static physics::Driver _drSwim;
static ULONG _ctSwimTicks = 0, _ctMessages = 0;

void OnFluid(FLOAT fWater, FLOAT fLava, BOOL bEyes, const FLOAT3D &vFlow) {
  _fWater = fWater;
  _fLava = fLava;
  _bEyes = bEyes;
  _vFlow = vFlow;
  _ulNewsTick = _ctTicks;
  _ctMessages++;
};

BOOL InFluid(void) {
  return (_fWater > 0.0f || _fLava > 0.0f) && _ctTicks - _ulNewsTick <= STALE_TICKS;
};

void Reset(void) {
  _fWater = _fLava = 0;
  _bSwimming = _bOnBottom = FALSE;
  _drSwim.Release();
};

void OnAction(CPlayerAction &pa, BOOL bFresh) {
  if (!InFluid() || glide::Gliding()) return;
  const BOOL bJump = pa.pa_vTranslation(2) > 0.01f;

  if (_bOnBottom && !(bJump && _fWater + _fLava >= JUMP_THRESHOLD)) {
    // walking on the bottom (or jumping in shallow water): Sam's walk, slowed; the current carries him along
    const FLOAT fHeading = pa.pa_aRotation(1) * PI / 180.0f;
    const FLOAT3D vFwd(-sinf(fHeading), 0, -cosf(fHeading)), vRight(cosf(fHeading), 0, -sinf(fHeading));
    const FLOAT fPush = (FLOAT)((_fLava > 0.0f ? LAVA_PUSH : WATER_PUSH) / (1.0 - WATER_DRAG) * 20.0); // m/s, at its steady speed
    pa.pa_vTranslation(1) = pa.pa_vTranslation(1) * WALK_SCALE + (_vFlow % vRight) * fPush;
    pa.pa_vTranslation(3) = pa.pa_vTranslation(3) * WALK_SCALE - (_vFlow % vFwd) * fPush;
    return;
  }

  // swimming: the swim moves him
  if (bFresh) _vInput = pa.pa_vTranslation;
  pa.pa_vTranslation = FLOAT3D(0, 0, 0);
};

void Step(CEntity *penPlayer) {
  _ctTicks++;
  if (penPlayer == NULL || !(penPlayer->GetFlags() & ENF_ALIVE) || !(penPlayer->en_ulPhysicsFlags & EPF_MOVABLE)) {
    _bSwimming = _bOnBottom = FALSE;
    return;
  }
  CMovableEntity *pmen = (CMovableEntity *)penPlayer;
  const BOOL bOnGround = pmen->en_penReference != NULL;

  // Sam's own water is Sam's; gliding into water folds the wings first (Glide.cpp); a boat floats on it
  if (!InFluid() || glide::Gliding() || glide::InWater(penPlayer) || physics::Riding()) {
    if (_bSwimming) CPrintF("[SemCraft2] out of Minecraft's %s\n", _fLava > 0.0f ? "lava" : "water");
    _bSwimming = _bOnBottom = FALSE;
    _drSwim.Release();
    return;
  }

  const BOOL bLava = _fLava > 0.0f && _fLava >= _fWater;
  const BOOL bJump = _vInput(2) > 0.01f, bDive = _vInput(2) < -0.01f;
  _bOnBottom = bOnGround;
  if (bOnGround && !(bJump && _fWater + _fLava >= JUMP_THRESHOLD)) {
    _drSwim.Release(); // Sam's walk (OnAction slowed it)
    return;
  }

  if (!_bSwimming) {
    _bSwimming = TRUE;
    _ctSwimTicks = 0;
    CPrintF("[SemCraft2] in Minecraft's %s\n", bLava ? "lava" : "water");
  }
  _ctSwimTicks++;

  // the velocity Sam's physics left (blocks per tick); nothing hit: Sam only added its pull, which is measured and taken
  // out (as in Glide.cpp)
  // (blocked: a wall, or the bottom's edge: maybe a ledge to climb out on)
  BOOL bBlocked;
  const FLOAT3D vFrom = _drSwim.From(pmen, !bOnGround, bBlocked) / 20.0f;
  DOUBLE vx = vFrom(1), vy = vFrom(2), vz = vFrom(3);

  // strokes: along the heading (Minecraft's moveRelative), input as fractions of Sam's speeds
  const FLOAT fHeading = penPlayer->GetPlacement().pl_OrientationAngle(1) * PI / 180.0f;
  const DOUBLE fx = -sin(fHeading), fz = -cos(fHeading), rx = cos(fHeading), rz = -sin(fHeading);
  DOUBLE dFwd = Clamp(-_vInput(3) / 10.0, -1.0, 1.0), dSide = Clamp(_vInput(1) / 10.0, -1.0, 1.0);
  const DOUBLE dLen = sqrt(dFwd * dFwd + dSide * dSide);
  if (dLen > 1.0) { dFwd /= dLen; dSide /= dLen; }
  const DOUBLE dAccel = bLava ? LAVA_ACCEL : WATER_ACCEL;
  vx += (fx * dFwd + rx * dSide) * dAccel;
  vz += (fz * dFwd + rz * dSide) * dAccel;
  if (bJump) vy += SWIM_UP;
  if (bDive) vy -= SWIM_UP;

  // the current
  const DOUBLE dPush = bLava ? LAVA_PUSH : WATER_PUSH;
  vx += _vFlow(1) * dPush;
  vy += _vFlow(2) * dPush;
  vz += _vFlow(3) * dPush;

  // drag, then the slow sink (not while swimming up)
  const DOUBLE dDrag = bLava ? LAVA_DRAG : WATER_DRAG;
  vx *= dDrag;
  vy *= VERTICAL_DRAG;
  vz *= dDrag;
  if (!bJump) vy -= bLava ? LAVA_SINK : WATER_SINK;
  if (vy < -MAX_SINK) vy = -MAX_SINK;

  // against a wall with jump held: out onto the ledge (Minecraft's jump out of water)
  if (bJump && bBlocked && _fWater + _fLava < 1.2f) vy = JUMP_OUT;

  _drSwim.Set(pmen, FLOAT3D((FLOAT)vx, (FLOAT)vy, (FLOAT)vz) * 20.0f, !bOnGround);
};

CTString Status(void) {
  CTString str;
  str.PrintF("fluid: %s%s%s water %.2f lava %.2f%s, %u messages, flow (%.2f %.2f %.2f)", InFluid() ? "in" : "out",
    _bSwimming ? " SWIMMING" : "", _bOnBottom ? " on the bottom" : "", _fWater, _fLava, _bEyes ? " eyes under" : "", _ctMessages,
    _vFlow(1), _vFlow(2), _vFlow(3));
  return str;
};

}; // namespace
