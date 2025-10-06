#ifndef DISPATCH_MESH_SHARED_H
#define DISPATCH_MESH_SHARED_H

enum EncodeMeshDrawArgsE {
  EncodeMeshDrawArgs_MainVertexBuf,
  EncodeMeshDrawArgs_MeshletBuf,
  EncodeMeshDrawArgs_InstanceDataBuf,
  EncodeMeshDrawArgs_MeshDataBuf,
  EncodeMeshDrawArgs_MeshletVerticesBuf,
  EncodeMeshDrawArgs_MeshletTrianglesBuf,
  EncodeMeshDrawArgs_SceneArgBuf,
  EncodeMeshDrawArgs_MeshletVisBuf,
};

enum PerFrameArgs {
  PerFrameArgs_MainUniformBuf,
  PerFrameArgs_CullDataBuf,
};

#endif
