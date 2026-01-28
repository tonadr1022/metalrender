# TODOs

- either all const ref or all std::move strings in RenderGraph
- instead of dirty textures in render graph, transition them to shader read first
- use fill_buffer() instead of compute for clear_bufs pass
- actual readback
- meshlet stats buffer actual readback
- move device\_->copy_to_buffer outside device interface.
- staging buffer for texture uploads
- consolidate upload functions with imgui renderer
- meshlet visibility bit buffer, meshlet occlusion culling
- figure out synchronization in metal3 command encoder officially
- modularize MemeRenderer123
- profile meshlets vs vertex shaders on bistro-like scene
- vulkan support
- re-add voxels
