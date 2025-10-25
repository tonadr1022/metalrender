/*
MIT License

Copyright (c) 2020 Erik Johansson

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
 */

//   Define BM_IMPLEMENTATION in exactly one source file to include the implementation
//   and include the mesher.h without that define as often as needed
//
//   it could look like this
//
//   #define BM_IMPLEMENTATION
//   #include "mesher.h"
//
//   There are other defines to control the behaviour of the library.
//   * Define BM_VECTOR with your own vector implementation - otherwise it will use std::vector

#ifndef MESHER_H
#define MESHER_H

#include <cstdint>
#include <vector>

#include "chunk_shaders_shared.h"
namespace greedy_mesher {

struct MeshData {
  std::vector<uint64_t> faceMasks;     // CS_2 * 6
  std::vector<uint64_t> opaqueMask;    // CS_P2
  std::vector<uint8_t> forwardMerged;  // CS_2
  std::vector<uint8_t> rightMerged;    // CS
  std::vector<uint64_t>* vertices;
  int vertexCount = 0;
  std::array<uint32_t, 6> faceVertexBegin{};
  std::array<uint32_t, 6> faceVertexLength{};

  void resize(size_t cs = k_chunk_len) {
    size_t cs_p = cs + 2;
    faceMasks.resize(cs * cs * 6ull);
    opaqueMask.resize(cs_p * cs_p);
    forwardMerged.resize(cs_p * cs_p);
    rightMerged.resize(cs);
    vertices->resize(10000);
  }
};

// @param[in] voxels: The input data includes duplicate edge data from neighboring chunks which is
// used for visibility culling. For optimal performance, your world data should already be
// structured this way so that you can feed the data straight into this algorithm. Input data is
// ordered in ZXY and is 64^3 which results in a 62^3 mesh.
//
// @param[out] meshData The allocated vertices in MeshData with a length of meshData.vertexCount.
void mesh(const uint8_t* voxels, MeshData& meshData, int lod);

static inline int getAxisIndex(const int axis, const int a, const int b, const int c, int cs_p) {
  int cs_p2 = cs_p * cs_p;
  if (axis == 0) {
    return b + (a * cs_p) + (c * cs_p2);
  }
  if (axis == 1) {
    return b + (c * cs_p) + (a * cs_p2);
  }
  return c + (a * cs_p) + (b * cs_p2);
}

inline void insertQuad(std::vector<uint64_t>& vertices, uint64_t quad, size_t& vertexI) {
  if (vertexI >= vertices.size() - 6ull) {
    vertices.resize(vertices.size() * 2ull, 0);
  }

  vertices[vertexI] = quad;

  vertexI++;
}

static inline uint64_t getQuad(uint64_t x, uint64_t y, uint64_t z, uint64_t w, uint64_t h,
                               uint64_t type) {
  return (type << 32) | (h << 24) | (w << 18) | (z << 12) | (y << 6) | x;
}

}  // namespace greedy_mesher
#endif  // MESHER_H
