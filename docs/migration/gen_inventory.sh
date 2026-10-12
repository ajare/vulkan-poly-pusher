#!/usr/bin/env bash
# Generates the M01 public-header coverage inventory CSV from the frozen MPP reference.
# Run from the vulkan-poly-pusher repo root. Requires the frozen reference at $MPP_REF
# (default: /home/wm/code/mpp-ref) checked out at the pinned commit.
#
# Categories:
#   retained                     - backend-agnostic; interface and implementation reused unchanged
#   retained-interface/native-impl - public header kept; GL execution replaced by native Vulkan
#   narrow-decoupling             - public header kept with additive Vulkan hooks / GL vocab remapped
#   native-replacement           - GL-specific plumbing removed and replaced
#   test                         - test-only header
set -euo pipefail
MPP_REF="${MPP_REF:-/home/wm/code/mpp-ref}"
OUT="${1:-docs/migration/coverage-inventory.csv}"

# mpp headers whose GL execution becomes native Vulkan behind a retained interface.
NATIVE_IMPL=" RenderSystem.h RenderPipeline.h RenderPipelineFlow.h RenderPipelineOutput.h \
RenderPass.h RenderTarget.h RenderTexture.h Screen.h Program.h ComputeProgram.h \
UniformBuffer.h ShaderStorageBuffer.h VertexBuffer.h VertexBufferRenderCommand.h \
BatchRenderer.h TextureRenderer.h BufferRenderer.h RenderGraphExecutor.h RenderGraphFrameContext.h \
RenderGraphBuiltInPasses.h RenderGraphScenePass.h RenderOutputProcessor.h \
DefaultShaders.h PbrShaders.h ParticleShaders.h TrailShaders.h \
ParticleSystem.h ParticleDrawProgram.h TrailSystem.h IblEnvironmentCache.h Caps.h RenderInfo.h \
Mesh.h MeshInstance.h Model.h ModelInstance.h ParticleData.h Font.h InternalFont.h "

# GL-specific plumbing removed outright.
NATIVE_REPLACEMENT=" GLErrorCheck.h "

# Narrow decoupling: additive Vulkan resource-use declarations or GL numeric-vocab remap.
DECOUPLING=" Sampler.h SamplerParams.h SamplerStream.h TextureParams.h TextureData.h \
RenderGraphPassFactoryRegistry.h RenderGraphImportRegistry.h \
ImGuiBackendData.h ImGuiDataProvider.h ImGuiPlatform.h \
FileTextureStream.h FileSamplerStream.h "

# Target milestone by module (refined per-header where noted).
milestone_for() {
  local module="$1" header="$2"
  local base; base="$(basename "$header")"
  case "$base" in
    *Tests.h) echo "all" ;;
    # M1 = first rendering slice; M2 = geometry/resources/legacy; M3 = graphs; etc.
    RenderGraph*.h) echo "M3" ;;
    Pbr*.h) echo "M4" ;;
    AmbientOcclusion.h) echo "M5" ;;
    AntiAliasing.h) echo "M6" ;;
    *Trail*.h) echo "M10" ;;
    Particle*.h) echo "M8" ;;
    Legacy*.h) echo "M2" ;;
    *) case "$module" in
         mpp-data|mpp-mesh|mpp-mesh-specification-parser|mpp-program) echo "M1" ;;
         mpp-app-support) echo "M1" ;;
         mpp-helper) echo "M2" ;;
         mpp-resource-parsers) echo "M2" ;;
         mpp) echo "M1" ;;
       esac ;;
  esac
}

category_for() {
  local header="$1"
  local base; base="$(basename "$header")"
  case "$base" in *Tests.h) echo "test"; return;; esac
  for h in $NATIVE_REPLACEMENT; do [ "$base" = "$h" ] && { echo "native-replacement"; return; }; done
  for h in $DECOUPLING;       do [ "$base" = "$h" ] && { echo "narrow-decoupling"; return; }; done
  for h in $NATIVE_IMPL;      do [ "$base" = "$h" ] && { echo "retained-interface/native-impl"; return; }; done
  echo "retained"
}

echo "module,header,category,target_milestone" > "$OUT"
for d in mpp mpp-app-support mpp-data mpp-helper mpp-mesh mpp-mesh-specification-parser mpp-program mpp-resource-parsers; do
  while IFS= read -r hdr; do
    rel="${hdr#"$MPP_REF/$d/include/"}"
    cat="$(category_for "$rel")"
    ms="$(milestone_for "$d" "$rel")"
    echo "$d,$rel,$cat,$ms" >> "$OUT"
  done < <(find "$MPP_REF/$d/include" \( -name '*.h' -o -name '*.hpp' \) | sort)
done
echo "wrote $OUT ($(($(wc -l < "$OUT")-1)) rows)"
