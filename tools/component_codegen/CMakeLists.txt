find_package(LLVM CONFIG REQUIRED)
find_package(Clang CONFIG REQUIRED)

add_executable(teng-component-codegen
    ComponentCodegen.cpp
    ComponentCodegenUtil.cpp
)

target_include_directories(teng-component-codegen
    PRIVATE
        ${LLVM_INCLUDE_DIRS}
        ${CLANG_INCLUDE_DIRS}
)
target_compile_definitions(teng-component-codegen PRIVATE ${LLVM_DEFINITIONS})
target_compile_features(teng-component-codegen PRIVATE cxx_std_23)

target_link_libraries(teng-component-codegen
    PRIVATE
        clangAST
        clangASTMatchers
        clangBasic
        clangFrontend
        clangSerialization
        clangTooling
)
