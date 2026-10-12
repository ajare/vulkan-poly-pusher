#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <vector>

namespace mpp
{
	enum class ParticleSpawnShape : uint32_t
	{
		Point,
		Line,
		Box,
		Sphere,
		Hemisphere,
		Disc,
		Cone
	};

	enum class ParticleBehaviourModule : uint32_t
	{
		Gravity = 1u << 0,
		Drag = 1u << 1,
		Noise = 1u << 2,
		Collision = 1u << 3,
		CurlNoise = 1u << 4,
		Turbulence = 1u << 5,
		VectorField = 1u << 6
	};

	enum class ParticleCollisionSource : uint32_t
	{
		ScreenSpace = 1u << 0,
		Analytical = 1u << 1,
		SignedDistanceField = 1u << 2
	};

	constexpr ParticleCollisionSource operator |(ParticleCollisionSource left, ParticleCollisionSource right) noexcept
	{
		return ParticleCollisionSource(uint32_t(left) | uint32_t(right));
	}

	enum class ParticleCollisionResponse : uint32_t
	{
		Bounce,
		Slide,
		Stop,
		Kill,
		SpawnSecondaryEffect
	};

	enum class ParticleColliderShape : uint32_t
	{
		Plane,
		Sphere,
		Box,
		Capsule
	};

	enum class ParticleFlag : uint32_t
	{
		Colliding = 1u << 0,
		CollisionEvent = 1u << 1,
		SpawnSecondaryEffect = 1u << 2
	};

	// Event trigger and action values are authored wire values. Secondary particle
	// bursts remain GPU-owned; the other actions are optional typed notifications
	// that cross through ParticleSystem's frame-lagged, non-blocking callback ring.
	enum class ParticleEventTrigger : uint32_t
	{
		Spawn,
		Death,
		Collision,
		Age
	};

	enum class ParticleEventAction : uint32_t
	{
		SecondaryParticleBurst,
		Decal,
		Audio,
		Light,
		GameplayCallback
	};

	struct ParticleEventRule
	{
		ParticleEventTrigger trigger{ ParticleEventTrigger::Spawn };
		ParticleEventAction action{ ParticleEventAction::SecondaryParticleBurst };
		// Index within the reusable particle effect asset. ParticleSystem remaps it
		// to the target live emitter before uploading the rule.
		uint32_t targetEmitterTemplate{ 0 };
		uint32_t count{ 1 };
		float age{ 0.0f };
		uint32_t payload{ 0 };
		// Runtime generation of the remapped target. Authored rules leave this at
		// zero; ParticleSystem uses it to prevent a destroyed target slot from
		// redirecting queued GPU work to an unrelated replacement emitter.
		uint32_t targetEmitterGeneration{ 0 };
	};

	// CPU mirror of one externally consumable GPU event. The callback action and
	// payload select application-owned decal, audio, light, or gameplay work.
	struct alignas(16) ParticleEvent
	{
		std::array<float, 4> positionAge{};
		std::array<float, 4> velocityLifetime{};
		// World-space contact normal for collision events; xyz is zero otherwise.
		// The GPU uses w internally to validate a secondary-burst target generation.
		std::array<float, 4> normalAndPadding{};
		// Trigger, action, source-emitter index, source-emitter generation.
		std::array<uint32_t, 4> typeAndSource{};
		// Authored payload, target emitter, burst count, deterministic event seed.
		std::array<uint32_t, 4> payloadAndSecondary{};

		ParticleEventTrigger getTrigger() const noexcept { return ParticleEventTrigger(typeAndSource[0]); }
		ParticleEventAction getAction() const noexcept { return ParticleEventAction(typeAndSource[1]); }
		uint32_t getSourceEmitterIndex() const noexcept { return typeAndSource[2]; }
		uint32_t getSourceEmitterGeneration() const noexcept { return typeAndSource[3]; }
		uint32_t getPayload() const noexcept { return payloadAndSecondary[0]; }
	};

	// GPU-only form of an event rule after target-emitter remapping. age is carried
	// as float bits so the record remains two naturally aligned uvec4 values.
	struct alignas(16) ParticleGpuEventRule
	{
		// Trigger, action, target live emitter, secondary burst count.
		std::array<uint32_t, 4> configuration{};
		// Age float bits, payload, target emitter generation, then padding.
		std::array<uint32_t, 4> parameters{};
	};

	struct alignas(16) ParticleEventStorageHeader
	{
		uint32_t queueCountA{ 0 };
		uint32_t queueCountB{ 0 };
		uint32_t externalCount{ 0 };
		uint32_t droppedCount{ 0 };
	};

	// Particle appearance enums are stored directly in TemplateRenderData::modes.
	// Animation playback occupies the low byte; RandomStart is an independent bit
	// so it can be combined with either playback mode.
	enum class ParticleBillboardMode : uint32_t
	{
		CameraFacing,
		ScreenAligned,
		Cylindrical,
		AxisLocked,
		VelocityAligned,
		VelocityStretched
	};

	enum class ParticleTextureAnimation : uint32_t
	{
		None = 0u,
		FrameOverLife = 1u,
		FixedRate = 2u,
		RandomStart = 1u << 8
	};

	constexpr ParticleTextureAnimation operator |(ParticleTextureAnimation left, ParticleTextureAnimation right) noexcept
	{
		return ParticleTextureAnimation(uint32_t(left) | uint32_t(right));
	}

	enum class ParticleBlendClass : uint32_t
	{
		Additive,
		Alpha,
		WeightedOit
	};

	enum class ParticleSortMode : uint32_t
	{
		None,
		BackToFront
	};

	// Mesh particles share simulation and compaction with billboards, but are
	// selected by a dedicated geometry pass.  The wire value lives in the second
	// sorting word, which was reserved in version-one particle assets.
	enum class ParticleRenderMode : uint32_t
	{
		Billboard,
		Mesh
	};

	// Runtime visibility flags belong to a live particle effect rather than its
	// reusable asset. More view masks can be added without changing particle data.
	enum class ParticleEffectVisibilityFlag : uint32_t
	{
		Visible = 1u << 0
	};

	// Lighting is deliberately emitter-scoped. Even when an emitter owns hundreds
	// of thousands of particles, these flags produce at most one proxy light and
	// one volumetric draw instance for that emitter.
	enum class ParticleLightingFlag : uint32_t
	{
		ProxyLight = 1u << 0,
		PbrLightInjection = 1u << 1,
		VolumetricContribution = 1u << 2
	};

	constexpr ParticleLightingFlag operator |(ParticleLightingFlag left, ParticleLightingFlag right) noexcept
	{
		return ParticleLightingFlag(uint32_t(left) | uint32_t(right));
	}

	inline constexpr uint32_t ParticleTexturePlaybackMask = 0xffu;
	inline constexpr uint32_t ParticleTextureRandomStartBit = uint32_t(ParticleTextureAnimation::RandomStart);
	inline constexpr float MaximumParticleDeltaSeconds = 0.1f;

	// Scalar slots are packed generically four channels per LUT row. Keep the
	// authored semantics here so adding a slot only changes this assignment; the
	// baker itself is independent of the number and meaning of curves.
	enum class ParticleScalarCurve : uint32_t
	{
		Size,
		Alpha,
		VelocityMultiplier,
		Drag,
		RotationSpeed,
		EmissiveIntensity,
		Count
	};

	struct ParticleCurveKey
	{
		float time{ 0.0f };
		float value{ 1.0f };
	};

	struct ParticleCurve
	{
		float defaultValue{ 1.0f };
		std::vector<ParticleCurveKey> keys;
	};

	struct ParticleGradientKey
	{
		float time{ 0.0f };
		std::array<float, 3> colour{ 1.0f, 1.0f, 1.0f };
	};

	struct ParticleGradient
	{
		std::array<float, 3> defaultColour{ 1.0f, 1.0f, 1.0f };
		std::vector<ParticleGradientKey> keys;
	};

	constexpr float clampParticleDeltaSeconds(float seconds) noexcept
	{
		return seconds <= 0.0f ? 0.0f :
			(seconds > MaximumParticleDeltaSeconds ? MaximumParticleDeltaSeconds : seconds);
	}

	// CPU reference for the shader's flipbook selection. This is also useful to
	// asset previews that need to show the exact frame the runtime will choose.
	inline uint32_t particleFlipbookFrame(uint32_t frameCount, uint32_t animation,
		float age, float lifetime, float fixedRate, uint32_t seed) noexcept
	{
		frameCount = frameCount == 0u ? 1u : frameCount;
		uint32_t frame = 0u;
		auto const playback = animation & ParticleTexturePlaybackMask;
		if (playback == uint32_t(ParticleTextureAnimation::FrameOverLife) && lifetime > 0.0f)
		{
			float const normalizedAge = age <= 0.0f ? 0.0f : (age >= lifetime ? 1.0f : age / lifetime);
			frame = normalizedAge >= 1.0f ? frameCount - 1u : uint32_t(normalizedAge * float(frameCount));
		}
		else if (playback == uint32_t(ParticleTextureAnimation::FixedRate) && age > 0.0f && fixedRate > 0.0f)
		{
			frame = uint32_t(age * fixedRate) % frameCount;
		}
		if ((animation & ParticleTextureRandomStartBit) != 0u) frame = (frame + seed % frameCount) % frameCount;
		return frame;
	}

	// CPU-side state for one continuous emitter. Keeping the fractional part is
	// what makes rates below one particle per frame deterministic and smooth.
	struct ParticleSpawnAccumulator
	{
		double remainder{ 0.0 };

		uint32_t accumulate(float particlesPerSecond, float rateMultiplier, float dt) noexcept
		{
			if (!(particlesPerSecond > 0.0f) || !(rateMultiplier > 0.0f) || !(dt > 0.0f)) return 0u;
			double const accumulated = remainder + double(particlesPerSecond) * double(rateMultiplier) * double(dt);
			if (accumulated >= double(UINT32_MAX))
			{
				remainder = 0.0;
				return UINT32_MAX;
			}
			uint32_t const whole = uint32_t(accumulated);
			remainder = accumulated - double(whole);
			return whole;
		}
	};

	// CPU mirror of the std430 particle record. vec3-plus-float values are
	// represented as four scalars so the layout does not depend on GLM alignment
	// options. The matching GLSL declaration lives in ParticleShaders.h.
	struct alignas(16) ParticleRecord
	{
		std::array<float, 4> positionAge{};
		std::array<float, 4> velocityLifetime{};
		uint32_t packedColour{ 0xffffffffu };
		float baseSize{ 1.0f };
		float rotation{ 0.0f };
		float angularVelocity{ 0.0f };
		uint32_t emitterIndex{ 0 };
		uint32_t seed{ 0 };
		uint32_t flags{ 0 };
		uint32_t padding{ 0 };
	};

	// Simulation and spawn data are deliberately separate from appearance data:
	// the simulation kernel dereferences this record for every active particle
	// and never needs a texture handle, atlas mode, or blend class.
	struct alignas(16) EmitterSimData
	{
		// Column-major world transform.
		std::array<float, 16> transform{
			1.0f, 0.0f, 0.0f, 0.0f,
			0.0f, 1.0f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			0.0f, 0.0f, 0.0f, 1.0f
		};
		// Line half-vector, box half-extents, or radius/height for radial shapes.
		std::array<float, 4> shapeParameters{};
		std::array<float, 4> initialVelocityMin{};
		std::array<float, 4> initialVelocityMax{};
		std::array<float, 4> colourMin{ 1.0f, 1.0f, 1.0f, 1.0f };
		std::array<float, 4> colourMax{ 1.0f, 1.0f, 1.0f, 1.0f };
		// lifetime min/max, base-size min/max.
		std::array<float, 4> lifetimeSizeRanges{ 1.0f, 1.0f, 1.0f, 1.0f };
		// rotation min/max, angular-velocity min/max.
		std::array<float, 4> rotationRanges{};
		// Spawn shape, emitter seed, behaviour-module mask, particle budget.
		std::array<uint32_t, 4> shapeSeedModulesBudget{};
		// Emission mode (continuous/burst), enabled, burst count, emitter-template index.
		std::array<uint32_t, 4> emissionState{ 0u, 1u, 0u, 0u };
		// Authored continuous spawn rate in particles/second, effect visibility
		// flags (stored as an exactly representable integer), then padding.
		std::array<float, 4> emissionRateAndPadding{ 0.0f, float(uint32_t(ParticleEffectVisibilityFlag::Visible)), 0.0f, 0.0f };
		// Spawn-rate, size, speed, and lifetime multipliers.
		std::array<float, 4> parameterMultipliers0{ 1.0f, 1.0f, 1.0f, 1.0f };
		// Alpha and emissive multipliers, then padding.
		std::array<float, 4> parameterMultipliers1{ 1.0f, 1.0f, 0.0f, 0.0f };
		// Fixed behaviour-module slots consumed by the simulation kernel.
		std::array<float, 4> gravityAndDrag{};
		std::array<float, 4> noiseFrequencyStrength{};
		std::array<float, 4> noiseScrollAndTimeScale{};
		// Curl noise has the same spatial controls as basic noise. Turbulence adds
		// octave count, lacunarity and gain in the first three channels of its final slot.
		std::array<float, 4> curlNoiseFrequencyStrength{};
		std::array<float, 4> curlNoiseScrollAndTimeScale{};
		std::array<float, 4> turbulenceFrequencyStrength{};
		std::array<float, 4> turbulenceScrollAndTimeScale{};
		std::array<float, 4> turbulenceOctavesLacunarityGain{ 1.0f, 2.0f, 0.5f, 0.0f };
		// The vector-field texture is shared world data; these controls map world
		// position into its texture domain and scale its decoded [-1, 1] vectors.
		std::array<float, 4> vectorFieldFrequencyStrength{};
		std::array<float, 4> vectorFieldScrollAndTimeScale{};
		// Collision source mask, response, then reserved words. Collision remains a
		// runtime-branched behaviour module, like gravity, drag, and noise.
		std::array<uint32_t, 4> collisionConfiguration{
			uint32_t(ParticleCollisionSource::Analytical), uint32_t(ParticleCollisionResponse::Bounce), 0u, 0u
		};
		// Restitution, friction, particle-radius scale, and screen-space thickness.
		std::array<float, 4> collisionParameters{ 0.5f, 0.0f, 1.0f, 0.1f };
		// Offset/count in the flattened GPU event-rule table, stable event-source
		// generation, and whether this emitter may receive secondary bursts.
		// Filled by ParticleSystem each frame.
		std::array<uint32_t, 4> eventRange{};
	};

	// A world-space analytical collider. Plane stores normal/distance in first;
	// sphere stores centre/radius in first; box stores centre in first, half
	// extents in second, and an XYZW orientation quaternion in third; capsule stores
	// endpoint/radius in first and endpoint B in second.
	struct alignas(16) ParticleCollider
	{
		std::array<uint32_t, 4> shapeAndPadding{};
		std::array<float, 4> first{};
		std::array<float, 4> second{};
		std::array<float, 4> third{ 0.0f, 0.0f, 0.0f, 1.0f };
	};

	// One optional world signed-distance field. worldToTexture maps world positions
	// to [0,1]^3; sampledDistance = (red - isoValue) * distanceScale.
	struct alignas(16) ParticleSignedDistanceFieldData
	{
		std::array<float, 16> worldToTexture{
			1.0f, 0.0f, 0.0f, 0.0f,
			0.0f, 1.0f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			0.0f, 0.0f, 0.0f, 1.0f
		};
		// Distance scale, iso value, enabled, padding.
		std::array<float, 4> parameters{ 1.0f, 0.0f, 0.0f, 0.0f };
	};

	// Authored emitter-level lighting. Direct-light injection is bounded by the
	// renderer's PBR light capacity, while the volumetric path draws one proxy
	// volume per emitter rather than manufacturing one light per Particle.
	struct alignas(16) ParticleEmitterLighting
	{
		// RGB radiance colour and direct-light intensity.
		std::array<float, 4> colourAndIntensity{ 1.0f, 1.0f, 1.0f, 1.0f };
		// World-space radius, volumetric intensity multiplier, then padding.
		std::array<float, 4> rangeAndVolumetric{ 1.0f, 1.0f, 0.0f, 0.0f };
		std::array<uint32_t, 4> flagsAndPadding{};
	};

	// CPU-built GPU record for the emitter-level volumetric draw. positionRange is
	// refreshed from the live emitter transform; the authored fields remain intact.
	struct alignas(16) ParticleVolumetricLightingGpuData
	{
		std::array<float, 4> positionAndRange{};
		std::array<float, 4> colourAndIntensity{};
		std::array<float, 4> volumetricAndPadding{};
		std::array<uint32_t, 4> flagsAndPadding{};
	};

	constexpr bool particleHasLighting(ParticleEmitterLighting const& lighting, ParticleLightingFlag flag) noexcept
	{
		return (lighting.flagsAndPadding[0] & uint32_t(flag)) != 0u;
	}

	// Draw-only emitter-template data. Culling also reads this record while
	// compacting the render list; spawn and simulation never fetch it.
	struct alignas(16) TemplateRenderData
	{
		// Shared albedo-array layer, flags, flipbook columns, and flipbook rows.
		// Layer zero is the renderer-owned white fallback. Flag bit zero says the
		// appearance has an authored texture; bit one requests sRGB decoding after
		// the source texels have been copied into the array's linear RGBA8 storage.
		std::array<uint32_t, 4> textureAndAtlas{ 0u, 0u, 1u, 1u };
		// RGB tint and alpha multiplier.
		std::array<float, 4> tintAndAlpha{ 1.0f, 1.0f, 1.0f, 1.0f };
		// Emissive intensity, soft fade distance, animation rate, LUT row.
		std::array<float, 4> appearance{ 1.0f, 0.0f, 0.0f, 0.0f };
		// Frame count, animation mode, billboard mode, blend class.
		std::array<uint32_t, 4> modes{ 1u, 0u, 0u, 0u };
		// Maximum draw distance, minimum projected diameter in pixels, runtime mesh
		// bounds radius, and distortion strength in normalized screen coordinates.
		// Zero disables the corresponding authored test/output.
		std::array<float, 4> culling{};
		// Sort mode, render mode, distortion-output enable, then a reserved word.
		// Back-to-front sorting is honoured only by conventional alpha billboards;
		// mesh particles have their own material-driven geometry pass.
		std::array<uint32_t, 4> sorting{};
	};

	constexpr bool particleTemplateRendersMesh(TemplateRenderData const& appearance) noexcept
	{
		return appearance.sorting[1] == uint32_t(ParticleRenderMode::Mesh);
	}

	constexpr bool particleAppearanceWritesDistortion(TemplateRenderData const& appearance) noexcept
	{
		return !particleTemplateRendersMesh(appearance) && appearance.sorting[2] != 0u && appearance.culling[3] != 0.0f;
	}

	constexpr bool particleAppearanceRequiresDepthSort(TemplateRenderData const& appearance) noexcept
	{
		return !particleTemplateRendersMesh(appearance) &&
			appearance.modes[3] == uint32_t(ParticleBlendClass::Alpha) &&
			appearance.sorting[0] == uint32_t(ParticleSortMode::BackToFront);
	}

	struct ParticleSortRecord
	{
		uint32_t key{ 0 };
		uint32_t particleIndex{ 0 };
	};

	// One CPU-to-GPU spawn command. spawnCounter is the first logical spawn
	// number represented by this command; no counter is stored per particle.
	struct alignas(16) ParticleSpawnCommand
	{
		uint32_t emitterIndex{ 0 };
		uint32_t count{ 0 };
		uint32_t randomSeed{ 0 };
		uint32_t spawnCounter{ 0 };
	};

	// Header of the GPU counter buffer. Per-template live counts immediately
	// follow this header. Compaction rebuilds those counts, spawning consumes
	// them for the template budget, and the statistics path copies this same
	// buffer rather than maintaining another set of counters.
	struct alignas(16) ParticleCounterHeader
	{
		uint32_t freeCount{ 0 };
		uint32_t activeCountA{ 0 };
		uint32_t activeCountB{ 0 };
		uint32_t droppedSpawnCount{ 0 };
		uint32_t spawnedCount{ 0 };
		uint32_t killedCount{ 0 };
		uint32_t renderedCount{ 0 };
		uint32_t culledCount{ 0 };
	};

	// Binary layout required by glMultiDrawArraysIndirect. first encodes the
	// template's render-list offset as offset * 4, allowing GLSL 4.30 to recover
	// the range without requiring ARB_shader_draw_parameters.
	struct ParticleDrawArraysIndirectCommand
	{
		uint32_t count{ 4 };
		uint32_t instanceCount{ 0 };
		uint32_t first{ 0 };
		uint32_t baseInstance{ 0 };
	};

	// Five words are accepted by glDrawElementsIndirect. Non-indexed mesh draws
	// use the first four words through glDrawArraysIndirect and leave padding zero.
	struct ParticleMeshDrawIndirectCommand
	{
		uint32_t count{ 0 };
		uint32_t instanceCount{ 0 };
		uint32_t first{ 0 };
		uint32_t baseVertexOrInstance{ 0 };
		uint32_t baseInstanceOrPadding{ 0 };
	};

	static_assert(std::is_standard_layout_v<ParticleRecord>);
	static_assert(offsetof(ParticleRecord, positionAge) == 0);
	static_assert(offsetof(ParticleRecord, velocityLifetime) == 16);
	static_assert(offsetof(ParticleRecord, packedColour) == 32);
	static_assert(offsetof(ParticleRecord, emitterIndex) == 48);
	static_assert(offsetof(ParticleRecord, padding) == 60);
	static_assert(sizeof(ParticleRecord) == 64, "The std430 particle array stride must be exactly 64 bytes.");
	static_assert(clampParticleDeltaSeconds(3.0f) == MaximumParticleDeltaSeconds);
	static_assert(sizeof(ParticleEvent) == 80);
	static_assert(sizeof(ParticleGpuEventRule) == 32);
	static_assert(sizeof(ParticleEventStorageHeader) == 16);
	static_assert(sizeof(EmitterSimData) == 464);
	static_assert(sizeof(ParticleCollider) == 64);
	static_assert(sizeof(ParticleSignedDistanceFieldData) == 80);
	static_assert(sizeof(ParticleEmitterLighting) == 48);
	static_assert(sizeof(ParticleVolumetricLightingGpuData) == 64);
	static_assert(sizeof(TemplateRenderData) == 96);
	static_assert(sizeof(ParticleSortRecord) == 8);
	static_assert(sizeof(ParticleSpawnCommand) == 16);
	static_assert(sizeof(ParticleCounterHeader) == 32);
	static_assert(sizeof(ParticleDrawArraysIndirectCommand) == 16);
	static_assert(sizeof(ParticleMeshDrawIndirectCommand) == 20);
}
