#define DRAW_COUNT_REQUIRED
#include "root_sig.hlsl"
#include "shared_imgui.h"

float4 ConvertUint32RGBAtoFloat4(uint c) {
  float r = (c & 0xFF) / 255.0;
  float g = ((c >> 8) & 0xFF) / 255.0;
  float b = ((c >> 16) & 0xFF) / 255.0;
  float a = ((c >> 24) & 0xFF) / 255.0;
  return float4(r, g, b, a);
}

VOut main(uint vert_id : SV_VertexID) {
  VOut o;
  ImGuiVertex vert = bindless_buffers[pc.vert_buf_idx].Load<ImGuiVertex>(
      (vert_id + GetVertexIndex()) * sizeof(ImGuiVertex));
  o.pos = mul(pc.proj, float4(vert.position, 0.0, 1.0));
  o.uv = vert.tex_coords;
  o.color = ConvertUint32RGBAtoFloat4(vert.color);
  if ((pc.flags & IMGUI_FLAG_SRGB_COLOR) != 0) {
    o.color.rgb = pow(o.color.rgb, 2.2);
  }
  return o;
}
