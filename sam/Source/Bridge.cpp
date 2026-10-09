/* SemCraft 2 - the Serious Sam half of the bridge. See Bridge.h and docs/CONTRACT.md. */
#include "StdH.h"
#include "Bridge.h"
#include "Link.h"
#include "Json.h"
#include "LevelExport.h"
#include "Weapons.h"
#include "Actors.h"
#include "Blocks.h"

#include <Engine/World/WorldRayCasting.h>
#include <string>
#include <deque>
#include <vector>
#include <stdio.h>

namespace bridge {

#ifdef SEMCRAFT_GAME_TFE
  static const char *GAME = "TFE";
  static const INDEX PLACT_SELECT_WEAPON_SHIFT = 9;
#else
  static const char *GAME = "TSE";
  static const INDEX PLACT_SELECT_WEAPON_SHIFT = 14;
#endif

// Player action buttons (Player.es)
static const ULONG PLACT_FIRE        = (1UL << 0);
static const ULONG PLACT_WEAPON_NEXT = (1UL << 2);
static const ULONG PLACT_WEAPON_PREV = (1UL << 3);
static const ULONG PLACT_WEAPON_FLIP = (1UL << 4);
static const ULONG PLACT_USE         = (1UL << 5);
static const ULONG PLACT_SELECT_WEAPON_MASK = (0x1FUL << PLACT_SELECT_WEAPON_SHIFT);

static const int PORT = 25610;

// A hit from Minecraft waiting for the next simulation tick.
struct Hurt {
  FLOAT fDamage;
  FLOAT3D vFrom;
  INDEX iType; // DamageType
};

static std::deque<Hurt> _aHurts;
// Minecraft explosions' knockback for Sam's player, m/s (the next simulation tick gives it)
static std::deque<FLOAT3D> _avPushes;
static levelx::Exported _level;
static BOOL _bLevelSent = FALSE;
static INDEX _iLinkGeneration = -1;
static BOOL _bBuild = FALSE;
static INDEX _iShowWeaponBefore = -1;
static ULONG _ulCamFrame = 0;
static ULONG _ctTicks = 0;
static BOOL _bLoggedFrustum = FALSE;
static CTString _strLastLevelReady = "";

// input edges
static BOOL _bKeyB = FALSE, _bLMB = FALSE, _bRMB = FALSE, _bKeyQ = FALSE;
static BOOL _abDigits[9] = { FALSE };

static void Send(const std::string &str) {
  link::Send(str);
};

void SendRaw(const char *strJson) {
  Send(strJson);
};

static CEntity *LocalPlayer(void) {
  CPlayerEntities cPlayers;
  IWorld::GetLocalPlayers(cPlayers);
  if (cPlayers.Count() == 0) return NULL;

  CEntity *pen = cPlayers.Pointer(0);
  if (pen == NULL || (pen->GetFlags() & ENF_DELETED)) return NULL;
  return pen;
};

// Is Sam's window in front and the game being played (no menu, console or NETRICSA over it)?
static BOOL Playing(void) {
  HWND hwnd = GetForegroundWindow();
  DWORD dwPid = 0;
  GetWindowThreadProcessId(hwnd, &dwPid);
  if (dwPid != GetCurrentProcessId()) return FALSE;

  CGameAPI *pGame = GetGameAPI();
  return pGame->IsGameOn() && !pGame->IsMenuOn() && pGame->GetConState() == 0 && pGame->GetCompState() == 0;
};

void Startup(void) {
  link::Start("127.0.0.1", PORT);
};

void Shutdown(void) {
  SetBuildMode(FALSE);
  link::Stop();
};

// ---------------------------------------------------------------------------------------------------------
// Camera
// ---------------------------------------------------------------------------------------------------------
void OnView(CPerspectiveProjection3D &ppr, CEntity *penViewer) {
  if (!link::Connected()) return;

  const FLOAT fTanL = ppr.pr_plClipL(3) / ppr.pr_plClipL(1);
  const FLOAT fTanR = ppr.pr_plClipR(3) / ppr.pr_plClipR(1);
  const FLOAT fTanT = ppr.pr_plClipU(3) / ppr.pr_plClipU(2);
  const FLOAT fTanB = ppr.pr_plClipD(3) / ppr.pr_plClipD(2);

  if (!_bLoggedFrustum) {
    _bLoggedFrustum = TRUE;
    CPrintF("[SemCraft2] Sam frustum tangents: L %.4f R %.4f B %.4f T %.4f, near %.3f, far %.1f, depth range %.2f-%.2f\n",
      fTanL, fTanR, fTanB, fTanT, ppr.pr_NearClipDistance, ppr.pr_FarClipDistance, ppr.pr_fDepthBufferNear, ppr.pr_fDepthBufferFar);
  }

  const FLOAT fVFov = (atanf(fTanT) - atanf(fTanB)) * 180.0f / PI;
  const FLOAT fAspect = (fTanR - fTanL) / (fTanT - fTanB);
  const FLOAT2D vScreen = ppr.pr_ScreenBBox.Size();
  if (!_finite(fVFov) || !_finite(fAspect) || fVFov <= 1.0f || fVFov >= 179.0f || fAspect <= 0.05f) return;

  const CPlacement3D &plView = ppr.pr_ViewerPlacement;
  FLOAT3D vFeet = plView.pl_PositionVector;
  CEntity *penPlayer = LocalPlayer();
  if (penPlayer != NULL) vFeet = penPlayer->GetLerpedPlacement().pl_PositionVector;

  char str[512];
  _snprintf(str, sizeof(str),
    "{\"t\":\"cam\",\"f\":%u,\"p\":[%.4f,%.4f,%.4f],\"r\":[%.4f,%.4f,%.4f],\"vfov\":%.4f,\"aspect\":%.5f,\"w\":%d,\"h\":%d,"
    "\"near\":%.4f,\"pl\":[%.4f,%.4f,%.4f]}",
    ++_ulCamFrame, plView.pl_PositionVector(1), plView.pl_PositionVector(2), plView.pl_PositionVector(3),
    (FLOAT)plView.pl_OrientationAngle(1), (FLOAT)plView.pl_OrientationAngle(2), (FLOAT)plView.pl_OrientationAngle(3),
    fVFov, fAspect, (INDEX)vScreen(1), (INDEX)vScreen(2), ppr.pr_NearClipDistance, vFeet(1), vFeet(2), vFeet(3));
  str[sizeof(str) - 1] = 0;
  Send(str);
};

// ---------------------------------------------------------------------------------------------------------
// Level
// ---------------------------------------------------------------------------------------------------------
static void SendLevel(void) {
  if (!_level.bValid || !link::Connected()) return;

  std::string str = "{\"t\":\"level\",\"name\":\"" + json::Escape(_level.strName.c_str()) + "\",\"file\":\""
    + json::Escape(_level.strFile.c_str()) + "\",\"hash\":\"" + _level.strHash + "\",\"tris\":" + std::to_string(_level.ctTris) + "}";
  _bLevelSent = link::Send(str);
  CPrintF("[SemCraft2] level announced to Minecraft: %s (%s)\n", _level.strName.c_str(), _bLevelSent ? "sent" : "send failed");
};

void OnWorldLoad(CWorld *pwo, const CTFileName &fnmWorld) {
  // only the game's world (not menu backgrounds)
  if (pwo != IWorld::GetWorld()) return;

  _level = levelx::Export(pwo, fnmWorld);
  _bLevelSent = FALSE;
  weapons::Reset();
  actors::Reset();
  blocks::Reset(TRUE);
  SendLevel();
};

void OnGameStop(void) {
  SetBuildMode(FALSE);
  _aHurts.clear();
  _avPushes.clear();
  weapons::Reset();
  actors::Reset();
  blocks::Reset(FALSE);
};

// ---------------------------------------------------------------------------------------------------------
// Build mode and input
// ---------------------------------------------------------------------------------------------------------
BOOL BuildMode(void) {
  return _bBuild;
};

void SetBuildMode(BOOL bOn) {
  bOn = !!bOn;
  if (bOn == _bBuild) return;
  _bBuild = bOn;

  // Sam's weapon goes away while Minecraft's hand is out
  CShellSymbol *pssShowWeapon = _pShell->GetSymbol("hud_bShowWeapon", TRUE);
  INDEX *piShowWeapon = (pssShowWeapon != NULL) ? (INDEX *)pssShowWeapon->ss_pvValue : NULL;

  if (piShowWeapon != NULL) {
    if (bOn) {
      _iShowWeaponBefore = *piShowWeapon;
      *piShowWeapon = 0;
    } else if (_iShowWeaponBefore >= 0) {
      *piShowWeapon = _iShowWeaponBefore;
      _iShowWeaponBefore = -1;
    }
  }

  // release whatever Minecraft holds
  if (_bLMB) { Send("{\"t\":\"key\",\"k\":\"attack\",\"down\":false}"); _bLMB = FALSE; }
  if (_bRMB) { Send("{\"t\":\"key\",\"k\":\"use\",\"down\":false}"); _bRMB = FALSE; }

  Send(bOn ? "{\"t\":\"build\",\"on\":true}" : "{\"t\":\"build\",\"on\":false}");
  CPrintF("[SemCraft2] build mode %s\n", bOn ? "ON: mouse and 1-9 act in Minecraft" : "off: Sam's weapons");
};

static BOOL KeyDown(int iVK) {
  return (GetAsyncKeyState(iVK) & 0x8000) != 0;
};

static void PollInput(void) {
  const BOOL bPlaying = Playing();

  // B toggles build mode
  const BOOL bKeyB = bPlaying && KeyDown('B');
  if (bKeyB && !_bKeyB) SetBuildMode(!_bBuild);
  _bKeyB = bKeyB;

  const BOOL bActive = bPlaying && _bBuild;
  const BOOL bLMB = bActive && KeyDown(VK_LBUTTON);
  const BOOL bRMB = bActive && KeyDown(VK_RBUTTON);

  if (bLMB != _bLMB) Send(bLMB ? "{\"t\":\"key\",\"k\":\"attack\",\"down\":true}" : "{\"t\":\"key\",\"k\":\"attack\",\"down\":false}");
  if (bRMB != _bRMB) Send(bRMB ? "{\"t\":\"key\",\"k\":\"use\",\"down\":true}" : "{\"t\":\"key\",\"k\":\"use\",\"down\":false}");
  _bLMB = bLMB;
  _bRMB = bRMB;

  // Q drops the held item
  const BOOL bKeyQ = bActive && KeyDown('Q');
  if (bKeyQ && !_bKeyQ) {
    Send("{\"t\":\"key\",\"k\":\"drop\",\"down\":true}");
    Send("{\"t\":\"key\",\"k\":\"drop\",\"down\":false}");
  }
  _bKeyQ = bKeyQ;

  for (INDEX i = 0; i < 9; i++) {
    const BOOL bDigit = bActive && KeyDown('1' + i);
    if (bDigit && !_abDigits[i]) {
      char str[64];
      _snprintf(str, sizeof(str), "{\"t\":\"slot\",\"n\":%d}", i);
      Send(str);
    }
    _abDigits[i] = bDigit;
  }
};

INDEX sc_iTestFire = 0;
INDEX sc_iTestSelect = 0;
INDEX sc_iTestWalk = 0;
INDEX sc_iTestTurn = 0;
FLOAT sc_fTestTurnH = 0.0f, sc_fTestTurnP = 0.0f;
// the turn so far: actions carry the accumulated rotation, so the offset stays added from then on
static ANGLE3D _aTestTurn(0, 0, 0);

void OnPlayerAction(CPlayerAction &pa, BOOL bFresh) {
  // test hooks: hold fire for a number of actions, select a weapon by number (both work with Sam's window in the
  // background, where Sam doesn't read its own input)
  if (sc_iTestFire > 0) {
    sc_iTestFire--;
    pa.pa_ulButtons |= PLACT_FIRE;
  }
  if (bFresh && sc_iTestTurn > 0) {
    sc_iTestTurn--;
    _aTestTurn(1) += sc_fTestTurnH;
    _aTestTurn(2) += sc_fTestTurnP;
  }
  pa.pa_aRotation += _aTestTurn;

  if (sc_iTestWalk > 0) {
    sc_iTestWalk--;
    pa.pa_vTranslation = FLOAT3D(0.0f, 0.0f, -10.0f); // run forward
  }
  if (sc_iTestSelect > 0) {
    pa.pa_ulButtons = (pa.pa_ulButtons & ~PLACT_SELECT_WEAPON_MASK) | ((ULONG)(sc_iTestSelect & 0x1F) << PLACT_SELECT_WEAPON_SHIFT);
    sc_iTestSelect = 0;
  }

  if (!_bBuild) return;

  // the mouse and the number keys belong to Minecraft now
  pa.pa_ulButtons &= ~(PLACT_FIRE | PLACT_USE | PLACT_WEAPON_NEXT | PLACT_WEAPON_PREV | PLACT_WEAPON_FLIP | PLACT_SELECT_WEAPON_MASK);
};

// ---------------------------------------------------------------------------------------------------------
// Messages from Minecraft
// ---------------------------------------------------------------------------------------------------------
// The player is its own "inflictor" (the engine's world entities aren't safe to stand in), so the type must be one
// Player.es doesn't ignore for self-damage: not CLOSERANGE, and not EXPLOSION/PROJECTILE on easy difficulties.
static INDEX DamageTypeFor(const std::string &strSource) {
  if (strSource.find("fire") != std::string::npos || strSource.find("lava") != std::string::npos) {
    return DMT_BURNING;
  }
  return DMT_IMPACT;
};

// Structured exceptions from entity code must not take the game down: the hit is dropped and the bridge says so.
static BOOL SafeInflict(CEntity *penPlayer, INDEX iType, FLOAT fDamage, const FLOAT3D &vHit, const FLOAT3D &vDir) {
  __try {
    penPlayer->InflictDirectDamage(penPlayer, penPlayer, (DamageType)iType, fDamage, vHit, vDir);
    return TRUE;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return FALSE;
  }
};

static BOOL SafePush(CEntity *penPlayer, const FLOAT3D &vSpeed) {
  __try {
    ((CMovableEntity *)penPlayer)->GiveImpulseTranslationAbsolute(vSpeed);
    return TRUE;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return FALSE;
  }
};

static void Handle(const std::string &strMsg) {
  std::string strType;
  if (!json::GetString(strMsg, "t", strType)) return;

  if (strType == "hurt") {
    double dDamage = 0;
    double adFrom[3] = { 0, 0, 0 };
    std::string strSource;
    json::GetNumber(strMsg, "d", dDamage);
    json::GetString(strMsg, "src", strSource);
    const INDEX ct = json::GetNumbers(strMsg, "from", adFrom, 3);

    Hurt h;
    h.fDamage = (FLOAT)dDamage;
    h.vFrom = FLOAT3D((FLOAT)adFrom[0], (FLOAT)adFrom[1], (FLOAT)adFrom[2]);
    h.iType = DamageTypeFor(strSource);
    if (ct < 3) h.iType = DMT_CLOSERANGE;
    if (_aHurts.size() < 64) _aHurts.push_back(h);

  } else if (strType == "blocks") {
    // Minecraft's solid blocks near the player, as Sam coordinates of their minimum corners
    static std::vector<double> adSet(30000), adClear(30000);
    const INDEX ctSet = json::GetNumbers(strMsg, "set", &adSet[0], (int)adSet.size());
    const INDEX ctClear = json::GetNumbers(strMsg, "clear", &adClear[0], (int)adClear.size());
    blocks::OnBlocks(&adSet[0], ctSet, &adClear[0], ctClear);
    static INDEX ctLogged = 0;
    if (ctLogged++ < 20) CPrintF("[SemCraft2] Minecraft blocks: %d solid, %d gone\n", ctSet / 3, ctClear / 3);

  } else if (strType == "actorhit") {
    double dId = 0, dDamage = 0;
    bool bBoom = false;
    json::GetNumber(strMsg, "id", dId);
    json::GetNumber(strMsg, "d", dDamage);
    json::GetBool(strMsg, "boom", bBoom);
    if (dDamage > 0) actors::OnHit((ULONG)dId, (FLOAT)dDamage, bBoom);

  } else if (strType == "push") {
    double adV[3] = { 0, 0, 0 };
    if (json::GetNumbers(strMsg, "v", adV, 3) == 3 && _avPushes.size() < 16) {
      _avPushes.push_back(FLOAT3D((FLOAT)adV[0], (FLOAT)adV[1], (FLOAT)adV[2]));
    }

  } else if (strType == "projhit") {
    double dId = 0;
    if (json::GetNumber(strMsg, "id", dId)) weapons::OnProjectileHit((ULONG)dId);

  } else if (strType == "hello") {
    CPrintF("^c00ff00[SemCraft2]^r Minecraft linked\n");

  } else if (strType == "level_ready") {
    double dBlocks = 0;
    json::GetNumber(strMsg, "blocks", dBlocks);
    CPrintF("^c00ff00[SemCraft2]^r Minecraft built the level: %d shell blocks\n", (INDEX)dBlocks);

  } else if (strType == "log") {
    std::string strText;
    if (json::GetString(strMsg, "m", strText)) CPrintF("[Minecraft] %s\n", strText.c_str());
  }
};

void OnFrame(void) {
  // a new connection: say hello and tell it where we are
  const INDEX iGeneration = link::Generation();
  if (iGeneration != _iLinkGeneration && link::Connected()) {
    _iLinkGeneration = iGeneration;
    char str[128];
    _snprintf(str, sizeof(str), "{\"t\":\"hello\",\"v\":1,\"game\":\"%s\",\"pid\":%u}", GAME, (ULONG)GetCurrentProcessId());
    Send(str);
    _bLevelSent = FALSE;
    if (_bBuild) Send("{\"t\":\"build\",\"on\":true}");
  }

  if (!_bLevelSent) SendLevel();

  std::string strMsg;
  for (INDEX i = 0; i < 256 && link::Poll(strMsg); i++) {
    Handle(strMsg);
  }

  PollInput();
};

static BOOL _bHurtBroken = FALSE;

void OnStep(void) {
  _ctTicks++;
  CEntity *penPlayer = LocalPlayer();

  // Minecraft's hits land on Sam's player
  if (penPlayer != NULL && GetGameAPI()->IsGameOn() && !_bHurtBroken && (penPlayer->GetFlags() & ENF_ALIVE)) {
    const FLOAT3D vPlayer = penPlayer->GetPlacement().pl_PositionVector;

    while (!_aHurts.empty()) {
      const Hurt h = _aHurts.front();
      _aHurts.pop_front();

      FLOAT3D vDir = vPlayer - h.vFrom;
      if (vDir.Length() < 0.01f) vDir = FLOAT3D(0, 0, -1);
      vDir.Normalize();

      if (!SafeInflict(penPlayer, h.iType, h.fDamage, vPlayer + FLOAT3D(0, 1, 0), vDir)) {
        _bHurtBroken = TRUE;
        CPrintF("^cff0000[SemCraft2]^r applying a Minecraft hit crashed inside the game; Minecraft damage is off for this session\n");
        break;
      }
    }

    // and its explosions throw him
    while (!_avPushes.empty() && !_bHurtBroken) {
      const FLOAT3D v = _avPushes.front();
      _avPushes.pop_front();
      if (!SafePush(penPlayer, v)) {
        _bHurtBroken = TRUE;
        CPrintF("^cff0000[SemCraft2]^r pushing the player crashed inside the game; Minecraft damage is off for this session\n");
      }
    }
  } else {
    _aHurts.clear();
    _avPushes.clear();
  }

  // Sam's guns hurt Minecraft's mobs
  if (GetGameAPI()->IsGameOn() && link::Connected()) {
    weapons::Step(penPlayer, &Send);
    // and Sam's monsters are in Minecraft as stand-ins its mobs hunt
    actors::Step(penPlayer, &Send);
  }

  // and Minecraft's blocks are solid here
  if (GetGameAPI()->IsGameOn()) blocks::Step();

  // the player's state, 5 times a second
  if (_ctTicks % 4 == 0 && link::Connected()) {
    FLOAT fHealth = 0;
    BOOL bAlive = FALSE;
    if (penPlayer != NULL) {
      fHealth = ((CLiveEntity *)penPlayer)->en_fHealth;
      bAlive = fHealth > 0;
    }

    char str[160];
    _snprintf(str, sizeof(str), "{\"t\":\"state\",\"hp\":%.2f,\"alive\":%s,\"inGame\":%s,\"build\":%s}",
      fHealth, bAlive ? "true" : "false", GetGameAPI()->IsGameOn() ? "true" : "false", _bBuild ? "true" : "false");
    Send(str);
  }
};

// ---------------------------------------------------------------------------------------------------------
// Test hooks
// ---------------------------------------------------------------------------------------------------------
BOOL AimPoint(FLOAT3D &vHit) {
  CEntity *penPlayer = LocalPlayer();
  if (penPlayer == NULL) return FALSE;

  CPlacement3D plView = IWorld::GetViewpoint((CPlayerEntity *)penPlayer, TRUE);
  FLOATmatrix3D m;
  MakeRotationMatrix(m, plView.pl_OrientationAngle);
  const FLOAT3D vDir(-m(1, 3), -m(2, 3), -m(3, 3)); // view looks down -z

  CCastRay cr(penPlayer, plView.pl_PositionVector, plView.pl_PositionVector + vDir * 300.0f);
  cr.cr_ttHitModels = CCastRay::TT_NONE;
  cr.cr_bHitTranslucentPortals = TRUE;
  IWorld::GetWorld()->CastRay(cr);

  if (cr.cr_penHit == NULL) return FALSE;
  vHit = cr.cr_vHit;
  return TRUE;
};

CTString Status(void) {
  CTString str;
  str.PrintF("link: %s (generation %d), level: %s (%d tris, %s), build: %s, cam frames sent: %u; %s",
    link::Connected() ? "connected" : "waiting for Minecraft on 127.0.0.1:25610", link::Generation(),
    _level.bValid ? _level.strName.c_str() : "none", _level.ctTris, _bLevelSent ? "sent" : "not sent",
    _bBuild ? "on" : "off", _ulCamFrame, (weapons::Status() + "; " + actors::Status() + "; " + blocks::Status()).str_String);
  return str;
};

}; // namespace
