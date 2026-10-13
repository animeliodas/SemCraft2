/* SemCraft 2 - the Serious Sam half of the bridge. See Bridge.h and docs/CONTRACT.md. */
#include "StdH.h"
#include "Bridge.h"
#include "Link.h"
#include "Json.h"
#include "LevelExport.h"
#include "Weapons.h"
#include "Actors.h"
#include "Blocks.h"
#include "Movers.h"
#include "Glide.h"
#include "Swim.h"
#include "Player.h"
#include "Physics.h"

#include <Engine/World/WorldRayCasting.h>
#include <Engine/Base/Input.h>
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

// Player.es state: the SeriousBridge core (Player.h)
using player::PROP_ARMOR;
using player::PROP_TOPHEALTH;
using player::PROP_VIEWSTATE;
using player::PROP_POWERUPS;
using player::PROP_SERIOUSBOMBS;
static void *PlayerProp(CEntity *pen, ULONG ulID) { return player::Prop(pen, ulID); };
static FLOAT PlayerFloat(CEntity *pen, ULONG ulID, FLOAT fDefault) { return player::PropFloat(pen, ulID, fDefault); };
static INDEX PlayerIndex(CEntity *pen, ULONG ulID, INDEX iDefault) { return player::PropIndex(pen, ulID, iDefault); };
FLOAT PowerupLeft(CEntity *penPlayer, INDEX iPowerup) { return player::PowerupLeft(penPlayer, iPowerup); };

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
// (Minecraft's knockback, pistons and ender pearls move Sam's player through the core's physics: Physics.h)
// score for Minecraft mobs Sam killed, given on the next simulation tick
static INDEX _ctScoreDue = 0;
// a level was entered: Minecraft's world is to be reset ("all" or "world"), sent once Minecraft is there
static std::string _strPendingReset;
static levelx::Exported _level;
static BOOL _bLevelSent = FALSE;
static INDEX _iLinkGeneration = -1;
static BOOL _bBuild = FALSE;
// a Minecraft screen (the inventory) is open over Sam: the mouse moves its cursor, keys and clicks go to it
static BOOL _bScreen = FALSE;
static FLOAT _fCursorX = 0.5f, _fCursorY = 0.5f, _fCursorSentX = -1.0f, _fCursorSentY = -1.0f;
// view rotation the mouse made while a screen was open, taken back out of every action from then on
// (the view stays still while a screen is open, and the test hook turns it: the core's player::OnAction)
static BOOL _bKeyE = FALSE;
static BOOL _bBoostKey = FALSE;
static BOOL _abKeys[256];
// keys already down when a screen opened: letting go of them isn't for the screen
static BOOL _abHeldAtOpen[256];
static BOOL _bSlash = FALSE;
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

BOOL OnChat(const char *strText) {
  if (!link::Connected() || strText == NULL || strText[0] == 0) return FALSE;
  Send(std::string("{\"t\":\"chat\",\"m\":\"") + json::Escape(strText) + "\"}");
  return TRUE;
};

CEntity *LocalPlayer(void) {
  return player::Local();
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
  // third person: Minecraft draws Steve at the feet, looking where the player looks
  const BOOL bThirdPerson = penPlayer != NULL && PlayerIndex(penPlayer, PROP_VIEWSTATE, 0) == 3;
  ANGLE3D aLook = plView.pl_OrientationAngle;
  if (penPlayer != NULL) aLook = IWorld::GetViewpoint((CPlayerEntity *)penPlayer, TRUE).pl_OrientationAngle;

  char str[600];
  _snprintf(str, sizeof(str),
    "{\"t\":\"cam\",\"f\":%u,\"p\":[%.4f,%.4f,%.4f],\"r\":[%.4f,%.4f,%.4f],\"vfov\":%.4f,\"aspect\":%.5f,\"w\":%d,\"h\":%d,"
    "\"near\":%.4f,\"pl\":[%.4f,%.4f,%.4f],\"tp\":%s,\"pr\":[%.3f,%.3f]}",
    ++_ulCamFrame, plView.pl_PositionVector(1), plView.pl_PositionVector(2), plView.pl_PositionVector(3),
    (FLOAT)plView.pl_OrientationAngle(1), (FLOAT)plView.pl_OrientationAngle(2), (FLOAT)plView.pl_OrientationAngle(3),
    fVFov, fAspect, (INDEX)vScreen(1), (INDEX)vScreen(2), ppr.pr_NearClipDistance, vFeet(1), vFeet(2), vFeet(3),
    bThirdPerson && sc_iSteve ? "true" : "false", (FLOAT)aLook(1), (FLOAT)aLook(2));
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
  movers::ResendAll();
};

void OnWorldLoad(CWorld *pwo, const CTFileName &fnmWorld) {
  // only the game's world (not menu backgrounds)
  if (pwo != IWorld::GetWorld()) return;

  // a level change replaces the world without OnGameStop: what's still held of the old one is dropped, not released
  weapons::Abandon();
  actors::Abandon();
  blocks::Abandon();

  _level = levelx::Export(pwo, fnmWorld);
  _bLevelSent = FALSE;
  weapons::Reset();
  actors::Reset();
  blocks::Reset(TRUE);
  player::Resync(); // a new player entity
  physics::Reset();
  SendLevel();
};

void OnGameStop(void) {
  SetBuildMode(FALSE);
  player::Resync();
  physics::Reset();
  _aHurts.clear();
  weapons::Reset();
  actors::Reset();
  blocks::Reset(FALSE);
  movers::Reset(); // their entity pointers must go before the world does
  glide::Reset();
  swim::Reset();
};

// ---------------------------------------------------------------------------------------------------------
// Build mode and input
// ---------------------------------------------------------------------------------------------------------
BOOL BuildMode(void) {
  return _bBuild;
};

// 1: in third person Minecraft draws Steve where Sam's player is, and Sam's own model is hidden (0: Sam's model)
INDEX sc_iSteve = 1;
static CEntity *_apenHidden[2] = { NULL, NULL };

void HideSamForSteve(BOOL bHide) {
  if (!bHide) {
    for (INDEX i = 0; i < 2; i++) {
      if (_apenHidden[i] != NULL) _apenHidden[i]->en_RenderType = CEntity::RT_MODEL;
      _apenHidden[i] = NULL;
    }
    return;
  }
  CEntity *pen = LocalPlayer();
  if (!sc_iSteve || pen == NULL || !link::Connected() || !GetGameAPI()->IsGameOn() || PlayerIndex(pen, PROP_VIEWSTATE, 0) != 3) return;
  if (!(pen->GetFlags() & ENF_ALIVE)) return; // a dead Sam lies there as himself
  CEntity *apen[2] = { pen, pen->IsPredicted() ? pen->GetPredictionTail() : NULL };
  for (INDEX i = 0; i < 2; i++) {
    if (apen[i] != NULL && apen[i]->en_RenderType == CEntity::RT_MODEL) {
      apen[i]->en_RenderType = CEntity::RT_NONE;
      _apenHidden[i] = apen[i];
    }
  }
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

FLOAT sc_fCursorSpeed = 1.0f;

BOOL ScreenCursor(FLOAT &fX, FLOAT &fY) {
  fX = _fCursorX;
  fY = _fCursorY;
  return _bScreen;
};

// A Minecraft screen is over Sam from now on: the cursor starts in the middle, keys already down don't count, and
// Minecraft lets go of what build mode held.
static void EnterScreen(void) {
  _bScreen = TRUE;
  _fCursorX = _fCursorY = 0.5f;
  _fCursorSentX = _fCursorSentY = -1.0f;
  for (INDEX vk = 0; vk < 256; vk++) {
    _abKeys[vk] = (GetAsyncKeyState(vk) & 0x8000) != 0;
    _abHeldAtOpen[vk] = _abKeys[vk];
  }
  if (_bLMB) { Send("{\"t\":\"key\",\"k\":\"attack\",\"down\":false}"); _bLMB = FALSE; }
  if (_bRMB) { Send("{\"t\":\"key\",\"k\":\"use\",\"down\":false}"); _bRMB = FALSE; }
};

// Sam asks Minecraft for a screen: "inventory" (creative, every block and item) or "chat" (its commands work there,
// strText starts the line).
static void OpenScreen(const char *strWhat, const char *strText) {
  if (!link::Connected() || _bScreen) return;
  EnterScreen();
  std::string str = std::string("{\"t\":\"screen\",\"open\":\"") + strWhat + "\"";
  if (strText != NULL) str += std::string(",\"text\":\"") + json::Escape(strText) + "\"";
  Send(str + "}");
  CPrintF("[SemCraft2] Minecraft %s open: the mouse moves the cursor, %s closes it\n", strWhat,
    strcmp(strWhat, "chat") == 0 ? "Enter or Escape" : "E");
};

// Minecraft closed it (or Sam's menu came up): back to Sam
static void ScreenClosed(BOOL bTellMinecraft) {
  if (!_bScreen) return;
  _bScreen = FALSE;
  if (bTellMinecraft) Send("{\"t\":\"screen\",\"open\":\"\"}");
};

// The keys a screen wants besides text, as Minecraft 26.3 (SDL3) sees them: the scancode (USB usage id) and the
// keycode (SDL_Keycode; layout-independent here, as the shortcuts want). FALSE: not forwarded as a key.
static BOOL SdlKey(int iVK, int &iScan, int &iCode) {
  if (iVK >= 'A' && iVK <= 'Z') { iScan = 4 + iVK - 'A'; iCode = 'a' + iVK - 'A'; return TRUE; }
  if (iVK >= '1' && iVK <= '9') { iScan = 30 + iVK - '1'; iCode = iVK; return TRUE; }
  if (iVK == '0') { iScan = 39; iCode = '0'; return TRUE; }
  if (iVK >= VK_F1 && iVK <= VK_F12) { iScan = 58 + iVK - VK_F1; iCode = 0x40000000 | iScan; return TRUE; }
  switch (iVK) {
    case VK_RETURN: iScan = 40; iCode = 13; return TRUE;
    case VK_BACK:   iScan = 42; iCode = 8; return TRUE;
    case VK_TAB:    iScan = 43; iCode = 9; return TRUE;
    case VK_SPACE:  iScan = 44; iCode = 32; return TRUE;
    case VK_DELETE: iScan = 76; iCode = 127; return TRUE;
    case VK_INSERT: iScan = 73; break;
    case VK_HOME:   iScan = 74; break;
    case VK_PRIOR:  iScan = 75; break;
    case VK_END:    iScan = 77; break;
    case VK_NEXT:   iScan = 78; break;
    case VK_RIGHT:  iScan = 79; break;
    case VK_LEFT:   iScan = 80; break;
    case VK_DOWN:   iScan = 81; break;
    case VK_UP:     iScan = 82; break;
    default: return FALSE;
  }
  iCode = 0x40000000 | iScan; // SDLK_* of keys without a character
  return TRUE;
};

// While a screen is open: clicks, keys and the characters they type go to it (the layout is the keyboard's).
static void PollScreen(void) {
  // the mouse, every frame: Sam's input reads its movement (pixels times sensitivity, y up); the view stays frozen
  RECT rc;
  if (_pInput != NULL && GetClientRect(GetForegroundWindow(), &rc) && rc.right > 0 && rc.bottom > 0) {
    _fCursorX = Clamp(_fCursorX + _pInput->GetAxisValue(MOUSE_X_AXIS) * sc_fCursorSpeed / (FLOAT)rc.right, 0.0f, 1.0f);
    _fCursorY = Clamp(_fCursorY - _pInput->GetAxisValue(MOUSE_Y_AXIS) * sc_fCursorSpeed / (FLOAT)rc.bottom, 0.0f, 1.0f);
    const FLOAT fWheel = _pInput->GetAxisValue(3);
    if (fWheel != 0.0f) {
      char strWheel[64];
      _snprintf(strWheel, sizeof(strWheel), "{\"t\":\"scrollgui\",\"d\":%d}", fWheel > 0 ? 1 : -1);
      Send(strWheel);
    }
  }

  const BOOL bShift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
  const BOOL bCtrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
  const int iMods = (bShift ? 0x0001 : 0) | (bCtrl ? 0x0040 : 0); // SDL_KMOD_LSHIFT, SDL_KMOD_LCTRL

  if (_fCursorX != _fCursorSentX || _fCursorY != _fCursorSentY) {
    char str[96];
    _snprintf(str, sizeof(str), "{\"t\":\"cursor\",\"x\":%.4f,\"y\":%.4f}", _fCursorX, _fCursorY);
    Send(str);
    _fCursorSentX = _fCursorX;
    _fCursorSentY = _fCursorY;
  }

  BYTE abState[256];
  GetKeyboardState(abState);
  for (int vk = 1; vk < 256; vk++) {
    if (vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU || vk == VK_LSHIFT || vk == VK_RSHIFT || vk == VK_LCONTROL || vk == VK_RCONTROL) continue;
    const BOOL bDown = (GetAsyncKeyState(vk) & 0x8000) != 0;
    if (bDown == _abKeys[vk]) continue;
    _abKeys[vk] = bDown;
    char str[96];
    // held since before the screen opened: its release isn't the screen's
    if (!bDown && _abHeldAtOpen[vk]) {
      _abHeldAtOpen[vk] = FALSE;
      continue;
    }
    if (bDown) _abHeldAtOpen[vk] = FALSE;
    if (vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON) {
      const int iButton = vk == VK_LBUTTON ? 0 : (vk == VK_RBUTTON ? 1 : 2);
      _snprintf(str, sizeof(str), "{\"t\":\"click\",\"b\":%d,\"down\":%s,\"shift\":%s}", iButton, bDown ? "true" : "false", bShift ? "true" : "false");
      Send(str);
      continue;
    }
    if (!bDown) continue;
    int iScan = 0, iCode = 0;
    if (SdlKey(vk, iScan, iCode)) {
      _snprintf(str, sizeof(str), "{\"t\":\"keydown\",\"sc\":%d,\"kc\":%d,\"mods\":%d}", iScan, iCode, iMods);
      Send(str);
    }
    // the character it types, if any (not with Ctrl, not the control characters)
    WCHAR awc[4];
    abState[VK_SHIFT] = bShift ? 0x80 : 0;
    const int ct = bCtrl ? 0 : ToUnicode(vk, MapVirtualKey(vk, 0), abState, awc, 4, 0);
    for (int i = 0; i < ct; i++) {
      if (awc[i] < 32) continue;
      _snprintf(str, sizeof(str), "{\"t\":\"char\",\"c\":%d}", (int)awc[i]);
      Send(str);
    }
  }
};

// Dev/test hooks (work with Sam's window in the background): open/close the inventory, click at a point, type.
void DevScreen(BOOL bOpen) {
  if (bOpen) OpenScreen("inventory", NULL); else ScreenClosed(TRUE);
};

void DevChat(const char *strText) {
  OpenScreen("chat", strText);
};

void DevClick(FLOAT fX, FLOAT fY, INDEX iButton) {
  if (!_bScreen) return;
  _fCursorX = Clamp(fX, 0.0f, 1.0f);
  _fCursorY = Clamp(fY, 0.0f, 1.0f);
  char str[128];
  _snprintf(str, sizeof(str), "{\"t\":\"cursor\",\"x\":%.4f,\"y\":%.4f}", _fCursorX, _fCursorY);
  Send(str);
  _fCursorSentX = _fCursorX;
  _fCursorSentY = _fCursorY;
  _snprintf(str, sizeof(str), "{\"t\":\"click\",\"b\":%d,\"down\":true,\"shift\":false}", iButton);
  Send(str);
  _snprintf(str, sizeof(str), "{\"t\":\"click\",\"b\":%d,\"down\":false,\"shift\":false}", iButton);
  Send(str);
};

void DevType(const char *strText) {
  if (!_bScreen) return;
  for (const char *pch = strText; *pch != 0; pch++) {
    char str[64];
    _snprintf(str, sizeof(str), "{\"t\":\"char\",\"c\":%d}", (int)(unsigned char)*pch);
    Send(str);
  }
};

// A firework while gliding, if the player holds one (Minecraft uses it up and launches it).
BOOL TryBoost(void) {
  if (!glide::Gliding() || !glide::FireworkInHand()) return FALSE;
  if (!glide::Boost()) return FALSE;
  Send("{\"t\":\"firework\"}");
  return TRUE;
};

// Sam entered a level: a new game resets Minecraft's world and the player's things ("all"), the next level or a
// loaded save only the world ("world").
void RequestReset(const char *strWhat) {
  if (_strPendingReset != "all") _strPendingReset = strWhat;
  CPrintF("[SemCraft2] level entered: Minecraft's %s to be reset\n", strcmp(strWhat, "all") == 0 ? "world and inventory" : "world");
};

static void PollInput(void) {
  if (sc_iDisable & 128) return;

  // Sam's chat line (T) while playing: Minecraft's chat opens instead (its commands work there)
  CGameAPI *pGame = GetGameAPI();
  if (!_bScreen && link::Connected() && pGame->IsGameOn() && !pGame->IsMenuOn() && pGame->GetConState() == 5) {
    HWND hwnd = GetForegroundWindow();
    DWORD dwPid = 0;
    GetWindowThreadProcessId(hwnd, &dwPid);
    if (dwPid == GetCurrentProcessId()) {
      pGame->SetConState(0);
      OpenScreen("chat", "");
      return;
    }
  }

  const BOOL bPlaying = Playing();

  if (_bScreen) {
    // Sam's window in the background: nothing to read, the screen stays
    HWND hwnd = GetForegroundWindow();
    DWORD dwPid = 0;
    GetWindowThreadProcessId(hwnd, &dwPid);
    if (dwPid != GetCurrentProcessId() && GetGameAPI()->IsGameOn() && !GetGameAPI()->IsMenuOn()) return;
    // Sam's menu (Escape) or console over it: the screen goes
    if (!bPlaying) {
      // typing T in Minecraft's search opens Sam's chat line: cancel that instead
      if (GetGameAPI()->IsGameOn() && !GetGameAPI()->IsMenuOn() && GetGameAPI()->GetConState() == 5) {
        GetGameAPI()->SetConState(0);
        return;
      }
      ScreenClosed(TRUE);
      return;
    }
    PollScreen();
    return;
  }

  // gliding: the right mouse button lights a firework if the player has one (one goes), in build mode too
  const BOOL bBoostKey = bPlaying && glide::Gliding() && KeyDown(VK_RBUTTON);
  if (bBoostKey && !_bBoostKey) TryBoost();
  _bBoostKey = bBoostKey;

  // E opens Minecraft's inventory, / its chat with a command started
  const BOOL bKeyE = bPlaying && KeyDown('E');
  if (bKeyE && !_bKeyE) OpenScreen("inventory", NULL);
  _bKeyE = bKeyE;
  const BOOL bSlash = bPlaying && KeyDown(VK_OEM_2);
  if (bSlash && !_bSlash) OpenScreen("chat", "/");
  _bSlash = bSlash;
  if (_bScreen) return;

  // B toggles build mode
  const BOOL bKeyB = bPlaying && KeyDown('B');
  if (bKeyB && !_bKeyB) SetBuildMode(!_bBuild);
  _bKeyB = bKeyB;

  const BOOL bActive = bPlaying && _bBuild;
  const BOOL bLMB = bActive && KeyDown(VK_LBUTTON);
  const BOOL bRMB = bActive && KeyDown(VK_RBUTTON) && !glide::Gliding();

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
INDEX sc_iTestButtons = 0;

void SetPlayerForTest(const char *strWhat) {
  CEntity *pen = LocalPlayer();
  if (pen == NULL) return;
  char strKey[32];
  FLOAT f = 0, f2 = 0;
  INDEX ctRead = 0;
  for (const char *pch = strWhat; sscanf(pch, "%31s %f%n", strKey, &f, &ctRead) == 2; pch += ctRead) {
    if (strcmp(strKey, "hp") == 0) {
      ((CLiveEntity *)pen)->en_fHealth = f;
    } else if (strcmp(strKey, "armor") == 0) {
      FLOAT *pf = (FLOAT *)PlayerProp(pen, PROP_ARMOR);
      if (pf != NULL) *pf = f;
    } else if (strcmp(strKey, "bombs") == 0) {
      INDEX *pi = (INDEX *)PlayerProp(pen, PROP_SERIOUSBOMBS);
      if (pi != NULL) *pi = (INDEX)f;
    } else if (strcmp(strKey, "power") == 0) {
      INDEX ctMore = 0;
      if (sscanf(pch + ctRead, "%f%n", &f2, &ctMore) != 1) break;
      ctRead += ctMore;
      FLOAT *pf = (FLOAT *)PlayerProp(pen, PROP_POWERUPS + (INDEX)f);
      if (pf != NULL) *pf = _pTimer->CurrentTick() + f2;
    }
  }
  CPrintF("[SemCraft2] player: health %.0f, armor %.0f, bombs %d\n", ((CLiveEntity *)pen)->en_fHealth,
    PlayerFloat(pen, PROP_ARMOR, -1), PlayerIndex(pen, PROP_SERIOUSBOMBS, -1));
};

void OnGameLoad(void) {
  player::Resync();
};

// Riding: Sam's movement keys go to the vehicle as Minecraft's (forward/back, left/right, jump, crouch gets off).
static void Steer(const CPlayerAction &pa, BOOL bFresh) {
  static char strLast[128] = "";
  static INDEX ctSame = 0;
  if (!bFresh) return;
  const FLOAT fFwd = Clamp(-pa.pa_vTranslation(3) / 5.0f, -1.0f, 1.0f), fSide = Clamp(pa.pa_vTranslation(1) / 5.0f, -1.0f, 1.0f);
  char str[128];
  _snprintf(str, sizeof(str), "{\"t\":\"steer\",\"f\":%.2f,\"s\":%.2f,\"jump\":%s,\"down\":%s}", fFwd, fSide,
    pa.pa_vTranslation(2) > 0.01f ? "true" : "false", pa.pa_vTranslation(2) < -0.01f ? "true" : "false");
  str[sizeof(str) - 1] = 0;
  // on a change, and twice a second anyway
  if (strcmp(str, strLast) != 0 || ++ctSame >= 10) {
    ctSame = 0;
    strcpy(strLast, str);
    Send(str);
  }
};

void OnPlayerAction(CPlayerAction &pa, BOOL bFresh) {
  if (sc_iDisable & 64) return;
  // test hooks: hold fire for a number of actions, select a weapon by number (both work with Sam's window in the
  // background, where Sam doesn't read its own input)
  if (sc_iTestFire > 0) {
    sc_iTestFire--;
    pa.pa_ulButtons |= PLACT_FIRE;
  }
  if (bFresh && sc_iTestButtons != 0) {
    pa.pa_ulButtons |= (ULONG)sc_iTestButtons;
    sc_iTestButtons = 0;
  }
  // the view turns by the test hook; and while a Minecraft screen is open the mouse moves its cursor (PollScreen)
  // instead of the view (the core's player::OnAction keeps it still)
  if (bFresh && sc_iTestTurn > 0) {
    sc_iTestTurn--;
    player::TurnView(sc_fTestTurnH, sc_fTestTurnP);
  }
  player::FreezeView(_bScreen);
  player::OnAction(pa, bFresh);

  // test hook: run forward (before what takes the movement keys over)
  if (sc_iTestWalk > 0 && !_bScreen) {
    sc_iTestWalk--;
    pa.pa_vTranslation = FLOAT3D(0.0f, 0.0f, -10.0f);
  }

  // riding a Minecraft vehicle: the movement keys steer it, the player doesn't walk
  if (physics::Riding()) {
    Steer(pa, bFresh);
    pa.pa_vTranslation = FLOAT3D(0, 0, 0);
  } else {
    // Minecraft's elytra: a jump in the air opens the wings, walking is off while gliding
    glide::OnAction(pa, bFresh);
    // Minecraft's water: swimming takes the input over
    swim::OnAction(pa, bFresh);
  }

  if (_bScreen) {
    pa.pa_ulButtons = 0;
    pa.pa_vTranslation = FLOAT3D(0, 0, 0);
    return;
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

// EReceiveScore of the game's entities (Global.es event 14: points), made here as CTriggerEvent is (Movers.cpp): what a
// dying monster sends its killer; Player.es adds it to the level's and the game's score.
class CReceiveScoreEvent : public CEntityEvent {
public:
  INDEX iPoints;
  CReceiveScoreEvent(void) : CEntityEvent(0x0000000E), iPoints(0) {};
  CEntityEvent *MakeCopy(void) { return new CReceiveScoreEvent(*this); };
};

static BOOL SendEventSEH(CEntity *pen, const CEntityEvent &ee) {
  __try {
    pen->SendEvent(ee);
    return TRUE;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return FALSE;
  }
};

static void GiveScore(CEntity *penPlayer, INDEX ctPoints) {
  CReceiveScoreEvent ee;
  ee.iPoints = ctPoints;
  SendEventSEH(penPlayer, ee);
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
    // and the knockback Minecraft would give it (the monster's own physics carries it from there)
    double adKb[3] = { 0, 0, 0 };
    if (json::GetNumbers(strMsg, "kb", adKb, 3) == 3 && (adKb[0] != 0 || adKb[1] != 0 || adKb[2] != 0)) {
      physics::Push((ULONG)dId, FLOAT3D((FLOAT)adKb[0], (FLOAT)adKb[1], (FLOAT)adKb[2]));
    }

  } else if (strType == "push") {
    double adV[3] = { 0, 0, 0 };
    CEntity *penPlayer = LocalPlayer();
    if (json::GetNumbers(strMsg, "v", adV, 3) == 3 && penPlayer != NULL) {
      physics::Push(penPlayer->en_ulID, FLOAT3D((FLOAT)adV[0], (FLOAT)adV[1], (FLOAT)adV[2]));
    }

  } else if (strType == "screen") {
    // Minecraft closed its screen, or opened one itself (a chest, a crafting table, a sign...): Sam follows
    bool bOpen = true;
    std::string strOpen;
    if (json::GetBool(strMsg, "open", bOpen) && !bOpen) {
      ScreenClosed(FALSE);
    } else if (json::GetString(strMsg, "open", strOpen) && !strOpen.empty() && !_bScreen) {
      EnterScreen();
      CPrintF("[SemCraft2] Minecraft opened %s: the mouse moves the cursor\n", strOpen.c_str());
    }

  } else if (strType == "gear") {
    bool bElytra = false, bHand = false;
    double dFireworks = 0;
    json::GetBool(strMsg, "elytra", bElytra);
    json::GetBool(strMsg, "hand", bHand);
    json::GetNumber(strMsg, "fireworks", dFireworks);
    glide::OnGear(bElytra, bHand, (INDEX)dFireworks);

  } else if (strType == "fluid") {
    double dWater = 0, dLava = 0, adFlow[3] = { 0, 0, 0 };
    bool bEyes = false;
    json::GetNumber(strMsg, "water", dWater);
    json::GetNumber(strMsg, "lava", dLava);
    json::GetBool(strMsg, "eyes", bEyes);
    json::GetNumbers(strMsg, "flow", adFlow, 3);
    swim::OnFluid((FLOAT)dWater, (FLOAT)dLava, bEyes, FLOAT3D((FLOAT)adFlow[0], (FLOAT)adFlow[1], (FLOAT)adFlow[2]));

  } else if (strType == "teleport") {
    // a Minecraft ender pearl landed: the player is there (and the landing hurts, as in Minecraft)
    double adP[3] = { 0, 0, 0 }, dDamage = 0;
    CEntity *penPlayer = LocalPlayer();
    if (json::GetNumbers(strMsg, "p", adP, 3) == 3 && penPlayer != NULL) {
      json::GetNumber(strMsg, "d", dDamage);
      const FLOAT3D vAt((FLOAT)adP[0], (FLOAT)adP[1], (FLOAT)adP[2]);
      physics::TeleportTo(penPlayer->en_ulID, vAt);
      CPrintF("[SemCraft2] ender pearl: to (%.1f, %.1f, %.1f)\n", vAt(1), vAt(2), vAt(3));
      if (dDamage > 0 && _aHurts.size() < 64) {
        Hurt h;
        h.fDamage = (FLOAT)dDamage;
        h.vFrom = vAt + FLOAT3D(0, 1, 0);
        h.iType = DMT_IMPACT;
        _aHurts.push_back(h);
      }
    }

  } else if (strType == "ride") {
    // a Minecraft vehicle (boat, minecart, horse...) carries Sam's player: he follows its seat (the core's physics)
    bool bOn = false;
    double adP[3] = { 0, 0, 0 }, adV[3] = { 0, 0, 0 }, dYaw = 0;
    std::string strKind;
    json::GetBool(strMsg, "on", bOn);
    json::GetNumbers(strMsg, "p", adP, 3);
    json::GetNumbers(strMsg, "v", adV, 3);
    json::GetString(strMsg, "kind", strKind);
    physics::Ride(bOn, FLOAT3D((FLOAT)adP[0], (FLOAT)adP[1], (FLOAT)adP[2]), FLOAT3D((FLOAT)adV[0], (FLOAT)adV[1], (FLOAT)adV[2]),
      strKind.empty() ? NULL : strKind.c_str());
    // the vehicle turning turns him with it, as in Minecraft (its yaw: 0 along +z; Sam's heading turns the other way)
    static BOOL bHaveYaw = FALSE;
    static FLOAT fLastYaw = 0;
    if (bOn && json::GetNumber(strMsg, "yaw", dYaw)) {
      if (bHaveYaw) {
        FLOAT fTurn = (FLOAT)dYaw - fLastYaw;
        while (fTurn > 180.0f) fTurn -= 360.0f;
        while (fTurn < -180.0f) fTurn += 360.0f;
        if (Abs(fTurn) < 45.0f) player::TurnView(-fTurn, 0.0f);
      }
      fLastYaw = (FLOAT)dYaw;
      bHaveYaw = TRUE;
    } else {
      bHaveYaw = FALSE;
    }

  } else if (strType == "kill") {
    // Sam killed a Minecraft mob: it counts in Sam's score, as his own monsters do
    double dScore = 0;
    std::string strKind;
    json::GetString(strMsg, "kind", strKind);
    if (json::GetNumber(strMsg, "score", dScore) && dScore > 0) {
      _ctScoreDue += (INDEX)dScore;
      CPrintF("[SemCraft2] Minecraft %s killed: +%d score\n", strKind.c_str(), (INDEX)dScore);
    }

  } else if (strType == "shove") {
    double adD[3] = { 0, 0, 0 };
    CEntity *penPlayer = LocalPlayer();
    if (json::GetNumbers(strMsg, "d", adD, 3) == 3 && penPlayer != NULL) {
      physics::Shove(penPlayer->en_ulID, FLOAT3D((FLOAT)adD[0], (FLOAT)adD[1], (FLOAT)adD[2]));
    }

  } else if (strType == "actorpush") {
    double dId = 0, adD[3] = { 0, 0, 0 };
    if (json::GetNumber(strMsg, "id", dId) && json::GetNumbers(strMsg, "d", adD, 3) == 3) {
      actors::OnPush((ULONG)dId, FLOAT3D((FLOAT)adD[0], (FLOAT)adD[1], (FLOAT)adD[2]));
    }

  } else if (strType == "trigger") {
    // Minecraft's redstone reached one of Sam's doors or lifts
    double dMover = -1;
    if (json::GetNumber(strMsg, "mover", dMover)) {
      const BOOL bSent = movers::Trigger((INDEX)dMover, LocalPlayer());
      CPrintF("[SemCraft2] Minecraft triggers mover %d: %s\n", (INDEX)dMover, bSent ? "sent" : "no such mover");
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
    _bScreen = FALSE; // a new Minecraft has no screen open
    if (_bBuild) Send("{\"t\":\"build\",\"on\":true}");
  }

  if (!_bLevelSent) SendLevel();
  if (!_strPendingReset.empty() && link::Connected()) {
    Send("{\"t\":\"reset\",\"what\":\"" + _strPendingReset + "\"}");
    _strPendingReset.clear();
  }

  std::string strMsg;
  for (INDEX i = 0; i < 256 && link::Poll(strMsg); i++) {
    Handle(strMsg);
  }

  PollInput();
};

static BOOL _bHurtBroken = FALSE;

// Dev: switch subsystems off to find one at fault (bits: 1 hurts, 2 weapons, 4 monsters, 8 movers, 16 glide, 32 blocks,
// 64 player-action edits, 128 input)
INDEX sc_iDisable = 0;

void OnStep(void) {
  _ctTicks++;
  CEntity *penPlayer = LocalPlayer();

  // Minecraft's hits land on Sam's player
  if (!(sc_iDisable & 1) && penPlayer != NULL && GetGameAPI()->IsGameOn() && !_bHurtBroken && (penPlayer->GetFlags() & ENF_ALIVE)) {
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

    // and killing its mobs scores
    if (_ctScoreDue > 0) {
      GiveScore(penPlayer, _ctScoreDue);
      _ctScoreDue = 0;
    }
  } else {
    _aHurts.clear();
  }

  // its explosions' knockback, its pistons, its ender pearls (queued as they came), and riding its boats
  physics::Step(penPlayer);

  // Sam's guns hurt Minecraft's mobs
  if (GetGameAPI()->IsGameOn() && link::Connected()) {
    if (!(sc_iDisable & 2)) weapons::Step(penPlayer, &Send);
    // and Sam's monsters are in Minecraft as stand-ins its mobs hunt
    if (!(sc_iDisable & 4)) actors::Step(penPlayer, &Send);
    // and its doors and lifts move there too
    if (!(sc_iDisable & 8)) movers::Step(&Send);
  }

  // Minecraft's elytra on Sam's player, and its water and lava
  if (GetGameAPI()->IsGameOn() && !(sc_iDisable & 16)) {
    glide::Step(penPlayer);
    swim::Step(penPlayer);
  }

  // and Minecraft's blocks are solid here
  if (GetGameAPI()->IsGameOn() && !(sc_iDisable & 32)) blocks::Step();

  // a serious bomb went off (TSE): its blast reaches Minecraft's monsters too
  static INDEX _ctBombsLast = -1;
  const INDEX ctBombs = penPlayer != NULL ? PlayerIndex(penPlayer, PROP_SERIOUSBOMBS, -1) : -1;
  if (ctBombs >= 0 && _ctBombsLast >= 0 && ctBombs < _ctBombsLast && link::Connected()) {
    const FLOAT3D v = penPlayer->GetPlacement().pl_PositionVector;
    char str[128];
    _snprintf(str, sizeof(str), "{\"t\":\"seriousbomb\",\"p\":[%.2f,%.2f,%.2f]}", v(1), v(2), v(3));
    Send(str);
    CPrintF("[SemCraft2] serious bomb: Minecraft's monsters around get it too\n");
  }
  _ctBombsLast = ctBombs;

  // the player's state, 5 times a second: health (hearts, more than ten above the level's top health), armour, powerups
  if (_ctTicks % 4 == 0 && link::Connected()) {
    FLOAT fHealth = 0, fTop = 100.0f, fArmor = 0;
    BOOL bAlive = FALSE;
    FLOAT afPowerups[4] = { 0, 0, 0, 0 };
    if (penPlayer != NULL) {
      fHealth = ((CLiveEntity *)penPlayer)->en_fHealth;
      bAlive = fHealth > 0;
      fTop = PlayerFloat(penPlayer, PROP_TOPHEALTH, 100.0f);
      fArmor = PlayerFloat(penPlayer, PROP_ARMOR, 0.0f);
      for (INDEX i = 0; i < 4; i++) afPowerups[i] = PowerupLeft(penPlayer, i);
    }

    char str[320];
    _snprintf(str, sizeof(str), "{\"t\":\"state\",\"hp\":%.2f,\"top\":%.1f,\"armor\":%.1f,\"pw\":[%.1f,%.1f,%.1f,%.1f],\"alive\":%s,"
      "\"inGame\":%s,\"build\":%s}",
      fHealth, fTop, fArmor, afPowerups[0], afPowerups[1], afPowerups[2], afPowerups[3], bAlive ? "true" : "false",
      GetGameAPI()->IsGameOn() ? "true" : "false", _bBuild ? "true" : "false");
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
  str.PrintF("link: %s (generation %d), level: %s (%d tris, %s), build: %s, screen: %s, cam frames sent: %u; %s",
    link::Connected() ? "connected" : "waiting for Minecraft on 127.0.0.1:25610", link::Generation(),
    _level.bValid ? _level.strName.c_str() : "none", _level.ctTris, _bLevelSent ? "sent" : "not sent",
    _bBuild ? "on" : "off", _bScreen ? "open" : "none", _ulCamFrame, (weapons::Status() + "; " + actors::Status() + "; " + blocks::Status() + "; " + movers::Status() + "; " + glide::Status() + "; " + swim::Status()).str_String);
  return str;
};

}; // namespace
