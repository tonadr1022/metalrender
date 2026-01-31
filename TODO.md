# TODOs

- instead of dirty textures in render graph, transition them to shader read first
- out*counts_buf* use render graph instead
- AllModelData is instance data mgr
- sometimes the end timestamp is less than start timestamp. This is ridiculous.
- too much stuff is in device begin/end frame. should be exposed at lower level.
- either all const ref or all std::move strings in RenderGraph
- consolidate tex upload functions with imgui renderer
- meshlet visibility bit buffer instead of uint32_t
- figure out synchronization in metal3 command encoder officially
- modularize MemeRenderer123
- vulkan support
- re-add voxels
