#ifndef SHARED_MESHLET_DRAW_STATS_HLSLI
#define SHARED_MESHLET_DRAW_STATS_HLSLI

#include "shader_core.h"

struct MeshletDrawStats {
  uint meshlets_drawn_early;
  uint meshlets_drawn_late;
  uint triangles_drawn_early;
  uint triangles_drawn_late;
};

#ifdef __HLSL__

static const uint kMeshletDrawStatPassesPerCategory = 2u;

static void MeshletDrawStats_AtomicAdd(RWStructuredBuffer<uint> stats, uint category, uint pass_idx,
                                       uint delta) {
  uint unused;
  InterlockedAdd(stats[category * kMeshletDrawStatPassesPerCategory + pass_idx], delta, unused);
}

void MeshletDrawStats_AddMeshlets(RWStructuredBuffer<uint> stats, uint pass, uint delta) {
  MeshletDrawStats_AtomicAdd(stats, 0, pass, delta);
}

void MeshletDrawStats_AddTriangles(RWStructuredBuffer<uint> stats, uint pass, uint tri_count,
                                   bool draw) {
  MeshletDrawStats_AtomicAdd(stats, 1, pass, tri_count * (draw ? 1u : 0u));
}

#endif

#endif  // SHARED_MESHLET_DRAW_STATS_HLSLI
