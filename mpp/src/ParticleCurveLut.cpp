#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

#include <GL/glew.h>
#include <GL/gl.h>

#include "mpp/GLErrorCheck.h"
#include "mpp/ParticleCurveLut.h"
#include "mpp/ParticleSystem.h"

namespace mpp
{
	namespace
	{
		float normalizedTime(float value)
		{
			if (!std::isfinite(value)) return 0.0f;
			return std::clamp(value, 0.0f, 1.0f);
		}

		float sampleCurve(ParticleCurve const& curve, float time)
		{
			if (curve.keys.empty()) return curve.defaultValue;
			auto keys = curve.keys;
			std::stable_sort(keys.begin(), keys.end(), [](auto const& left, auto const& right)
				{ return normalizedTime(left.time) < normalizedTime(right.time); });
			if (time <= normalizedTime(keys.front().time)) return keys.front().value;
			if (time >= normalizedTime(keys.back().time)) return keys.back().value;
			auto right = std::upper_bound(keys.begin(), keys.end(), time, [](float value, auto const& key)
				{ return value < normalizedTime(key.time); });
			auto const& left = *(right - 1);
			float const leftTime = normalizedTime(left.time);
			float const rightTime = normalizedTime(right->time);
			float const amount = rightTime > leftTime ? (time - leftTime) / (rightTime - leftTime) : 1.0f;
			return left.value + (right->value - left.value) * amount;
		}

		std::array<float, 3> sampleGradient(ParticleGradient const& gradient, float time)
		{
			if (gradient.keys.empty()) return gradient.defaultColour;
			auto keys = gradient.keys;
			std::stable_sort(keys.begin(), keys.end(), [](auto const& left, auto const& right)
				{ return normalizedTime(left.time) < normalizedTime(right.time); });
			if (time <= normalizedTime(keys.front().time)) return keys.front().colour;
			if (time >= normalizedTime(keys.back().time)) return keys.back().colour;
			auto right = std::upper_bound(keys.begin(), keys.end(), time, [](float value, auto const& key)
				{ return value < normalizedTime(key.time); });
			auto const& left = *(right - 1);
			float const leftTime = normalizedTime(left.time);
			float const rightTime = normalizedTime(right->time);
			float const amount = rightTime > leftTime ? (time - leftTime) / (rightTime - leftTime) : 1.0f;
			std::array<float, 3> result;
			for (size_t channel = 0; channel < result.size(); ++channel)
				result[channel] = left.colour[channel] + (right->colour[channel] - left.colour[channel]) * amount;
			return result;
		}
	}

	ParticleEffectCurveLut::~ParticleEffectCurveLut()
	{
		unload();
	}

	std::shared_ptr<ParticleEffectCurveLut> ParticleEffectCurveLut::bake(
		std::span<ParticleEmitterTemplate const> emitterTemplates)
	{
		auto result = std::shared_ptr<ParticleEffectCurveLut>(new ParticleEffectCurveLut);
		result->mHeight = uint32_t(emitterTemplates.size()) * RowsPerTemplate;
		result->mFloatTexels.assign(size_t(SampleCount) * result->mHeight * 4u, 1.0f);
		result->mRowOffsets.reserve(emitterTemplates.size());

		for (size_t templateIndex = 0; templateIndex < emitterTemplates.size(); ++templateIndex)
		{
			uint32_t const rowOffset = uint32_t(templateIndex) * RowsPerTemplate;
			result->mRowOffsets.push_back(rowOffset);
			auto const& emitterTemplate = emitterTemplates[templateIndex];
			for (uint32_t x = 0; x < SampleCount; ++x)
			{
				float const time = float(x) / float(SampleCount - 1u);
				for (uint32_t curveIndex = 0; curveIndex < ScalarCurveCount; ++curveIndex)
				{
					uint32_t const row = rowOffset + curveIndex / 4u;
					uint32_t const channel = curveIndex % 4u;
					result->mFloatTexels[(size_t(row) * SampleCount + x) * 4u + channel] =
						sampleCurve(emitterTemplate.curves[curveIndex], time);
				}
				auto const colour = sampleGradient(emitterTemplate.colourGradient, time);
				size_t const gradientTexel = (size_t(rowOffset + ColourGradientRow) * SampleCount + x) * 4u;
				for (size_t channel = 0; channel < colour.size(); ++channel)
					result->mFloatTexels[gradientTexel + channel] = colour[channel];
			}
		}
		return result;
	}

	uint32_t ParticleEffectCurveLut::getRowOffset(size_t emitterTemplateIndex) const
	{
		return mRowOffsets.at(emitterTemplateIndex);
	}

	void ParticleEffectCurveLut::bind(uint32_t textureUnit)
	{
		if (mHeight == 0u) throw std::logic_error("A particle effect curve LUT requires at least one emitter template.");
		if (mTexture == 0u)
		{
			GL_CHECK(glGenTextures(1, &mTexture));
			GL_CHECK(glBindTexture(GL_TEXTURE_2D, mTexture));
			GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
			GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
			GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
			GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
			GL_CHECK(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, SampleCount, GLsizei(mHeight), 0,
				GL_RGBA, GL_FLOAT, mFloatTexels.data()));
			GL_CHECK(glObjectLabel(GL_TEXTURE, mTexture, -1, "Particle effect RGBA16F curve LUT"));
		}
		GL_CHECK(glActiveTexture(GL_TEXTURE0 + textureUnit));
		GL_CHECK(glBindSampler(textureUnit, 0));
		GL_CHECK(glBindTexture(GL_TEXTURE_2D, mTexture));
	}

	void ParticleEffectCurveLut::unload()
	{
		if (mTexture == 0u) return;
		GL_CHECK(glDeleteTextures(1, &mTexture));
		mTexture = 0u;
	}
}
