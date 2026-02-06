# TODOs

- timestamp query better organization
- flat map for gpu per-frame upload allocator
- queue management between metal 3 and 4 is actually awful. I let it go just to get metal 3
  working again
- fix cone culling
- handle y-flip on metal vs vulkan
- instead of acquiring next swapchain image in begin_swapchain_rendering(), maybe expose it?
- in begin_rendering() use image view instead of assuming default view
- modularize engine into separate libraries?
- DLL export macro. Not really relevant until Vulkan
- instead of dirty textures in render graph, transition them to shader read first
- out*counts_buf* use render graph instead
- sometimes the end timestamp is less than start timestamp. This is ridiculous.
- too much stuff is in device begin/end frame. should be exposed at lower level.
- either all const ref or all std::move strings in RenderGraph
- consolidate tex upload functions with imgui renderer
- meshlet visibility bit buffer instead of uint32_t
- figure out synchronization in metal3 command encoder officially
- modularize MemeRenderer123
- vulkan support
- re-add voxels
