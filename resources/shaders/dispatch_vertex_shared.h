#ifndef DISPATCH_VERTEX_SHARED_H
#define DISPATCH_VERTEX_SHARED_H

enum DispatchVertexShaderArgs {
  DispatchVertexShaderArgs_MainVertexBuf,
  DispatchVertexShaderArgs_MainIndexBuf,
  DispatchVertexShaderArgs_MeshDataBuf,
  DispatchVertexShaderArgs_SceneArgBuf,
  DispatchVertexShaderArgs_InstanceDataBuf,
};

struct DispatchVertexShaderParams {
  uint32_t tot_meshes;
  bool frustum_cull;
};

#endif
