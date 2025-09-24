#ifndef DISPATCH_MESH_SHARED_H
#define DISPATCH_MESH_SHARED_H

enum EncodeMeshDrawArgsE {
  EncodeMeshDrawArgs_MainVertexBuf,
  EncodeMeshDrawArgs_MeshletBuf,
  EncodeMeshDrawArgs_InstanceModelMatrixBuf,
  EncodeMeshDrawArgs_InstanceDataBuf,
  EncodeMeshDrawArgs_MeshletVerticesBuf,
  EncodeMeshDrawArgs_MeshletTrianglesBuf,
  EncodeMeshDrawArgs_MainUniformBuf,
  EncodeMeshDrawArgs_SceneArgBuf,
};

struct DispatchMeshParams {
  uint32_t tot_meshes;
};
#endif
