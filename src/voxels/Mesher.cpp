#include "Mesher.hpp"

#include <tracy/Tracy.hpp>

namespace greedy_mesher {

void mesh(const uint8_t* voxels, MeshData& meshData, int lod) {
  ZoneScoped;
  meshData.vertexCount = 0;
  size_t vertexI = 0;
  int cs = k_chunk_len >> lod;
  int cs_p = cs + 2;

  meshData.resize(cs);
  auto& opaqueMask = meshData.opaqueMask;
  auto& faceMasks = meshData.faceMasks;
  auto& forwardMerged = meshData.forwardMerged;
  auto& rightMerged = meshData.rightMerged;

  uint64_t P_MASK = ~(1ull << (cs + 1) | 1);

  // Hidden face culling
  for (int a = 1; a < cs_p - 1; a++) {
    const int aCS_P = a * cs_p;

    for (int b = 1; b < cs_p - 1; b++) {
      uint64_t columnBits = opaqueMask[(a * cs_p) + b] & P_MASK;
      const int baIndex = (b - 1) + (a - 1) * cs;
      const int abIndex = (a - 1) + (b - 1) * cs;

      faceMasks[baIndex + 0 * cs * cs] = (columnBits & ~opaqueMask[aCS_P + cs_p + b]) >> 1;
      faceMasks[baIndex + 1 * cs * cs] = (columnBits & ~opaqueMask[aCS_P - cs_p + b]) >> 1;

      faceMasks[abIndex + 2 * cs * cs] = (columnBits & ~opaqueMask[aCS_P + (b + 1)]) >> 1;
      faceMasks[abIndex + 3 * cs * cs] = (columnBits & ~opaqueMask[aCS_P + (b - 1)]) >> 1;

      faceMasks[baIndex + 4 * cs * cs] = columnBits & ~(opaqueMask[aCS_P + b] >> 1);
      faceMasks[baIndex + 5 * cs * cs] = columnBits & ~(opaqueMask[aCS_P + b] << 1);
    }
  }

  // Greedy meshing faces 0-3
  for (int face = 0; face < 4; face++) {
    const int axis = face / 2;

    const int faceVertexBegin = vertexI;

    for (int layer = 0; layer < cs; layer++) {
      const int bitsLocation = layer * cs + face * (cs * cs);

      for (int forward = 0; forward < cs; forward++) {
        uint64_t bitsHere = faceMasks[forward + bitsLocation];
        if (bitsHere == 0) {
          continue;
        }

        const uint64_t bitsNext = forward + 1 < cs ? faceMasks[(forward + 1) + bitsLocation] : 0;

        uint8_t rightMerged = 1;
        while (bitsHere) {
          uint64_t bitPos;
#ifdef _MSC_VER
          _BitScanForward64(&bitPos, bitsHere);
#else
          bitPos = __builtin_ctzll(bitsHere);
#endif

          const uint8_t type = voxels[getAxisIndex(axis, forward + 1, bitPos + 1, layer + 1, cs_p)];
          uint8_t& forwardMergedRef = forwardMerged[bitPos];

          if ((bitsNext >> bitPos & 1) &&
              type == voxels[getAxisIndex(axis, forward + 2, bitPos + 1, layer + 1, cs_p)]) {
            forwardMergedRef++;
            bitsHere &= ~(1ull << bitPos);
            continue;
          }

          for (int right = bitPos + 1; right < cs; right++) {
            if (!(bitsHere >> right & 1) || forwardMergedRef != forwardMerged[right] ||
                type != voxels[getAxisIndex(axis, forward + 1, right + 1, layer + 1, cs_p)]) {
              break;
            }
            forwardMerged[right] = 0;
            rightMerged++;
          }
          bitsHere &= ~((1ull << (bitPos + rightMerged)) - 1);

          const uint8_t meshFront = forward - forwardMergedRef;
          const uint8_t meshLeft = bitPos;
          const uint8_t meshUp = layer + (~face & 1);

          const uint8_t meshWidth = rightMerged;
          const uint8_t meshLength = forwardMergedRef + 1;

          forwardMergedRef = 0;
          rightMerged = 1;

          uint64_t quad;
          switch (face) {
            case 0:
            case 1:
              quad = getQuad(meshFront + (face == 1 ? meshLength : 0), meshUp, meshLeft, meshLength,
                             meshWidth, type);
              break;
            case 2:
            case 3:
              quad = getQuad(meshUp, meshFront + (face == 2 ? meshLength : 0), meshLeft, meshLength,
                             meshWidth, type);
              break;
            default:
              quad = 0;
          }

          insertQuad(*meshData.vertices, quad, vertexI);
        }
      }
    }

    const int faceVertexLength = vertexI - faceVertexBegin;
    meshData.faceVertexBegin[face] = faceVertexBegin;
    meshData.faceVertexLength[face] = faceVertexLength;
  }

  // Greedy meshing faces 4-5
  for (int face = 4; face < 6; face++) {
    const int axis = face / 2;

    const int faceVertexBegin = vertexI;

    for (int forward = 0; forward < cs; forward++) {
      const int bitsLocation = forward * cs + face * (cs * cs);
      const int bitsForwardLocation = (forward + 1) * cs + face * (cs * cs);

      for (int right = 0; right < cs; right++) {
        uint64_t bitsHere = faceMasks[right + bitsLocation];
        if (bitsHere == 0) continue;

        const uint64_t bitsForward = forward < cs - 1 ? faceMasks[right + bitsForwardLocation] : 0;
        const uint64_t bitsRight = right < cs - 1 ? faceMasks[right + 1 + bitsLocation] : 0;
        const int rightCS = right * cs;

        while (bitsHere) {
          uint64_t bitPos;
#ifdef _MSC_VER
          _BitScanForward64(&bitPos, bitsHere);
#else
          bitPos = __builtin_ctzll(bitsHere);
#endif

          bitsHere &= ~(1ull << bitPos);

          const uint8_t type = voxels[getAxisIndex(axis, right + 1, forward + 1, bitPos, cs_p)];
          uint8_t& forwardMergedRef = forwardMerged[rightCS + (bitPos - 1)];
          uint8_t& rightMergedRef = rightMerged[bitPos - 1];

          if (rightMergedRef == 0 && (bitsForward >> bitPos & 1) &&
              type == voxels[getAxisIndex(axis, right + 1, forward + 2, bitPos, cs_p)]) {
            forwardMergedRef++;
            continue;
          }

          if ((bitsRight >> bitPos & 1) &&
              forwardMergedRef == forwardMerged[(rightCS + cs) + (bitPos - 1)] &&
              type == voxels[getAxisIndex(axis, right + 2, forward + 1, bitPos, cs_p)]) {
            forwardMergedRef = 0;
            rightMergedRef++;
            continue;
          }

          const uint8_t meshLeft = right - rightMergedRef;
          const uint8_t meshFront = forward - forwardMergedRef;
          const uint8_t meshUp = bitPos - 1 + (~face & 1);

          const uint8_t meshWidth = 1 + rightMergedRef;
          const uint8_t meshLength = 1 + forwardMergedRef;

          forwardMergedRef = 0;
          rightMergedRef = 0;

          const uint64_t quad = getQuad(meshLeft + (face == 4 ? meshWidth : 0), meshFront, meshUp,
                                        meshWidth, meshLength, type);

          insertQuad(*meshData.vertices, quad, vertexI);
        }
      }
    }

    const int faceVertexLength = vertexI - faceVertexBegin;
    meshData.faceVertexBegin[face] = faceVertexBegin;
    meshData.faceVertexLength[face] = faceVertexLength;
  }

  meshData.vertexCount = vertexI + 1;
}
}  // namespace greedy_mesher
