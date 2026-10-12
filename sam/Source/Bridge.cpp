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
// Minecraft's pistons moving Sam's player, metres (the next simulation tick moves him)
static std::deque<FLOAT3D> _avShoves;
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
static ANGLE3D _aLastRaw(0, 0, 0);
static BOOL _bHaveLastRaw = FALSE;
// What the player's actions carry on top of their own rotation: the test turns, minus what the mouse turned while a
// Minecraft screen was open. Actions carry the accumulated rotation (Player.es applies their difference), so it stays
// added from then on. A new player entity (a level, a new game, a loaded save) has its own: it's read back from it, or
// the first action would jump the view by the difference (see ResyncOffset).
static ANGLE3D _aOffset(0, 0, 0);
static BOOL _bResyncOffset = TRUE;
static ULONG _ulOffsetPlayer = 0;
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
  movers::ResendAll();
};

void OnWorldLoad(CWorld *pwo, const CTFileName &fnmWorld) {
  // only the game's world (not menu backgrounds)
  if (pwo != IWorld::GetWorld()) return;

  _level = levelx::Export(pwo, fnmWorld);
  _bLevelSent = FALSE;
  weapons::Reset();
  actors::Reset();
  blocks::Reset(TRUE);
  _bResyncOffset = TRUE; // a new player entity
  SendLevel();
};

void OnGameStop(void) {
  SetBuildMode(FALSE);
  _bResyncOffset = TRUE;
  _aHurts.clear();
  _avPushes.clear();
  weapons::Reset();
  actors::Reset();
  blocks::Reset(FALSE);
  movers::Reset(); // their entity pointers must go before the world does
  glide::Reset();
  swim::Reset();
  _avShoves.clear();
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

// Player.es (class 401, both games): m_aLastRotation (what the last applied action carried) and m_aLocalRotation (the
// rotation the player's actions are composed from: the mouse adds to it)
static const ULONG PROP_LAST_ROTATION = (401UL << 8) | 151;
static const ULONG PROP_LOCAL_ROTATION = (401UL << 8) | 154;

static void ResyncOffset(CEntity *pen) {
  CEntityProperty *pepLast = IWorld::PropertyForId(LibClassHolder(pen), PROP_LAST_ROTATION);
  CEntityProperty *pepLocal = IWorld::PropertyForId(LibClassHolder(pen), PROP_LOCAL_ROTATION);
  _aOffset = ANGLE3D(0, 0, 0);
  if (pepLast != NULL && pepLocal != NULL) {
    _aOffset = *(ANGLE3D *)((UBYTE *)pen + pepLast->ep_slOffset) - *(ANGLE3D *)((UBYTE *)pen + pepLocal->ep_slOffset);
  }
  _ulOffsetPlayer = pen->en_ulID;
  _bResyncOffset = FALSE;
  _bHaveLastRaw = FALSE;
};

void OnGameLoad(void) {
  _bResyncOffset = TRUE;
};

void OnPlayerAction(CPlayerAction &pa, BOOL bFresh) {
  if (sc_iDisable & 64) return;
  // test hooks: hold fire for a number of actions, select a weapon by number (both work with Sam's window in the
  // background, where Sam doesn't read its own input)
  if (sc_iTestFire > 0) {
    sc_iTestFire--;
    pa.pa_ulButtons |= PLACT_FIRE;
  }
  // the view turns by the test hook; and while a Minecraft screen is open the mouse moves its cursor (PollScreen)
  // instead of the view, so what it turned is taken back out (see _aOffset)
  CEntity *penPlayer = LocalPlayer();
  if (penPlayer != NULL && (_bResyncOffset || penPlayer->en_ulID != _ulOffsetPlayer)) ResyncOffset(penPlayer);
  if (bFresh) {
    if (sc_iTestTurn > 0) {
      sc_iTestTurn--;
      _aOffset(1) += sc_fTestTurnH;
      _aOffset(2) += sc_fTestTurnP;
    }
    if (_bScreen && _bHaveLastRaw) _aOffset -= pa.pa_aRotation - _aLastRaw;
    _aLastRaw = pa.pa_aRotation;
    _bHaveLastRaw = TRUE;
  }
  pa.pa_aRotation += _aOffset;

  // Minecraft's elytra: a jump in the air opens the wings, walking is off while gliding
  glide::OnAction(pa, bFresh);
  // Minecraft's water: swimming takes the input over
  swim::OnAction(pa, bFresh);

  if (_bScreen) {
    pa.pa_ulButtons = 0;
    pa.pa_vTranslation = FLOAT3D(0, 0, 0);
    return;
  }

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

// Moved by vDelta metres, as a piston moves him (Minecraft already found it free there).
static BOOL SafeMove(CEntity *pen, const FLOAT3D &vDelta) {
  __try {
    CPlacement3D pl = pen->GetPlacement();
    pl.pl_PositionVector += vDelta;
    pen->SetPlacement(pl);
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

  } else if (strType == "shove") {
    double adD[3] = { 0, 0, 0 };
    if (json::GetNumbers(strMsg, "d", adD, 3) == 3 && _avShoves.size() < 32) {
      _avShoves.push_back(FLOAT3D((FLOAT)adD[0], (FLOAT)adD[1], (FLOAT)adD[2]));
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

    // and its explosions throw him
    while (!_avPushes.empty() && !_bHurtBroken) {
      const FLOAT3D v = _avPushes.front();
      _avPushes.pop_front();
      if (!SafePush(penPlayer, v)) {
        _bHurtBroken = TRUE;
        CPrintF("^cff0000[SemCraft2]^r pushing the player crashed inside the game; Minecraft damage is off for this session\n");
      }
    }

    // and its pistons move him
    FLOAT3D vShove(0, 0, 0);
    while (!_avShoves.empty()) {
      vShove += _avShoves.front();
      _avShoves.pop_front();
    }
    if (vShove.Length() > 0.0001f && !_bHurtBroken && !SafeMove(penPlayer, vShove)) {
      _bHurtBroken = TRUE;
      CPrintF("^cff0000[SemCraft2]^r moving the player crashed inside the game; Minecraft damage is off for this session\n");
    }
  } else {
    _aHurts.clear();
    _avPushes.clear();
    _avShoves.clear();
  }

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
  str.PrintF("link: %s (generation %d), level: %s (%d tris, %s), build: %s, screen: %s, cam frames sent: %u; %s",
    link::Connected() ? "connected" : "waiting for Minecraft on 127.0.0.1:25610", link::Generation(),
    _level.bValid ? _level.strName.c_str() : "none", _level.ctTris, _bLevelSent ? "sent" : "not sent",
    _bBuild ? "on" : "off", _bScreen ? "open" : "none", _ulCamFrame, (weapons::Status() + "; " + actors::Status() + "; " + blocks::Status() + "; " + movers::Status() + "; " + glide::Status() + "; " + swim::Status()).str_String);
  return str;
};

}; // namespace
