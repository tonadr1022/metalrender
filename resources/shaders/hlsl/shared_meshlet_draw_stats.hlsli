#ifndef SHARED_MESHLET_DRAW_STATS_HLSLI
#define SHARED_MESHLET_DRAW_STATS_HLSLI

#include "shader_core.h"

#define MESHLET_DRAW_STATS_CATEGORY_MESHLETS 0
#define MESHLET_DRAW_STATS_CATEGORY_TRIANGLES 1

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

#endif

#endif  // SHARED_MESHLET_DRAW_STATS_HLSLI
