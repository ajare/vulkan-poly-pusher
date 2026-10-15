cmake_minimum_required(VERSION 3.22)
project(VulkanPolyPusher VERSION 0.1 LANGUAGES C CXX)
include(CTest)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
option(VPP_BUILD_RENDERER "Build the Vulkan triangle/clear/present subset" ON)
if(VPP_BUILD_RENDERER)
    find_package(Vulkan 1.3 REQUIRED)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    add_subdirectory(ext/sdl EXCLUDE_FROM_ALL)
endif()
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/ext/utils/vendor/yaml-cpp/CMakeLists.txt")
    message(FATAL_ERROR "Initialize pinned dependencies: git submodule update --init --recursive")
endif()
add_subdirectory(ext/utils EXCLUDE_FROM_ALL)
target_include_directories(Utils PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/ext/utils/include")
add_library(mpp_vendor_headers INTERFACE)
target_include_directories(mpp_vendor_headers INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/vendor/include")
function(cpu_module target module export)
    set(sources ${ARGN})
    list(TRANSFORM sources PREPEND "${module}/src/")
    add_library(${target} SHARED ${sources})
    target_include_directories(${target} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/${module}/include")
    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/compat")
    target_compile_definitions(${target} PRIVATE ${export} NOMINMAX)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_options(${target} PRIVATE "LINKER:--no-undefined")
    endif()
    target_link_libraries(${target} PUBLIC mpp_vendor_headers)
endfunction()
cpu_module(MppData mpp-data MPP_DATA_DLL_EXPORT StructuredData.cpp)
cpu_module(MppMesh mpp-mesh MPP_MESH_DLL_EXPORT MaterialInformation.cpp MeshDefinition.cpp MeshSpecification.cpp Vertex.cpp VertexBufferAttributeLayout.cpp VertexBufferDefinition.cpp VertexData.cpp)
cpu_module(MppProgram mpp-program MPP_PROGRAM_DLL_EXPORT Attribute.cpp Parser.cpp ShaderStage.cpp)
target_include_directories(MppProgram PRIVATE mpp-program/include/mpp/program)
target_sources(MppProgram PRIVATE mpp-program/include/mpp/program/glslRegistry.cpp)
target_link_libraries(MppProgram PUBLIC MppMesh PRIVATE Utils)
cpu_module(MassivePolyPusher mpp MPP_DLL_EXPORT
    Camera.cpp Colour.cpp ClipRectangle.cpp Diagnostic.cpp DiagnosticTests.cpp
    MppException.cpp SceneDocument.cpp LegacyPipelineDocument.cpp PbrPipelineDocument.cpp
    RenderGraph.cpp RenderGraphTests.cpp RenderGraphPassFactoryRegistry.cpp
    UniformCollection.cpp TextureParams.cpp SamplerParams.cpp PbrMaterialFeatures.cpp PbrMaterialTests.cpp
    ParticleEffectValidator.cpp ParticleEffectBounds.cpp Resource.cpp ResourceWrangler.cpp ResourceStream.cpp
    String.cpp
    TextureStreamBase.cpp TextureStream.cpp TextureData.cpp SamplerStream.cpp
    BasicMaterialStream.cpp PbrMaterialStream.cpp PostEffectMaterialStream.cpp ProgramStream.cpp
    ProgrammaticProgramStream.cpp ProgrammaticTextureStream.cpp ProgrammaticBasicMaterialStream.cpp
    ParticleEffectStream.cpp ProgrammaticParticleEffectStream.cpp StringStream.cpp FileDataStream.cpp
    Logger.cpp StaticLogger.cpp ResourceStreamSerializer.cpp
    ProgrammaticPbrMaterialStream.cpp ProgrammaticPostEffectMaterialStream.cpp
    ProgrammaticSamplerStream.cpp ProgrammaticStringStream.cpp)
# Pinned, implementation-private runtime shader toolchain (also in CPU-only builds).
set(BUILD_EXTERNAL OFF CACHE BOOL "" FORCE)
set(ENABLE_OPT OFF CACHE BOOL "" FORCE)
set(GLSLANG_TESTS OFF CACHE BOOL "" FORCE)
set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "" FORCE)
set(ENABLE_SPVREMAPPER OFF CACHE BOOL "" FORCE)
set(ENABLE_HLSL OFF CACHE BOOL "" FORCE)
set(GLSLANG_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
add_subdirectory(ext/glslang EXCLUDE_FROM_ALL)
add_library(vpp_spirv_reflect STATIC ext/spirv-reflect/spirv_reflect.c)
target_include_directories(vpp_spirv_reflect PUBLIC ext/spirv-reflect)
target_sources(MassivePolyPusher PRIVATE compat/CpuDefinitions.cpp mpp/src/vulkan/ProgramCompiler.cpp)
if(NOT VPP_BUILD_RENDERER)
    # CPU-only boundary stubs; the renderer build replaces them with the
    # native resource/program/material implementations below.
    target_sources(MassivePolyPusher PRIVATE compat/CpuResourceBoundary.cpp)
endif()
target_link_libraries(MassivePolyPusher PRIVATE glslang::glslang glslang::glslang-default-resource-limits vpp_spirv_reflect)
target_link_libraries(MassivePolyPusher PUBLIC MppData MppMesh MppProgram PRIVATE Utils)
if(VPP_BUILD_RENDERER)
    target_sources(MassivePolyPusher PRIVATE mpp/src/vulkan/WindowBridge.cpp mpp/src/vulkan/RenderSystem.cpp
        mpp/src/vulkan/GeometryStore.cpp mpp/src/vulkan/Mesh.cpp mpp/src/vulkan/VertexBuffer.cpp
        mpp/src/vulkan/PersistentMappedBuffer.cpp mpp/src/vulkan/GeometryDiagnostics.cpp
        mpp/src/vulkan/PipelineStore.cpp mpp/src/vulkan/Program.cpp mpp/src/vulkan/BasicMaterial.cpp
        mpp/src/vulkan/ResourceManager.cpp mpp/src/vulkan/UniformBinding.cpp mpp/src/vulkan/PipelineDiagnostics.cpp)
    target_link_libraries(MassivePolyPusher PRIVATE Vulkan::Vulkan SDL3::SDL3)
endif()
cpu_module(MppResourceParsers mpp-resource-parsers MPP_RESOURCE_PARSERS_DLL_EXPORT
    SceneParser.cpp SceneSerializer.cpp RenderGraphParser.cpp RenderGraphSerializer.cpp
    LegacyPipelineParser.cpp LegacyPipelineSerializer.cpp LegacyPipelineDocumentLoader.cpp
    PbrPipelineParser.cpp PbrPipelineSerializer.cpp PbrPipelineDocumentLoader.cpp
    ParticleEffectParser.cpp ParticleEffectSerializer.cpp ParticleResourceTests.cpp RenderGraphResourceTests.cpp
    LegacyPipelineResourceValidator.cpp PbrPipelineResourceValidator.cpp
    FileStream.cpp FileTextureStream.cpp FileSamplerStream.cpp FileProgramStream.cpp FileStringStream.cpp
    FileBasicMaterialStream.cpp FilePbrMaterialStream.cpp FilePostEffectMaterialStream.cpp FileParticleEffectStream.cpp
    MeshSpecificationParser.cpp Serializer.cpp XmlSerializer.cpp YamlSerializer.cpp DocumentWriter.cpp)
target_link_libraries(MppResourceParsers PUBLIC MassivePolyPusher PRIVATE Utils)
cpu_module(MppMeshSpecificationParser mpp-mesh-specification-parser MPP_MESH_SPECIFICATION_PARSER_DLL_EXPORT ModelspecStream.cpp ProgramInformation.cpp SpecificationParser.cpp)
target_link_libraries(MppMeshSpecificationParser PUBLIC MppResourceParsers PRIVATE Utils)
cpu_module(MppHelper mpp-helper MPP_HELPER_DLL_EXPORT FpsCamera.cpp FreeCamera.cpp OrthoCamera.cpp)
target_link_libraries(MppHelper PUBLIC MassivePolyPusher)
find_package(Threads REQUIRED)
add_library(MppAppSupport STATIC mpp-app-support/src/BackgroundWork.cpp mpp-app-support/src/CommandStack.cpp mpp-app-support/src/DocumentFile.cpp mpp-app-support/src/DocumentFoundationTests.cpp mpp-app-support/src/DocumentId.cpp mpp-app-support/src/PackageManifest.cpp mpp-app-support/src/ZipArchive.cpp)
target_include_directories(MppAppSupport PUBLIC mpp-app-support/include)
target_link_libraries(MppAppSupport PUBLIC MassivePolyPusher Utils Threads::Threads)
if(VPP_BUILD_RENDERER)
    target_sources(MppAppSupport PRIVATE mpp-app-support/src/Window.cpp mpp-app-support/src/WindowSDL.cpp mpp-app-support/src/InputManager.cpp mpp-app-support/src/InputManagerSDL.cpp mpp-app-support/src/Timer.cpp mpp-app-support/src/TimerSDL.cpp)
    target_include_directories(MppAppSupport PRIVATE mpp/src/vulkan)
    # SDL is part of the frozen WindowSDL public interface; Vulkan remains private.
    target_link_libraries(MppAppSupport PUBLIC SDL3::SDL3)
    add_executable(VppClearPresent tools/ClearPresent.cpp)
    target_compile_definitions(VppClearPresent PRIVATE VPP_VULKAN)
    target_link_libraries(VppClearPresent PRIVATE MppAppSupport)
    add_executable(VppGeometryUpload tools/GeometryUpload.cpp)
    target_compile_definitions(VppGeometryUpload PRIVATE VPP_VULKAN)
    target_link_libraries(VppGeometryUpload PRIVATE MppAppSupport)
    add_executable(VppTrianglePresent tools/TrianglePresent.cpp)
    target_compile_definitions(VppTrianglePresent PRIVATE VPP_VULKAN VPP_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(VppTrianglePresent PRIVATE MppAppSupport MppResourceParsers)
    add_executable(VppProgramPipeline tools/ProgramPipeline.cpp)
    target_compile_definitions(VppProgramPipeline PRIVATE VPP_VULKAN VPP_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(VppProgramPipeline PRIVATE MppAppSupport MppResourceParsers)
endif()
add_executable(VppAssetValidate tools/AssetValidate.cpp)
target_link_libraries(VppAssetValidate PRIVATE MppResourceParsers)
add_executable(ProgramBuilder program-builder/src/Main.cpp)
target_link_libraries(ProgramBuilder PRIVATE MppMeshSpecificationParser MppProgram Utils)
if(BUILD_TESTING)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    if(VPP_BUILD_RENDERER)
        add_test(NAME vulkan_clear_present COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_clear_validation.py" "$<TARGET_FILE:VppClearPresent>" "${CMAKE_BINARY_DIR}/clear.ppm")
        set_tests_properties(vulkan_clear_present PROPERTIES TIMEOUT 120 LABELS "gpu;window")
        add_test(NAME vulkan_geometry_upload COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_geometry_validation.py" "$<TARGET_FILE:VppGeometryUpload>" "${CMAKE_BINARY_DIR}/geometry.log")
        set_tests_properties(vulkan_geometry_upload PROPERTIES TIMEOUT 120 LABELS "gpu;window")
        add_test(NAME vulkan_triangle_present COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_triangle_validation.py" "$<TARGET_FILE:VppTrianglePresent>" "${CMAKE_BINARY_DIR}/triangle.ppm")
        set_tests_properties(vulkan_triangle_present PROPERTIES TIMEOUT 120 LABELS "gpu;window")
        add_test(NAME vulkan_program_pipeline COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_pipeline_validation.py" "$<TARGET_FILE:VppProgramPipeline>" "${CMAKE_BINARY_DIR}/pipeline.log")
        set_tests_properties(vulkan_program_pipeline PROPERTIES TIMEOUT 120 LABELS "gpu;window")
    endif()
    add_test(NAME public_declarations COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/audit_public_headers.py" "${CMAKE_CURRENT_SOURCE_DIR}")
    add_library(VppPublicInterfaces OBJECT tests/PublicInterfaces.cpp)
    target_link_libraries(VppPublicInterfaces PRIVATE MppResourceParsers MppAppSupport MppMeshSpecificationParser MppHelper)
    add_executable(VppCpuTests tests/CpuTests.cpp tests/ShaderCompilationTests.cpp)
    target_compile_definitions(VppCpuTests PRIVATE VPP_CPU_ONLY VPP_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(VppCpuTests PRIVATE MppResourceParsers MppAppSupport MppMeshSpecificationParser MppHelper)
    if(VPP_BUILD_RENDERER)
        # CPU coverage for the pure Vulkan-boundary vertex-format translation
        # and the pipeline vertex-input/uniform-block translation.
        target_sources(VppCpuTests PRIVATE tests/GeometryTranslationTests.cpp tests/PipelineTranslationTests.cpp)
        target_compile_definitions(VppCpuTests PRIVATE VPP_GEOMETRY_TESTS)
        target_include_directories(VppCpuTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/mpp/src" "${CMAKE_CURRENT_SOURCE_DIR}/compat")
        target_link_libraries(VppCpuTests PRIVATE Vulkan::Vulkan)
    endif()
    add_test(NAME cpu_regressions COMMAND VppCpuTests)
    add_test(NAME authored_assets COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_assets.py" "$<TARGET_FILE:VppAssetValidate>")
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/tool-smoke")
    add_test(NAME program_builder COMMAND ProgramBuilder
        -v "${CMAKE_CURRENT_SOURCE_DIR}/program-builder/resources/test.vert"
        -f "${CMAKE_CURRENT_SOURCE_DIR}/program-builder/resources/test.frag"
        -m "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/program.modelspec")
    set_tests_properties(program_builder PROPERTIES WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/tool-smoke")
    set_tests_properties(cpu_regressions authored_assets PROPERTIES TIMEOUT 120)
endif()
