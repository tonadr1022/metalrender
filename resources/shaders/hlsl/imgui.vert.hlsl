#include "root_sig.hlsl"
#include "shared_imgui.h"

struct DrawID {
  uint did;
  uint vert_id;
};

CONSTANT_BUFFER(DrawID, gDrawID, 999);

float4 ConvertUint32RGBAtoFloat4(uint c) {
  float r = (c & 0xFF) / 255.0;
  float g = ((c >> 8) & 0xFF) / 255.0;
  float b = ((c >> 16) & 0xFF) / 255.0;
  float a = ((c >> 24) & 0xFF) / 255.0;
  return float4(r, g, b, a);
}

VOut main(uint vert_id : SV_VertexID) {
  VOut o;
  ImGuiVertex vert = bindless_buffers[vert_buf_idx].Load<ImGuiVertex>((vert_id + gDrawID.vert_id) *
                                                                      sizeof(ImGuiVertex));
  o.pos = mul(proj, float4(vert.position, 0.0, 1.0));
  o.uv = vert.tex_coords;
  o.color = ConvertUint32RGBAtoFloat4(vert.color);
  return o;
}
