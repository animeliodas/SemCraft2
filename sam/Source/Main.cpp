/* SemCraft 2 - Minecraft inside Serious Sam Classic (TFE 1.05 / TSE 1.07), Classics Patch plugin.
 *
 * Serious Sam stays the game you play: its camera drives a hidden Minecraft 26.3 (Fabric mod "semcraft2"), whose
 * picture is drawn into Sam's frame with depth. Sam's level becomes invisible collision in Minecraft, Minecraft's
 * hits hurt Sam's player, and build mode (B) turns the mouse and 1-9 into Minecraft's.
 * Contract: docs/CONTRACT.md in the SemCraft 2 repository.
 */
#include "StdH.h"

#include "Bridge.h"
#include "Compositor.h"
#include "GlDraw.h"

#include <Engine/World/WorldRayCasting.h>

#include <vector>
#include <stdio.h>

CLASSICSPATCH_DEFINE_PLUGIN(k_EPluginFlagGame | k_EPluginFlagServer, MakeVersion(0, 1, 0),
  "SemCraft 2", "SemCraft 2", "Minecraft inside Serious Sam: Minecraft's world drawn into Sam's, built on Sam's levels.");

// Once per drawn frame: only the first big perspective view (the game view) is composited.
static BOOL _bViewDone = FALSE;

// A screenshot of the next finished frame goes here (sc_Shot).
static CTString _strShotPending = "";

// Dev hook (environment SEMCRAFT2_DEV=1): console commands from %TEMP%\SemCraft2\cmd.txt, so tests can drive Sam
// without touching the user's mouse and keyboard.
static BOOL _bDevHook = FALSE;
static DWORD _tmNextDevPoll = 0;

static void WriteBmp(const char *strFile, INDEX w, INDEX h, const UBYTE *pubRGBA) {
  FILE *f = fopen(strFile, "wb");
  if (f == NULL) {
    CPrintF("[SemCraft2] sc_Shot: can't write %s\n", strFile);
    return;
  }

  const INDEX iRow = (w * 3 + 3) & ~3;
  const ULONG ulData = iRow * h;
  UBYTE aubHeader[54] = { 'B', 'M' };
  *(ULONG *)(aubHeader + 2) = 54 + ulData;
  *(ULONG *)(aubHeader + 10) = 54;
  *(ULONG *)(aubHeader + 14) = 40;
  *(LONG *)(aubHeader + 18) = w;
  *(LONG *)(aubHeader + 22) = h; // bottom-up, like glReadPixels
  *(UWORD *)(aubHeader + 26) = 1;
  *(UWORD *)(aubHeader + 28) = 24;
  *(ULONG *)(aubHeader + 34) = ulData;
  fwrite(aubHeader, 54, 1, f);

  std::vector<UBYTE> aubRow(iRow, 0);
  for (INDEX y = 0; y < h; y++) {
    const UBYTE *pub = pubRGBA + (SIZE_T)y * w * 4;
    for (INDEX x = 0; x < w; x++) {
      aubRow[x * 3 + 0] = pub[x * 4 + 2];
      aubRow[x * 3 + 1] = pub[x * 4 + 1];
      aubRow[x * 3 + 2] = pub[x * 4 + 0];
    }
    fwrite(&aubRow[0], iRow, 1, f);
  }
  fclose(f);
  CPrintF("[SemCraft2] screenshot %dx%d -> %s\n", w, h, strFile);
};

static void TakeShot(CDrawPort *pdp) {
  if (_strShotPending == "" || pdp == NULL || pdp->dp_Raster == NULL || !gldraw::ContextCurrent()) return;

  const INDEX w = pdp->dp_Raster->ra_Width, h = pdp->dp_Raster->ra_Height;
  std::vector<UBYTE> aub((SIZE_T)w * h * 4);
  gldraw::ReadPixels(0, 0, w, h, &aub[0]);
  WriteBmp(_strShotPending.str_String, w, h, &aub[0]);
  _strShotPending = "";
};

static void PollDevCommands(void) {
  if (!_bDevHook) return;

  const DWORD tmNow = GetTickCount();
  if ((LONG)(tmNow - _tmNextDevPoll) < 0) return;
  _tmNextDevPoll = tmNow + 250;

  char strTemp[MAX_PATH];
  GetTempPathA(MAX_PATH, strTemp);
  CTString strFile = CTString(strTemp) + "SemCraft2\\cmd.txt";
  CTString strTaken = CTString(strTemp) + "SemCraft2\\cmd.taken";

  // take the file away first, so a writer can't append to what we're reading
  DeleteFileA(strTaken.str_String);
  if (!MoveFileA(strFile.str_String, strTaken.str_String)) return;

  FILE *f = fopen(strTaken.str_String, "rb");
  if (f == NULL) return;

  char strLine[1024];
  while (fgets(strLine, sizeof(strLine), f) != NULL) {
    size_t ct = strlen(strLine);
    while (ct > 0 && (strLine[ct - 1] == 10 || strLine[ct - 1] == 13)) strLine[--ct] = 0;
    if (ct == 0) continue;

    CPrintF("[SemCraft2] dev> %s\n", strLine);
    _pShell->Execute(strLine);
  }
  fclose(f);
  DeleteFileA(strTaken.str_String);
};

// ------------------------------------------------------------------
// Console commands
// ------------------------------------------------------------------
static void SC_Status(SHELL_FUNC_ARGS) {
  BEGIN_SHELL_FUNC;
  CPrintF("[SemCraft2] %s\n", bridge::Status().str_String);
  CPrintF("[SemCraft2] %s\n", compositor::Status().str_String);
};

static void SC_Build(SHELL_FUNC_ARGS) {
  BEGIN_SHELL_FUNC;
  bridge::SetBuildMode(!bridge::BuildMode());
};

// Test hook: a gold pillar in Minecraft where Sam's crosshair hits Sam's world.
static void SC_Pillar(SHELL_FUNC_ARGS) {
  BEGIN_SHELL_FUNC;
  const INDEX iHeight = NEXT_ARG(INDEX);

  FLOAT3D vHit;
  if (!bridge::AimPoint(vHit)) {
    CPrintF("[SemCraft2] sc_Pillar: aim at Sam's world\n");
    return;
  }

  CTString str;
  str.PrintF("{\"t\":\"pillar\",\"p\":[%.3f,%.3f,%.3f],\"h\":%d}", vHit(1), vHit(2) + 0.01f, vHit(3), Clamp(iHeight, (INDEX)1, (INDEX)32));
  bridge::SendRaw(str.str_String);
  CPrintF("[SemCraft2] pillar at (%.2f, %.2f, %.2f)\n", vHit(1), vHit(2), vHit(3));
};

// Test hook / fun: spawn Minecraft mobs where Sam's crosshair hits: sc_Spawn("zombie 3").
// (One string argument: a second argument after a CTString didn't arrive intact.)
static void SC_Spawn(SHELL_FUNC_ARGS) {
  BEGIN_SHELL_FUNC;
  const CTString &strArgs = *NEXT_ARG(CTString *);

  char strKindBuf[64] = "zombie";
  INDEX ct = 1;
  sscanf(strArgs.str_String, "%63s %d", strKindBuf, &ct);
  const CTString strKind = strKindBuf;

  FLOAT3D vHit;
  if (!bridge::AimPoint(vHit)) {
    CPrintF("[SemCraft2] sc_Spawn: aim at Sam's world\n");
    return;
  }

  CTString str;
  str.PrintF("{\"t\":\"spawn\",\"k\":\"%s\",\"p\":[%.3f,%.3f,%.3f],\"n\":%d,\"r\":3}", strKind.str_String, vHit(1), vHit(2) + 0.5f, vHit(3),
    Clamp(ct, (INDEX)1, (INDEX)50));
  bridge::SendRaw(str.str_String);
};

// Test hook: a Serious Sam monster where the crosshair hits, e.g. sc_SpawnSam("Gnaar") (Classes\Gnaar.ecl).
// Created directly in the world, like LocalCheats does without its entity packets (those carry C++ objects across
// the DLL boundary, which a plugin with its own runtime must not do: it crashed). Single player / local server only.
static void SC_SpawnSam(SHELL_FUNC_ARGS) {
  BEGIN_SHELL_FUNC;
  const CTString &strName = *NEXT_ARG(CTString *);

  if (!_pNetwork->IsServer()) {
    CPrintF("[SemCraft2] sc_SpawnSam: only in single player or on a local server\n");
    return;
  }

  FLOAT3D vHit;
  if (!bridge::AimPoint(vHit)) {
    CPrintF("[SemCraft2] sc_SpawnSam: aim at Sam's world\n");
    return;
  }

  CPlacement3D pl(vHit + FLOAT3D(0, 0.5f, 0), ANGLE3D(0, 0, 0));
  const CTFileName fnmClass = CTString("Classes\\") + strName + ".ecl";

  try {
    CEntity *pen = IWorld::GetWorld()->CreateEntity_t(pl, fnmClass);
    pen->Initialize();
    CPrintF("[SemCraft2] spawned %s (%u) at (%.1f, %.1f, %.1f)\n", strName.str_String, pen->en_ulID, vHit(1), vHit(2), vHit(3));
  } catch (char *strError) {
    CPrintF("[SemCraft2] sc_SpawnSam: can't create %s: %s\n", fnmClass.str_String, strError);
  }
};

// Test hook: what Sam's own collision meets along the crosshair (models by their collision boxes, like bullets).
static void SC_Probe(SHELL_FUNC_ARGS) {
  BEGIN_SHELL_FUNC;
  CPlayerEntities cPlayers;
  IWorld::GetLocalPlayers(cPlayers);
  if (cPlayers.Count() == 0) return;

  CPlayerEntity *penPlayer = cPlayers.Pointer(0);
  CPlacement3D plView = IWorld::GetViewpoint(penPlayer, TRUE);
  FLOATmatrix3D m;
  MakeRotationMatrix(m, plView.pl_OrientationAngle);
  const FLOAT3D vDir(-m(1, 3), -m(2, 3), -m(3, 3));

  CCastRay cr(penPlayer, plView.pl_PositionVector, plView.pl_PositionVector + vDir * 300.0f);
  cr.cr_ttHitModels = CCastRay::TT_COLLISIONBOX;
  IWorld::GetWorld()->CastRay(cr);

  if (cr.cr_penHit == NULL) {
    CPrintF("[SemCraft2] probe: nothing\n");
  } else {
    CPrintF("[SemCraft2] probe: %s at %.2f m (%.2f, %.2f, %.2f)\n", cr.cr_penHit->GetClass()->ec_pdecDLLClass->dec_strName,
      cr.cr_fHitDistance, cr.cr_vHit(1), cr.cr_vHit(2), cr.cr_vHit(3));
  }
};

// Test hook: where the player stands.
static void SC_Where(SHELL_FUNC_ARGS) {
  BEGIN_SHELL_FUNC;
  CPlayerEntities cPlayers;
  IWorld::GetLocalPlayers(cPlayers);
  if (cPlayers.Count() == 0) return;
  const CPlacement3D &pl = cPlayers.Pointer(0)->GetPlacement();
  CPrintF("[SemCraft2] where: %.3f %.3f %.3f heading %.1f\n", pl.pl_PositionVector(1), pl.pl_PositionVector(2), pl.pl_PositionVector(3),
    (FLOAT)pl.pl_OrientationAngle(1));
};

// Test hook: the living monsters within 150 m, with their health.
static void SC_Monsters(SHELL_FUNC_ARGS) {
  BEGIN_SHELL_FUNC;
  CPlayerEntities cPlayers;
  IWorld::GetLocalPlayers(cPlayers);
  if (cPlayers.Count() == 0) return;
  const FLOAT3D vPlayer = cPlayers.Pointer(0)->GetPlacement().pl_PositionVector;

  INDEX ct = 0;
  FOREACHINDYNAMICCONTAINER(IWorld::GetWorld()->wo_cenEntities, CEntity, iten) {
    CEntity *pen = &*iten;
    if ((pen->GetFlags() & ENF_DELETED) || !(pen->GetFlags() & ENF_ALIVE) || !IsDerivedFromClass(pen, "Enemy Base")) continue;
    const FLOAT3D vPos = pen->GetPlacement().pl_PositionVector;
    const FLOAT fDist = (vPos - vPlayer).Length();
    if (fDist > 150.0f) continue;
    CPrintF("[SemCraft2] monster %s (%u) health %.1f at %.1f m\n", pen->GetClass()->ec_pdecDLLClass->dec_strName, pen->en_ulID,
      ((CLiveEntity *)pen)->en_fHealth, fDist);
    ct++;
  }
  CPrintF("[SemCraft2] monsters: %d\n", ct);
};

// Screenshot of the next finished frame (Sam + Minecraft) as a .bmp.
static void SC_Shot(SHELL_FUNC_ARGS) {
  BEGIN_SHELL_FUNC;
  const CTString &strFile = *NEXT_ARG(CTString *);
  _strShotPending = strFile;
};

// Raw JSON to Minecraft (e.g. sc_Send("{\"t\":\"cmd\",\"c\":\"time set night\"}")).
static void SC_Send(SHELL_FUNC_ARGS) {
  BEGIN_SHELL_FUNC;
  const CTString &strJson = *NEXT_ARG(CTString *);
  bridge::SendRaw(strJson.str_String);
};

// ------------------------------------------------------------------
// Plugin events
// ------------------------------------------------------------------
static void SC_OnStep(void) {
  bridge::OnStep();
};

static void SC_OnFrame(CDrawPort *pdp) {
  bridge::OnFrame();
  PollDevCommands();
};

static void SC_OnPreDraw(CDrawPort *pdp) {
  _bViewDone = FALSE;
};

static void SC_OnRenderView(CWorld &wo, CEntity *penViewer, CAnyProjection3D &apr, CDrawPort *pdp) {
  if (_bViewDone || pdp == NULL || !apr.IsPerspective()) return;
  if (!GetGameAPI()->IsGameOn() || &wo != IWorld::GetWorld()) return;

  // the game view, not a small picture-in-picture
  CRaster *pra = pdp->dp_Raster;
  if (pra != NULL && pdp->GetWidth() * pdp->GetHeight() * 2 < pra->ra_Width * pra->ra_Height) return;

  _bViewDone = TRUE;

  // RenderView() prepared its own copy of the projection; prepare ours the same way (Render.cpp) to get the
  // frustum planes and the world's depth range (0..0.9; the background uses 0.9..1)
  CPerspectiveProjection3D ppr = *(CPerspectiveProjection3D *)(CProjection3D *)apr;
  ppr.ObjectPlacementL() = CPlacement3D(FLOAT3D(0, 0, 0), ANGLE3D(0, 0, 0));
  ppr.ObjectFaceForwardL() = FALSE;
  ppr.ObjectStretchL() = FLOAT3D(1, 1, 1);
  ppr.DepthBufferNearL() = 0.0f;
  ppr.DepthBufferFarL() = 0.9f;
  ppr.Prepare();

  bridge::OnView(ppr, penViewer);
  compositor::DrawWorld(ppr);
};

static void SC_OnPostDraw(CDrawPort *pdp) {
  compositor::DrawOverlay();
  TakeShot(pdp);
};

static void SC_OnWorldLoad(CWorld *pwo, const CTFileName &fnmWorld) {
  bridge::OnWorldLoad(pwo, fnmWorld);
};

static void SC_OnGameStop(void) {
  bridge::OnGameStop();
};

static void SC_OnPlayerAction(INDEX iClient, INDEX iPlayer, CPlayerAction &pa, INDEX iResent) {
  bridge::OnPlayerAction(pa, iResent < 0);
};

// ------------------------------------------------------------------
// Module entry points
// ------------------------------------------------------------------
CLASSICSPATCH_PLUGIN_STARTUP(HIniConfig props, PluginEvents_t &events) {
  events.m_processing->OnStep = &SC_OnStep;
  events.m_processing->OnFrame = &SC_OnFrame;
  events.m_rendering->OnPreDraw = &SC_OnPreDraw;
  events.m_rendering->OnRenderView = &SC_OnRenderView;
  events.m_rendering->OnPostDraw = &SC_OnPostDraw;
  events.m_world->OnWorldLoad = &SC_OnWorldLoad;
  events.m_game->OnGameStop = &SC_OnGameStop;
  events.m_packet->OnPlayerAction = &SC_OnPlayerAction;

  ClassicsPlugins()->RegisterMethod(TRUE, "void", "sc_Status", "void", &SC_Status);
  ClassicsPlugins()->RegisterMethod(TRUE, "void", "sc_Build", "void", &SC_Build);
  ClassicsPlugins()->RegisterMethod(TRUE, "void", "sc_Pillar", "INDEX", &SC_Pillar);
  ClassicsPlugins()->RegisterMethod(TRUE, "void", "sc_Spawn", "CTString", &SC_Spawn);
  ClassicsPlugins()->RegisterMethod(TRUE, "void", "sc_Send", "CTString", &SC_Send);
  ClassicsPlugins()->RegisterMethod(TRUE, "void", "sc_Shot", "CTString", &SC_Shot);
  ClassicsPlugins()->RegisterMethod(TRUE, "void", "sc_SpawnSam", "CTString", &SC_SpawnSam);
  ClassicsPlugins()->RegisterMethod(TRUE, "void", "sc_Probe", "void", &SC_Probe);
  ClassicsPlugins()->RegisterMethod(TRUE, "void", "sc_Where", "void", &SC_Where);
  ClassicsPlugins()->RegisterMethod(TRUE, "void", "sc_Monsters", "void", &SC_Monsters);

  const char *strDev = getenv("SEMCRAFT2_DEV");
  _bDevHook = strDev != NULL && strDev[0] == '1';
  if (_bDevHook) CPrintF("[SemCraft2] dev hook on: commands from %%TEMP%%/SemCraft2/cmd.txt\n");
  _pShell->DeclareSymbol("user INDEX sc_iCompositeMode;", &compositor::sc_iCompositeMode);
  _pShell->DeclareSymbol("user FLOAT sc_fDepthBias;", &compositor::sc_fDepthBias);
  _pShell->DeclareSymbol("persistent user FLOAT sc_fLightMatch;", &compositor::sc_fLightMatch);
  _pShell->DeclareSymbol("user INDEX sc_iTestFire;", &bridge::sc_iTestFire);
  _pShell->DeclareSymbol("user INDEX sc_iTestSelect;", &bridge::sc_iTestSelect);
  _pShell->DeclareSymbol("user INDEX sc_iTestWalk;", &bridge::sc_iTestWalk);
  _pShell->DeclareSymbol("user INDEX sc_iTestTurn;", &bridge::sc_iTestTurn);
  _pShell->DeclareSymbol("user FLOAT sc_fTestTurnH;", &bridge::sc_fTestTurnH);
  _pShell->DeclareSymbol("user FLOAT sc_fTestTurnP;", &bridge::sc_fTestTurnP);

  bridge::Startup();
  CPrintF("^c00ff00[SemCraft2]^r loaded (built %s %s). Start Minecraft with the semcraft2 mod; B toggles build mode, sc_Status() shows the link.\n",
    __DATE__, __TIME__);
};

CLASSICSPATCH_PLUGIN_SHUTDOWN(HIniConfig props) {
  bridge::Shutdown();
  compositor::Shutdown(FALSE);
  CPrintF("[SemCraft2] shut down\n");
};
