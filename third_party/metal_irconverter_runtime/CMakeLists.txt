add_library(metal_irconverter_runtime INTERFACE)

target_include_directories(metal_irconverter_runtime
    SYSTEM INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# target_compile_options(metal_irconverter_runtime INTERFACE
#     $<$<CXX_COMPILER_ID:AppleClang>:
#         -Wno-gnu-anonymous-struct
#         -Wno-gnu-anon-struct
#         -Wno-error=gnu-anonymous-struct
#         -Wno-c99-extensions
#         -Wno-error=c99-extensions
#     >
# )
