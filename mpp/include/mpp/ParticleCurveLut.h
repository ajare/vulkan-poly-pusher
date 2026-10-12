#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "mpp/Config.h"
#include "mpp/ParticleData.h"

namespace mpp
{
	struct ParticleEmitterTemplate;

	// CPU-baked, GPU-lazy lookup texture shared by every live instance of one
	// particle effect asset. Rows are fixed by emitter-template order; there is
	// deliberately no runtime row allocation API.
	class _MPPAPI ParticleEffectCurveLut
	{
		std::vector<float> mFloatTexels;
		std::vector<uint32_t> mRowOffsets;
		uint32_t mHeight{ 0 };
		uint32_t mTexture{ 0 };

		ParticleEffectCurveLut() = default;

	public:
		static constexpr uint32_t SampleCount = 256;
		static constexpr uint32_t ScalarCurveCount = uint32_t(ParticleScalarCurve::Count);
		static constexpr uint32_t ScalarRowCount = (ScalarCurveCount + 3u) / 4u;
		static constexpr uint32_t ColourGradientRow = ScalarRowCount;
		static constexpr uint32_t RowsPerTemplate = ScalarRowCount + 1u;

		~ParticleEffectCurveLut();
		ParticleEffectCurveLut(ParticleEffectCurveLut const&) = delete;
		ParticleEffectCurveLut& operator=(ParticleEffectCurveLut const&) = delete;

		static std::shared_ptr<ParticleEffectCurveLut> bake(std::span<ParticleEmitterTemplate const> emitterTemplates);

		uint32_t getWidth() const noexcept { return SampleCount; }
		uint32_t getHeight() const noexcept { return mHeight; }
		uint32_t getRowOffset(size_t emitterTemplateIndex) const;
		std::vector<float> const& getFloatTexels() const noexcept { return mFloatTexels; }

		// Uploads as RGBA16F on first use and binds with linear, clamp-to-edge
		// sampling. Float source data lets OpenGL perform the IEEE half conversion.
		void bind(uint32_t textureUnit);
		void unload();
		uint32_t getTextureId() const noexcept { return mTexture; }
	};
}
