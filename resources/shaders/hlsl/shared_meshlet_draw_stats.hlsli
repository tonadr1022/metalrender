// Layout must match C++ MeshletDrawStats (MemeRenderer123.hpp). Atomics use RWStructuredBuffer<uint>.

#ifndef SHARED_MESHLET_DRAW_STATS_HLSLI
#define SHARED_MESHLET_DRAW_STATS_HLSLI

struct MeshletDrawStats {
  uint meshlets_drawn_early;
  uint meshlets_drawn_late;
  uint triangles_drawn_early;
  uint triangles_drawn_late;
};

void MeshletDrawStats_AddMeshlets(RWStructuredBuffer<uint> stats, uint pass, uint delta) {
  uint unused;
  InterlockedAdd(stats[pass], delta, unused);
}

void MeshletDrawStats_AddTriangles(RWStructuredBuffer<uint> stats, uint pass, uint tri_count,
                                   bool draw) {
  uint unused;
  InterlockedAdd(stats[pass + 2], tri_count * (draw ? 1u : 0u), unused);
}

#endif  // SHARED_MESHLET_DRAW_STATS_HLSLI
