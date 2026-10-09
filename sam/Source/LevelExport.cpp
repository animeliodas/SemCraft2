/* SemCraft 2 - level geometry export. See LevelExport.h. */
#include "StdH.h"
#include "LevelExport.h"

#include <Engine/World/WorldRayCasting.h>
#include <vector>
#include <stdio.h>

namespace levelx {

struct Tri {
  FLOAT v[9];
  FLOAT n[3];
  ULONG ulFlags;
};

static const ULONG FLAG_MOVING = 1;
static const ULONG FLAG_PASSABLE = 2;

static ULONG64 Fnv(ULONG64 h, const void *p, size_t ct) {
  const UBYTE *pub = (const UBYTE *)p;
  for (size_t i = 0; i < ct; i++) {
    h ^= pub[i];
    h *= 1099511628211ULL;
  }
  return h;
};

static void ExportPolygon(CBrushPolygon &bpo, ULONG ulBrushFlags, std::vector<Tri> &atri) {
  const INDEX ctVtx = bpo.bpo_apbvxTriangleVertices.Count();
  const INDEX ctEl = bpo.bpo_aiTriangleElements.Count();
  if (ctVtx < 3 || ctEl < 3) return;

  ULONG ulFlags = ulBrushFlags;
  if (bpo.bpo_ulFlags & BPOF_PASSABLE) ulFlags |= FLAG_PASSABLE;

  FLOAT3D vN(0, 0, 0);
  if (bpo.bpo_pbplPlane != NULL) {
    const FLOATplane3D &pl = bpo.bpo_pbplPlane->bpl_plAbsolute;
    vN = FLOAT3D(pl(1), pl(2), pl(3));
  }

  for (INDEX iEl = 0; iEl + 2 < ctEl; iEl += 3) {
    const INDEX ai[3] = { bpo.bpo_aiTriangleElements[iEl], bpo.bpo_aiTriangleElements[iEl + 1], bpo.bpo_aiTriangleElements[iEl + 2] };
    if (ai[0] < 0 || ai[1] < 0 || ai[2] < 0 || ai[0] >= ctVtx || ai[1] >= ctVtx || ai[2] >= ctVtx) continue;

    Tri t;
    for (INDEX k = 0; k < 3; k++) {
      const FLOAT3D &v = bpo.bpo_apbvxTriangleVertices[ai[k]]->bvx_vAbsolute;
      t.v[k * 3 + 0] = v(1);
      t.v[k * 3 + 1] = v(2);
      t.v[k * 3 + 2] = v(3);
    }
    t.n[0] = vN(1); t.n[1] = vN(2); t.n[2] = vN(3);
    t.ulFlags = ulFlags;
    atri.push_back(t);
  }
};

Exported Export(CWorld *pwo, const CTFileName &fnmWorld) {
  Exported ex;
  ex.bValid = FALSE;
  ex.ctTris = 0;
  ex.strName = fnmWorld.str_String;

  std::vector<Tri> atri;
  atri.reserve(16384);
  INDEX ctBrushes = 0, ctMoving = 0;

  CDynamicArray<CBrush3D> &abr = pwo->wo_baBrushes.ba_abrBrushes;
  abr.Lock();

  for (INDEX iBrush = 0; iBrush < abr.Count(); iBrush++) {
    CBrush3D &br = abr[iBrush];
    CEntity *pen = br.br_penEntity;
    if (pen == NULL || (pen->GetFlags() & ENF_DELETED)) continue;

    // triggers, water and force fields aren't solid
    if (pen->en_RenderType == CEntity::RT_FIELDBRUSH) continue;

    ULONG ulBrushFlags = 0;
    if (pen->en_ulPhysicsFlags & EPF_MOVABLE) { ulBrushFlags |= FLAG_MOVING; ctMoving++; }
    if ((pen->en_ulCollisionFlags & ECF_TESTMASK) == 0) ulBrushFlags |= FLAG_PASSABLE;

    // the most detailed mip only (the others are the same brush, simpler)
    CBrushMip *pbm = br.GetFirstMip();
    if (pbm == NULL) continue;
    ctBrushes++;

    FOREACHINDYNAMICARRAY(pbm->bm_abscSectors, CBrushSector, itbsc) {
      CBrushSector &bsc = *itbsc;
      for (INDEX iPol = 0; iPol < bsc.bsc_abpoPolygons.Count(); iPol++) {
        ExportPolygon(bsc.bsc_abpoPolygons[iPol], ulBrushFlags, atri);
      }
    }
  }

  abr.Unlock();

#ifdef SEMCRAFT_GAME_TSE
  // TSE's outdoor ground is terrain (height maps), not brushes. Its height map is read through ray casts (the
  // engine's Terrain.h doesn't compile with a modern compiler): straight down on a grid over each terrain, keeping
  // the hits on that terrain, then two triangles per grid cell.
  INDEX ctTerrains = 0, ctRays = 0;
  const DWORD tmStart = GetTickCount();
  FOREACHINDYNAMICCONTAINER(pwo->wo_cenEntities, CEntity, iten) {
    CEntity *pen = &*iten;
    if ((pen->GetFlags() & ENF_DELETED) || pen->en_RenderType != CEntity::RT_TERRAIN) continue;

    FLOATaabbox3D box;
    pen->GetBoundingBox(box);
    const FLOAT3D vSize = box.Size();
    if (vSize(1) <= 0 || vSize(3) <= 0) continue;
    ctTerrains++;

    // about a metre apart, coarser on huge terrains (at most ~250k rays)
    FLOAT fStep = 1.0f;
    while ((vSize(1) / fStep) * (vSize(3) / fStep) > 250000.0f) fStep *= 1.5f;
    const INDEX ctX = (INDEX)ceil(vSize(1) / fStep) + 1, ctZ = (INDEX)ceil(vSize(3) / fStep) + 1;

    std::vector<FLOAT> afY((size_t)ctX * ctZ, -1e30f);
    for (INDEX iz = 0; iz < ctZ; iz++) {
      for (INDEX ix = 0; ix < ctX; ix++) {
        const FLOAT fX = Min(box.minvect(1) + ix * fStep, box.maxvect(1));
        const FLOAT fZ = Min(box.minvect(3) + iz * fStep, box.maxvect(3));
        CCastRay cr(NULL, FLOAT3D(fX, box.maxvect(2) + 1.0f, fZ), FLOAT3D(fX, box.minvect(2) - 1.0f, fZ));
        cr.cr_ttHitModels = CCastRay::TT_NONE;
        cr.cr_bHitTranslucentPortals = FALSE;
        cr.cr_penHit = NULL;
        // keep going through whatever else is in the way until this terrain is hit
        for (INDEX iTry = 0; iTry < 4; iTry++) {
          pwo->CastRay(cr);
          ctRays++;
          if (cr.cr_penHit == NULL || cr.cr_penHit == pen) break;
          const FLOAT3D vFrom = cr.cr_vHit - FLOAT3D(0, 0.05f, 0);
          cr = CCastRay(NULL, vFrom, FLOAT3D(fX, box.minvect(2) - 1.0f, fZ));
          cr.cr_ttHitModels = CCastRay::TT_NONE;
          cr.cr_bHitTranslucentPortals = FALSE;
        }
        if (cr.cr_penHit == pen) afY[(size_t)iz * ctX + ix] = cr.cr_vHit(2);
      }
    }

    for (INDEX iz = 0; iz + 1 < ctZ; iz++) {
      for (INDEX ix = 0; ix + 1 < ctX; ix++) {
        const FLOAT ay[4] = { afY[(size_t)iz * ctX + ix], afY[(size_t)iz * ctX + ix + 1], afY[(size_t)(iz + 1) * ctX + ix], afY[(size_t)(iz + 1) * ctX + ix + 1] };
        if (ay[0] < -1e29f || ay[1] < -1e29f || ay[2] < -1e29f || ay[3] < -1e29f) continue; // a hole, or the edge
        FLOAT3D av[4];
        for (INDEX k = 0; k < 4; k++) {
          av[k] = FLOAT3D(Min(box.minvect(1) + (ix + (k & 1)) * fStep, box.maxvect(1)), ay[k], Min(box.minvect(3) + (iz + (k >> 1)) * fStep, box.maxvect(3)));
        }

        // two triangles per cell, normals facing up (into the air above the ground)
        const INDEX aiTri[2][3] = { { 0, 2, 1 }, { 1, 2, 3 } };
        for (INDEX t = 0; t < 2; t++) {
          Tri tri;
          for (INDEX k = 0; k < 3; k++) {
            const FLOAT3D &v = av[aiTri[t][k]];
            tri.v[k * 3 + 0] = v(1); tri.v[k * 3 + 1] = v(2); tri.v[k * 3 + 2] = v(3);
          }
          FLOAT3D vN = (av[aiTri[t][1]] - av[aiTri[t][0]]) * (av[aiTri[t][2]] - av[aiTri[t][0]]);
          if (vN(2) < 0) vN = -vN;
          const FLOAT fLen = vN.Length();
          if (fLen < 1e-6f) continue;
          vN /= fLen;
          tri.n[0] = vN(1); tri.n[1] = vN(2); tri.n[2] = vN(3);
          tri.ulFlags = 0;
          atri.push_back(tri);
        }
      }
    }
  }
  if (ctTerrains > 0) {
    CPrintF("[SemCraft2] %d terrain(s) sampled as ground: %d rays in %d ms\n", ctTerrains, ctRays, (INDEX)(GetTickCount() - tmStart));
  }
#endif

  if (atri.empty()) {
    CPrintF("^cff8000[SemCraft2]^r level %s has no brush triangles\n", fnmWorld.str_String);
    return ex;
  }

  // bounds and a content hash (the same level loaded again reuses Minecraft's shell)
  FLOAT afMin[3] = { 1e30f, 1e30f, 1e30f }, afMax[3] = { -1e30f, -1e30f, -1e30f };
  ULONG64 ullHash = 14695981039346656037ULL;
  ullHash = Fnv(ullHash, fnmWorld.str_String, strlen(fnmWorld.str_String));

  for (size_t i = 0; i < atri.size(); i++) {
    for (INDEX k = 0; k < 9; k++) {
      const INDEX a = k % 3;
      afMin[a] = Min(afMin[a], atri[i].v[k]);
      afMax[a] = Max(afMax[a], atri[i].v[k]);
    }
    ullHash = Fnv(ullHash, &atri[i], sizeof(Tri));
  }

  char strHash[32];
  sprintf(strHash, "%08x%08x", (ULONG)(ullHash >> 32), (ULONG)ullHash);

  char strTemp[MAX_PATH];
  GetTempPathA(MAX_PATH, strTemp);
  std::string strDir = std::string(strTemp) + "SemCraft2";
  CreateDirectoryA(strDir.c_str(), NULL);
  std::string strFile = strDir + "\\level_" + strHash + ".tri";

  FILE *f = fopen(strFile.c_str(), "wb");
  if (f == NULL) {
    CPrintF("^cff0000[SemCraft2]^r can't write %s\n", strFile.c_str());
    return ex;
  }

  const ULONG ulMagic = 0x31544353; // "SCT1"
  const ULONG ulVersion = 1, ulCount = (ULONG)atri.size(), ulReserved = 0;
  fwrite(&ulMagic, 4, 1, f);
  fwrite(&ulVersion, 4, 1, f);
  fwrite(&ulCount, 4, 1, f);
  fwrite(&ulReserved, 4, 1, f);
  fwrite(afMin, 4, 3, f);
  fwrite(afMax, 4, 3, f);
  fwrite(&atri[0], sizeof(Tri), atri.size(), f);
  const BOOL bOK = !ferror(f);
  fclose(f);

  if (!bOK) {
    CPrintF("^cff0000[SemCraft2]^r writing %s failed\n", strFile.c_str());
    return ex;
  }

  CPrintF("^c00ff00[SemCraft2]^r level exported: %d brushes (%d moving), %d triangles, bounds (%.0f %.0f %.0f)-(%.0f %.0f %.0f)\n",
    ctBrushes, ctMoving, (INDEX)atri.size(), afMin[0], afMin[1], afMin[2], afMax[0], afMax[1], afMax[2]);

  ex.strFile = strFile;
  ex.strHash = strHash;
  ex.ctTris = (INDEX)atri.size();
  ex.bValid = TRUE;
  return ex;
};

}; // namespace
