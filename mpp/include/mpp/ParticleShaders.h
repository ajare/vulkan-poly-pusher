#pragma once

namespace mpp
{
	// Initialises only GPU-owned allocation state. In particular the CPU never
	// manufactures free particle indices or counter values.
	inline char const* ParticlePoolInitialiseComputeShader = R"MPP(#version 430

layout(local_size_x = MPP_PARTICLE_WORK_GROUP_SIZE) in;

layout(std430, binding = 1) restrict writeonly buffer ParticleFreeIndices
{
    uint FREE_INDICES[];
};

layout(std430, binding = 4) restrict buffer ParticleCounters
{
    uint FREE_COUNT;
    uint ACTIVE_COUNT_A;
    uint ACTIVE_COUNT_B;
    uint DROPPED_SPAWN_COUNT;
    uint SPAWNED_COUNT;
    uint KILLED_COUNT;
    uint RENDERED_COUNT;
    uint CULLED_COUNT;
    uint LIVE_COUNTS[];
};

uniform uint POOL_CAPACITY;
uniform uint TEMPLATE_CAPACITY;

void main()
{
    uint index = gl_GlobalInvocationID.x;
    if (index < POOL_CAPACITY)
        FREE_INDICES[index] = index;
    if (index < TEMPLATE_CAPACITY)
        LIVE_COUNTS[index] = 0u;

    if (index == 0u)
    {
        FREE_COUNT = POOL_CAPACITY;
        ACTIVE_COUNT_A = 0u;
        ACTIVE_COUNT_B = 0u;
        DROPPED_SPAWN_COUNT = 0u;
        SPAWNED_COUNT = 0u;
        KILLED_COUNT = 0u;
        RENDERED_COUNT = 0u;
        CULLED_COUNT = 0u;
    }
}
)MPP";

	// Resets only the per-frame diagnostic counters. This dispatch exists only
	// while statistics are enabled and never touches the allocation counters.
	inline char const* ParticleStatisticsPrepareComputeShader = R"MPP(#version 430

layout(local_size_x = 1) in;

layout(std430, binding = 4) restrict buffer ParticleCounters
{
    uint FREE_COUNT;
    uint ACTIVE_COUNT_A;
    uint ACTIVE_COUNT_B;
    uint DROPPED_SPAWN_COUNT;
    uint SPAWNED_COUNT;
    uint KILLED_COUNT;
    uint RENDERED_COUNT;
    uint CULLED_COUNT;
    uint LIVE_COUNTS[];
};

uniform uint ACTIVE_LIST_INDEX;
uniform uint REQUESTED_SPAWN_COUNT;

void main()
{
    DROPPED_SPAWN_COUNT = 0u;
    SPAWNED_COUNT = REQUESTED_SPAWN_COUNT;
    KILLED_COUNT = ACTIVE_LIST_INDEX == 0u ? ACTIVE_COUNT_A : ACTIVE_COUNT_B;
    RENDERED_COUNT = 0u;
    CULLED_COUNT = 0u;
}
)MPP";

	// Resets event queues once per frame, then converts one selected queue count
	// into an indirect dispatch while clearing its cascade destination. A final
	// mode accounts for work beyond the validated cascade-depth bound.
	inline char const* ParticleEventPrepareComputeShader = R"MPP(#version 430

layout(local_size_x = 1) in;

struct EventRule { uvec4 configuration; uvec4 parameters; };
struct ParticleEventRecord { vec4 positionAge; vec4 velocityLifetime; vec4 normalAndPadding; uvec4 typeAndSource; uvec4 payloadAndSecondary; };
layout(std430, binding = 6) restrict buffer ParticleEvents
{
    uvec4 EVENT_COUNTS;
    EventRule EVENT_RULES[MPP_PARTICLE_MAX_EVENT_RULES];
    ParticleEventRecord EVENT_QUEUE_A[MPP_PARTICLE_MAX_GENERATED_EVENTS];
    ParticleEventRecord EVENT_QUEUE_B[MPP_PARTICLE_MAX_GENERATED_EVENTS];
    ParticleEventRecord EXTERNAL_EVENTS[MPP_PARTICLE_MAX_EXTERNAL_EVENTS];
};
layout(std430, binding = 7) restrict writeonly buffer ParticleEventDispatchCommand
{
    uint DISPATCH_COMMAND[];
};

uniform uint MODE;
uniform uint SOURCE_QUEUE;

void main()
{
    if (MODE == 0u)
    {
        EVENT_COUNTS = uvec4(0u);
        DISPATCH_COMMAND[0] = 0u;
        DISPATCH_COMMAND[1] = 1u;
        DISPATCH_COMMAND[2] = 1u;
        return;
    }

    uint sourceCount = min(SOURCE_QUEUE == 0u ? EVENT_COUNTS.x : EVENT_COUNTS.y,
        uint(MPP_PARTICLE_MAX_GENERATED_EVENTS));
    if (MODE == 2u)
    {
        atomicAdd(EVENT_COUNTS.w, sourceCount);
        if (SOURCE_QUEUE == 0u) EVENT_COUNTS.x = 0u; else EVENT_COUNTS.y = 0u;
        return;
    }

    if (SOURCE_QUEUE == 0u) EVENT_COUNTS.y = 0u; else EVENT_COUNTS.x = 0u;
    DISPATCH_COMMAND[0] = sourceCount;
    DISPATCH_COMMAND[1] = 1u;
    DISPATCH_COMMAND[2] = 1u;
}
)MPP";

	// One work group consumes one spawn command. Each lane handles a strided
	// subset of that command, so dispatch cost follows requested spawns rather
	// than pool capacity. Allocation and budget reservations use CAS loops: an
	// exhausted pool can never underflow FREE_COUNT or duplicate an index.
	inline char const* ParticleSpawnComputeShader = R"MPP(#version 430

layout(local_size_x = MPP_PARTICLE_WORK_GROUP_SIZE) in;

struct ParticleRecord
{
    vec4 positionAge;
    vec4 velocityLifetime;
    uint packedColour;
    float baseSize;
    float rotation;
    float angularVelocity;
    uint emitterIndex;
    uint seed;
    uint flags;
    uint padding;
};

struct EmitterSimData
{
    mat4 transform;
    vec4 shapeParameters;
    vec4 initialVelocityMin;
    vec4 initialVelocityMax;
    vec4 colourMin;
    vec4 colourMax;
    vec4 lifetimeSizeRanges;
    vec4 rotationRanges;
    uvec4 shapeSeedModulesBudget;
    uvec4 emissionState;
    vec4 emissionRateAndPadding;
    vec4 parameterMultipliers0;
    vec4 parameterMultipliers1;
    vec4 gravityAndDrag;
    vec4 noiseFrequencyStrength;
    vec4 noiseScrollAndTimeScale;
    vec4 curlNoiseFrequencyStrength;
    vec4 curlNoiseScrollAndTimeScale;
    vec4 turbulenceFrequencyStrength;
    vec4 turbulenceScrollAndTimeScale;
    vec4 turbulenceOctavesLacunarityGain;
    vec4 vectorFieldFrequencyStrength;
    vec4 vectorFieldScrollAndTimeScale;
    uvec4 collisionConfiguration;
    vec4 collisionParameters;
    uvec4 eventRange;
};

struct SpawnCommand
{
    uint emitterIndex;
    uint count;
    uint randomSeed;
    uint spawnCounter;
};

layout(std430, binding = 0) restrict writeonly buffer ParticlePool
{
    ParticleRecord PARTICLES[];
};
layout(std430, binding = 1) restrict buffer ParticleFreeIndices
{
    uint FREE_INDICES[];
};
layout(std430, binding = 2) restrict writeonly buffer ParticleActiveIndicesA
{
    uint ACTIVE_INDICES_A[];
};
layout(std430, binding = 3) restrict writeonly buffer ParticleActiveIndicesB
{
    uint ACTIVE_INDICES_B[];
};
layout(std430, binding = 4) restrict buffer ParticleCounters
{
    uint FREE_COUNT;
    uint ACTIVE_COUNT_A;
    uint ACTIVE_COUNT_B;
    uint DROPPED_SPAWN_COUNT;
    uint SPAWNED_COUNT;
    uint KILLED_COUNT;
    uint RENDERED_COUNT;
    uint CULLED_COUNT;
    uint LIVE_COUNTS[];
};
layout(std430, binding = 5) restrict readonly buffer ParticleEmitters
{
    EmitterSimData EMITTERS[];
};
struct EventRule { uvec4 configuration; uvec4 parameters; };
struct ParticleEventRecord { vec4 positionAge; vec4 velocityLifetime; vec4 normalAndPadding; uvec4 typeAndSource; uvec4 payloadAndSecondary; };
layout(std430, binding = 6) restrict buffer ParticleEvents
{
    uvec4 EVENT_COUNTS;
    EventRule EVENT_RULES[MPP_PARTICLE_MAX_EVENT_RULES];
    ParticleEventRecord EVENT_QUEUE_A[MPP_PARTICLE_MAX_GENERATED_EVENTS];
    ParticleEventRecord EVENT_QUEUE_B[MPP_PARTICLE_MAX_GENERATED_EVENTS];
    ParticleEventRecord EXTERNAL_EVENTS[MPP_PARTICLE_MAX_EXTERNAL_EVENTS];
};
layout(std430, binding = 7) restrict readonly buffer ParticleSpawnCommands
{
    SpawnCommand SPAWN_COMMANDS[];
};
uniform uint EMITTER_COUNT;
uniform uint TEMPLATE_COUNT;
uniform uint SPAWN_COMMAND_OFFSET;
uniform uint ACTIVE_LIST_INDEX;

uint hashValue(uint value)
{
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    value ^= value >> 16u;
    return value;
}

float randomScalar(inout uint state)
{
    state = hashValue(state + 0x9e3779b9u);
    return float(state) * (1.0 / 4294967296.0);
}

vec3 randomUnitVector(inout uint state)
{
    float z = randomScalar(state) * 2.0 - 1.0;
    float angle = randomScalar(state) * 6.28318530718;
    float radius = sqrt(max(0.0, 1.0 - z * z));
    return vec3(cos(angle) * radius, z, sin(angle) * radius);
}

vec3 sampleShape(uint shape, vec4 parameters, inout uint state)
{
    if (shape == 0u) // point
        return vec3(0.0);
    if (shape == 1u) // line, centred on the emitter
        return parameters.xyz * (randomScalar(state) * 2.0 - 1.0);
    if (shape == 2u) // box volume
        return parameters.xyz * vec3(
            randomScalar(state) * 2.0 - 1.0,
            randomScalar(state) * 2.0 - 1.0,
            randomScalar(state) * 2.0 - 1.0);
    if (shape == 3u) // sphere volume
        return randomUnitVector(state) * (parameters.x * pow(randomScalar(state), 1.0 / 3.0));
    if (shape == 4u) // hemisphere volume, local +Y
    {
        vec3 direction = randomUnitVector(state);
        direction.y = abs(direction.y);
        return direction * (parameters.x * pow(randomScalar(state), 1.0 / 3.0));
    }
    if (shape == 5u) // disc area, local XZ
    {
        float angle = randomScalar(state) * 6.28318530718;
        float radius = parameters.x * sqrt(randomScalar(state));
        return vec3(cos(angle) * radius, 0.0, sin(angle) * radius);
    }
    // Cone volume, apex at the origin and axis along local +Y.
    float heightFraction = pow(randomScalar(state), 1.0 / 3.0);
    float angle = randomScalar(state) * 6.28318530718;
    float radius = parameters.x * heightFraction * sqrt(randomScalar(state));
    return vec3(cos(angle) * radius, parameters.y * heightFraction, sin(angle) * radius);
}

bool reserveTemplateParticle(uint templateIndex, uint budget)
{
    uint observed = LIVE_COUNTS[templateIndex];
    while (observed < budget)
    {
        uint previous = atomicCompSwap(LIVE_COUNTS[templateIndex], observed, observed + 1u);
        if (previous == observed) return true;
        observed = previous;
    }
    return false;
}

bool popFreeIndex(out uint particleIndex)
{
    uint observed = FREE_COUNT;
    while (observed > 0u)
    {
        uint previous = atomicCompSwap(FREE_COUNT, observed, observed - 1u);
        if (previous == observed)
        {
            particleIndex = FREE_INDICES[observed - 1u];
            return true;
        }
        observed = previous;
    }
    return false;
}

void appendActiveIndex(uint particleIndex)
{
    uint destination;
    if (ACTIVE_LIST_INDEX == 0u)
    {
        destination = atomicAdd(ACTIVE_COUNT_A, 1u);
        ACTIVE_INDICES_A[destination] = particleIndex;
    }
    else
    {
        destination = atomicAdd(ACTIVE_COUNT_B, 1u);
        ACTIVE_INDICES_B[destination] = particleIndex;
    }
}

void publishSpawnEvents(ParticleRecord particle, EmitterSimData emitter)
{
    for (uint ordinal = 0u; ordinal < emitter.eventRange.y; ++ordinal)
    {
        EventRule rule = EVENT_RULES[emitter.eventRange.x + ordinal];
        if (rule.configuration.x != 0u &&
            !(rule.configuration.x == 3u && uintBitsToFloat(rule.parameters.x) <= 0.0)) continue;
        uint destination = atomicAdd(EVENT_COUNTS.x, 1u);
        if (destination >= MPP_PARTICLE_MAX_GENERATED_EVENTS)
        {
            atomicAdd(EVENT_COUNTS.w, 1u);
            continue;
        }
        ParticleEventRecord event;
        event.positionAge = particle.positionAge;
        event.velocityLifetime = particle.velocityLifetime;
        event.normalAndPadding = vec4(0.0, 0.0, 0.0, uintBitsToFloat(rule.parameters.z));
        event.typeAndSource = uvec4(rule.configuration.xy, particle.emitterIndex, emitter.eventRange.z);
        event.payloadAndSecondary = uvec4(rule.parameters.y, rule.configuration.zw,
            hashValue(particle.seed ^ ordinal));
        EVENT_QUEUE_A[destination] = event;
    }
}

void main()
{
    SpawnCommand command = SPAWN_COMMANDS[SPAWN_COMMAND_OFFSET + gl_WorkGroupID.x];
    if (command.emitterIndex >= EMITTER_COUNT)
    {
        for (uint ordinal = gl_LocalInvocationID.x; ordinal < command.count; ordinal += gl_WorkGroupSize.x)
            atomicAdd(DROPPED_SPAWN_COUNT, 1u);
        return;
    }

    EmitterSimData emitter = EMITTERS[command.emitterIndex];
    if (emitter.emissionState.y == 0u) return;

    uint templateIndex = emitter.emissionState.w;
    if (templateIndex >= TEMPLATE_COUNT)
    {
        for (uint ordinal = gl_LocalInvocationID.x; ordinal < command.count; ordinal += gl_WorkGroupSize.x)
            atomicAdd(DROPPED_SPAWN_COUNT, 1u);
        return;
    }

    uint budget = emitter.shapeSeedModulesBudget.w;
    for (uint ordinal = gl_LocalInvocationID.x; ordinal < command.count; ordinal += gl_WorkGroupSize.x)
    {
        if (!reserveTemplateParticle(templateIndex, budget))
        {
            atomicAdd(DROPPED_SPAWN_COUNT, 1u);
            continue;
        }

        uint particleIndex;
        if (!popFreeIndex(particleIndex))
        {
            atomicAdd(LIVE_COUNTS[templateIndex], 0xffffffffu);
            atomicAdd(DROPPED_SPAWN_COUNT, 1u);
            continue;
        }

        uint seed = hashValue(emitter.shapeSeedModulesBudget.y ^ command.randomSeed);
        seed = hashValue(seed ^ particleIndex);
        seed = hashValue(seed ^ (command.spawnCounter + ordinal));
        uint randomState = seed;

        vec3 localPosition = sampleShape(emitter.shapeSeedModulesBudget.x, emitter.shapeParameters, randomState);
        vec3 velocityMix = vec3(randomScalar(randomState), randomScalar(randomState), randomScalar(randomState));
        vec3 localVelocity = mix(emitter.initialVelocityMin.xyz, emitter.initialVelocityMax.xyz, velocityMix);
        localVelocity *= emitter.parameterMultipliers0.z;

        float lifetime = mix(emitter.lifetimeSizeRanges.x, emitter.lifetimeSizeRanges.y, randomScalar(randomState));
        lifetime *= emitter.parameterMultipliers0.w;
        float size = mix(emitter.lifetimeSizeRanges.z, emitter.lifetimeSizeRanges.w, randomScalar(randomState));
        size *= emitter.parameterMultipliers0.y;
        float rotation = mix(emitter.rotationRanges.x, emitter.rotationRanges.y, randomScalar(randomState));
        float angularVelocity = mix(emitter.rotationRanges.z, emitter.rotationRanges.w, randomScalar(randomState));
        vec4 colour = mix(emitter.colourMin, emitter.colourMax, vec4(
            randomScalar(randomState), randomScalar(randomState), randomScalar(randomState), randomScalar(randomState)));

        ParticleRecord particle;
        particle.positionAge = vec4((emitter.transform * vec4(localPosition, 1.0)).xyz, 0.0);
        particle.velocityLifetime = vec4(mat3(emitter.transform) * localVelocity, lifetime);
        particle.packedColour = packUnorm4x8(clamp(colour, 0.0, 1.0));
        particle.baseSize = size;
        particle.rotation = rotation;
        particle.angularVelocity = angularVelocity;
        particle.emitterIndex = command.emitterIndex;
        particle.seed = seed;
        particle.flags = 0u;
        particle.padding = 0u;
        PARTICLES[particleIndex] = particle;
        appendActiveIndex(particleIndex);
        publishSpawnEvents(particle, emitter);
    }
}
)MPP";

	// Converts the GPU-owned active count into an indirect compute command and
	// clears only the destination state. This constant-cost dispatch is what lets
	// the simulation kernel itself run over the active list rather than capacity.
	inline char const* ParticleSimulationPrepareComputeShader = R"MPP(#version 430

layout(local_size_x = 1) in;

layout(std430, binding = 4) restrict buffer ParticleCounters
{
    uint FREE_COUNT;
    uint ACTIVE_COUNT_A;
    uint ACTIVE_COUNT_B;
    uint DROPPED_SPAWN_COUNT;
    uint SPAWNED_COUNT;
    uint KILLED_COUNT;
    uint RENDERED_COUNT;
    uint CULLED_COUNT;
    uint LIVE_COUNTS[];
};
layout(std430, binding = 7) restrict writeonly buffer ParticleSimulationDispatchCommand
{
    uint DISPATCH_COMMAND[];
};

uniform uint ACTIVE_LIST_INDEX;

void main()
{
    uint activeCount = ACTIVE_LIST_INDEX == 0u ? ACTIVE_COUNT_A : ACTIVE_COUNT_B;
    if (ACTIVE_LIST_INDEX == 0u)
        ACTIVE_COUNT_B = 0u;
    else
        ACTIVE_COUNT_A = 0u;

    DISPATCH_COMMAND[0] = (activeCount + MPP_PARTICLE_WORK_GROUP_SIZE - 1u) / MPP_PARTICLE_WORK_GROUP_SIZE;
    DISPATCH_COMMAND[1] = 1u;
    DISPATCH_COMMAND[2] = 1u;
}
)MPP";

	// The one simulation kernel. Behaviour modules branch on the emitter mask at
	// runtime (ADR 0006); no module define permutations exist. Survivors append to
	// the opposite list while dead slots return directly to the GPU free stack.
	inline char const* ParticleSimulationComputeShader = R"MPP(#version 430

layout(local_size_x = MPP_PARTICLE_WORK_GROUP_SIZE) in;

const uint PARTICLE_MODULE_GRAVITY = 1u << 0u;
const uint PARTICLE_MODULE_DRAG = 1u << 1u;
const uint PARTICLE_MODULE_NOISE = 1u << 2u;
const uint PARTICLE_MODULE_COLLISION = 1u << 3u;
const uint PARTICLE_MODULE_CURL_NOISE = 1u << 4u;
const uint PARTICLE_MODULE_TURBULENCE = 1u << 5u;
const uint PARTICLE_MODULE_VECTOR_FIELD = 1u << 6u;
const uint COLLISION_SCREEN_SPACE = 1u << 0u;
const uint COLLISION_ANALYTICAL = 1u << 1u;
const uint COLLISION_SDF = 1u << 2u;
const uint RESPONSE_BOUNCE = 0u;
const uint RESPONSE_SLIDE = 1u;
const uint RESPONSE_STOP = 2u;
const uint RESPONSE_KILL = 3u;
const uint RESPONSE_SPAWN_SECONDARY = 4u;
const uint PARTICLE_FLAG_COLLIDING = 1u << 0u;
const uint PARTICLE_FLAG_COLLISION_EVENT = 1u << 1u;
const uint PARTICLE_FLAG_SPAWN_SECONDARY = 1u << 2u;

layout(std140, binding = 3) uniform CameraFrame
{
    mat4 VIEW_MATRIX;
    mat4 PROJECTION_MATRIX;
    mat4 INVERSE_PROJECTION_MATRIX;
    vec4 VIEWPORT_SIZE;
    vec4 NEAR_FAR_TIME;
};

struct ParticleRecord
{
    vec4 positionAge;
    vec4 velocityLifetime;
    uint packedColour;
    float baseSize;
    float rotation;
    float angularVelocity;
    uint emitterIndex;
    uint seed;
    uint flags;
    uint padding;
};

struct EmitterSimData
{
    mat4 transform;
    vec4 shapeParameters;
    vec4 initialVelocityMin;
    vec4 initialVelocityMax;
    vec4 colourMin;
    vec4 colourMax;
    vec4 lifetimeSizeRanges;
    vec4 rotationRanges;
    uvec4 shapeSeedModulesBudget;
    uvec4 emissionState;
    vec4 emissionRateAndPadding;
    vec4 parameterMultipliers0;
    vec4 parameterMultipliers1;
    vec4 gravityAndDrag;
    vec4 noiseFrequencyStrength;
    vec4 noiseScrollAndTimeScale;
    vec4 curlNoiseFrequencyStrength;
    vec4 curlNoiseScrollAndTimeScale;
    vec4 turbulenceFrequencyStrength;
    vec4 turbulenceScrollAndTimeScale;
    vec4 turbulenceOctavesLacunarityGain;
    vec4 vectorFieldFrequencyStrength;
    vec4 vectorFieldScrollAndTimeScale;
    uvec4 collisionConfiguration;
    vec4 collisionParameters;
    uvec4 eventRange;
};

layout(std430, binding = 0) restrict buffer ParticlePool
{
    ParticleRecord PARTICLES[];
};
layout(std430, binding = 1) restrict buffer ParticleFreeIndices
{
    uint FREE_INDICES[];
};
layout(std430, binding = 2) restrict buffer ParticleActiveIndicesA
{
    uint ACTIVE_INDICES_A[];
};
layout(std430, binding = 3) restrict buffer ParticleActiveIndicesB
{
    uint ACTIVE_INDICES_B[];
};
layout(std430, binding = 4) restrict buffer ParticleCounters
{
    uint FREE_COUNT;
    uint ACTIVE_COUNT_A;
    uint ACTIVE_COUNT_B;
    uint DROPPED_SPAWN_COUNT;
    uint SPAWNED_COUNT;
    uint KILLED_COUNT;
    uint RENDERED_COUNT;
    uint CULLED_COUNT;
    uint LIVE_COUNTS[];
};
layout(std430, binding = 5) restrict readonly buffer ParticleEmitters
{
    EmitterSimData EMITTERS[];
};
struct ParticleCollider
{
    uvec4 shapeAndPadding;
    vec4 first;
    vec4 second;
    vec4 third;
};
layout(std430, binding = 6) restrict readonly buffer ParticleColliders
{
    ParticleCollider COLLIDERS[];
};
struct EventRule { uvec4 configuration; uvec4 parameters; };
struct ParticleEventRecord { vec4 positionAge; vec4 velocityLifetime; vec4 normalAndPadding; uvec4 typeAndSource; uvec4 payloadAndSecondary; };
layout(std430, binding = 7) restrict buffer ParticleEvents
{
    uvec4 EVENT_COUNTS;
    EventRule EVENT_RULES[MPP_PARTICLE_MAX_EVENT_RULES];
    ParticleEventRecord EVENT_QUEUE_A[MPP_PARTICLE_MAX_GENERATED_EVENTS];
    ParticleEventRecord EVENT_QUEUE_B[MPP_PARTICLE_MAX_GENERATED_EVENTS];
    ParticleEventRecord EXTERNAL_EVENTS[MPP_PARTICLE_MAX_EXTERNAL_EVENTS];
};
layout(binding = 0) uniform sampler3D NOISE_TEXTURE;
layout(binding = 1) uniform sampler2D COLLISION_DEPTH_TEXTURE;
layout(binding = 2) uniform sampler3D SIGNED_DISTANCE_FIELD_TEXTURE;
layout(binding = 3) uniform sampler3D VECTOR_FIELD_TEXTURE;
uniform uint ACTIVE_LIST_INDEX;
uniform uint EMITTER_COUNT;
uniform uint TEMPLATE_COUNT;
uniform uint COLLIDER_COUNT;
uniform int HAS_COLLISION_DEPTH;
uniform int HAS_SIGNED_DISTANCE_FIELD;
uniform int HAS_VECTOR_FIELD;
uniform float DELTA_SECONDS;
uniform float SIMULATION_SECONDS;
uniform mat4 SDF_WORLD_TO_TEXTURE;
uniform vec4 SDF_PARAMETERS;

void killParticle(uint particleIndex, uint emitterIndex)
{
    uint freeDestination = atomicAdd(FREE_COUNT, 1u);
    FREE_INDICES[freeDestination] = particleIndex;
    if (emitterIndex < EMITTER_COUNT)
    {
        uint templateIndex = EMITTERS[emitterIndex].emissionState.w;
        if (templateIndex < TEMPLATE_COUNT)
            atomicAdd(LIVE_COUNTS[templateIndex], 0xffffffffu);
    }
}

void appendSurvivor(uint particleIndex)
{
    if (ACTIVE_LIST_INDEX == 0u)
    {
        uint destination = atomicAdd(ACTIVE_COUNT_B, 1u);
        ACTIVE_INDICES_B[destination] = particleIndex;
    }
    else
    {
        uint destination = atomicAdd(ACTIVE_COUNT_A, 1u);
        ACTIVE_INDICES_A[destination] = particleIndex;
    }
}

uint hashValue(uint value)
{
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    value ^= value >> 16u;
    return value;
}

void publishEvents(uint trigger, float previousAge, ParticleRecord particle, EmitterSimData emitter, vec3 eventNormal)
{
    for (uint ordinal = 0u; ordinal < emitter.eventRange.y; ++ordinal)
    {
        EventRule rule = EVENT_RULES[emitter.eventRange.x + ordinal];
        if (rule.configuration.x != trigger) continue;
        if (trigger == 3u)
        {
            float eventAge = uintBitsToFloat(rule.parameters.x);
            if (!(previousAge < eventAge && particle.positionAge.w >= eventAge)) continue;
        }
        uint destination = atomicAdd(EVENT_COUNTS.x, 1u);
        if (destination >= MPP_PARTICLE_MAX_GENERATED_EVENTS)
        {
            atomicAdd(EVENT_COUNTS.w, 1u);
            continue;
        }
        ParticleEventRecord event;
        event.positionAge = particle.positionAge;
        event.velocityLifetime = particle.velocityLifetime;
        event.normalAndPadding = vec4(eventNormal, uintBitsToFloat(rule.parameters.z));
        event.typeAndSource = uvec4(rule.configuration.xy, particle.emitterIndex, emitter.eventRange.z);
        event.payloadAndSecondary = uvec4(rule.parameters.y, rule.configuration.zw,
            hashValue(particle.seed ^ ordinal ^ trigger));
        EVENT_QUEUE_A[destination] = event;
    }
}

vec3 safeNormal(vec3 value, vec3 fallback)
{
    float magnitudeSquared = dot(value, value);
    return magnitudeSquared > 1.0e-12 ? value * inversesqrt(magnitudeSquared) : fallback;
}

float linearViewDepth(float depth)
{
    float nearPlane = NEAR_FAR_TIME.x;
    float farPlane = NEAR_FAR_TIME.y;
    float ndc = depth * 2.0 - 1.0;
    return (2.0 * nearPlane * farPlane) /
        max(farPlane + nearPlane - ndc * (farPlane - nearPlane), 1.0e-6);
}

vec3 reconstructWorldPosition(vec2 uv, float depth)
{
    vec4 view = INVERSE_PROJECTION_MATRIX * vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    view /= max(abs(view.w), 1.0e-6) * (view.w < 0.0 ? -1.0 : 1.0);
    return (inverse(VIEW_MATRIX) * vec4(view.xyz, 1.0)).xyz;
}

bool screenSpaceContact(vec3 position, vec3 previousPosition, float radius, float thickness,
    out vec3 normal, out float penetration)
{
    vec4 clip = PROJECTION_MATRIX * VIEW_MATRIX * vec4(position, 1.0);
    if (clip.w <= 0.0) return false;
    vec3 ndc = clip.xyz / clip.w;
    vec2 uv = ndc.xy * 0.5 + 0.5;
    if (any(lessThanEqual(uv, vec2(0.0))) || any(greaterThanEqual(uv, vec2(1.0)))) return false;
    float sceneDepth = textureLod(COLLISION_DEPTH_TEXTURE, uv, 0.0).r;
    if (sceneDepth >= 1.0) return false;

    float particleDepth = -(VIEW_MATRIX * vec4(position, 1.0)).z;
    float previousDepth = -(VIEW_MATRIX * vec4(previousPosition, 1.0)).z;
    float surfaceDepth = linearViewDepth(sceneDepth);
    float surfaceDistance = surfaceDepth - particleDepth;
    float travel = length(position - previousPosition);
    if (surfaceDistance > radius || surfaceDistance < -(max(0.0, thickness) + travel + radius) ||
        previousDepth > surfaceDepth + radius) return false;

    vec2 texel = 1.0 / vec2(textureSize(COLLISION_DEPTH_TEXTURE, 0));
    float rightDepth = textureLod(COLLISION_DEPTH_TEXTURE, clamp(uv + vec2(texel.x, 0.0), vec2(0.0), vec2(1.0)), 0.0).r;
    float upDepth = textureLod(COLLISION_DEPTH_TEXTURE, clamp(uv + vec2(0.0, texel.y), vec2(0.0), vec2(1.0)), 0.0).r;
    if (rightDepth >= 1.0) rightDepth = sceneDepth;
    if (upDepth >= 1.0) upDepth = sceneDepth;
    vec3 surface = reconstructWorldPosition(uv, sceneDepth);
    vec3 right = reconstructWorldPosition(uv + vec2(texel.x, 0.0), rightDepth);
    vec3 up = reconstructWorldPosition(uv + vec2(0.0, texel.y), upDepth);
    normal = safeNormal(cross(right - surface, up - surface),
        safeNormal((inverse(VIEW_MATRIX) * vec4(0.0, 0.0, 1.0, 0.0)).xyz, vec3(0.0, 1.0, 0.0)));
    if (dot(normal, position - previousPosition) > 0.0) normal = -normal;
    penetration = radius - surfaceDistance;
    return true;
}

vec3 rotateByQuaternion(vec4 quaternion, vec3 value)
{
    return value + 2.0 * cross(quaternion.xyz, cross(quaternion.xyz, value) + quaternion.w * value);
}

bool analyticalContact(ParticleCollider collider, vec3 position, float radius,
    out vec3 normal, out float penetration)
{
    uint shape = collider.shapeAndPadding.x;
    float distanceToSurface;
    if (shape == 0u) // plane: dot(normal, point) - distance = 0
    {
        normal = safeNormal(collider.first.xyz, vec3(0.0, 1.0, 0.0));
        distanceToSurface = dot(normal, position) - collider.first.w;
    }
    else if (shape == 1u) // sphere
    {
        vec3 offset = position - collider.first.xyz;
        normal = safeNormal(offset, vec3(0.0, 1.0, 0.0));
        distanceToSurface = length(offset) - max(0.0, collider.first.w);
    }
    else if (shape == 2u) // oriented box
    {
        vec4 orientation = dot(collider.third, collider.third) > 1.0e-12 ? normalize(collider.third) : vec4(0.0, 0.0, 0.0, 1.0);
        vec3 local = rotateByQuaternion(vec4(-orientation.xyz, orientation.w), position - collider.first.xyz);
        vec3 halfExtents = max(abs(collider.second.xyz), vec3(1.0e-6));
        vec3 q = abs(local) - halfExtents;
        vec3 outside = max(q, vec3(0.0));
        distanceToSurface = length(outside) + min(max(q.x, max(q.y, q.z)), 0.0);
        if (dot(outside, outside) > 1.0e-12)
            normal = safeNormal(sign(local) * outside, vec3(0.0, 1.0, 0.0));
        else if (q.x > q.y && q.x > q.z)
            normal = vec3(local.x < 0.0 ? -1.0 : 1.0, 0.0, 0.0);
        else if (q.y > q.z)
            normal = vec3(0.0, local.y < 0.0 ? -1.0 : 1.0, 0.0);
        else
            normal = vec3(0.0, 0.0, local.z < 0.0 ? -1.0 : 1.0);
        normal = rotateByQuaternion(orientation, normal);
    }
    else if (shape == 3u) // capsule
    {
        vec3 segment = collider.second.xyz - collider.first.xyz;
        float denominator = dot(segment, segment);
        float t = denominator > 1.0e-12 ? clamp(dot(position - collider.first.xyz, segment) / denominator, 0.0, 1.0) : 0.0;
        vec3 offset = position - (collider.first.xyz + segment * t);
        normal = safeNormal(offset, vec3(0.0, 1.0, 0.0));
        distanceToSurface = length(offset) - max(0.0, collider.first.w);
    }
    else return false;

    if (distanceToSurface > radius) return false;
    penetration = radius - distanceToSurface;
    return true;
}

bool signedDistanceFieldContact(vec3 position, float radius, out vec3 normal, out float penetration)
{
    vec3 uv = (SDF_WORLD_TO_TEXTURE * vec4(position, 1.0)).xyz;
    if (any(lessThan(uv, vec3(0.0))) || any(greaterThan(uv, vec3(1.0)))) return false;
    float scale = SDF_PARAMETERS.x;
    float isoValue = SDF_PARAMETERS.y;
    float distanceToSurface = (textureLod(SIGNED_DISTANCE_FIELD_TEXTURE, uv, 0.0).r - isoValue) * scale;
    if (distanceToSurface > radius) return false;

    vec3 texel = 1.0 / vec3(textureSize(SIGNED_DISTANCE_FIELD_TEXTURE, 0));
    vec3 gradient;
    gradient.x = textureLod(SIGNED_DISTANCE_FIELD_TEXTURE, clamp(uv + vec3(texel.x, 0.0, 0.0), vec3(0.0), vec3(1.0)), 0.0).r -
        textureLod(SIGNED_DISTANCE_FIELD_TEXTURE, clamp(uv - vec3(texel.x, 0.0, 0.0), vec3(0.0), vec3(1.0)), 0.0).r;
    gradient.y = textureLod(SIGNED_DISTANCE_FIELD_TEXTURE, clamp(uv + vec3(0.0, texel.y, 0.0), vec3(0.0), vec3(1.0)), 0.0).r -
        textureLod(SIGNED_DISTANCE_FIELD_TEXTURE, clamp(uv - vec3(0.0, texel.y, 0.0), vec3(0.0), vec3(1.0)), 0.0).r;
    gradient.z = textureLod(SIGNED_DISTANCE_FIELD_TEXTURE, clamp(uv + vec3(0.0, 0.0, texel.z), vec3(0.0), vec3(1.0)), 0.0).r -
        textureLod(SIGNED_DISTANCE_FIELD_TEXTURE, clamp(uv - vec3(0.0, 0.0, texel.z), vec3(0.0), vec3(1.0)), 0.0).r;
    normal = safeNormal(transpose(mat3(SDF_WORLD_TO_TEXTURE)) * gradient, vec3(0.0, 1.0, 0.0));
    penetration = radius - distanceToSurface;
    return true;
}

vec3 centredNoise(vec3 position)
{
    return textureLod(NOISE_TEXTURE, position, 0.0).xyz * 2.0 - 1.0;
}

vec3 sampleCurlNoise(vec3 position, vec3 frequency)
{
    vec3 stepSize = 1.0 / vec3(textureSize(NOISE_TEXTURE, 0));
    vec3 dx = (centredNoise(position + vec3(stepSize.x, 0.0, 0.0)) -
        centredNoise(position - vec3(stepSize.x, 0.0, 0.0))) * (0.5 / stepSize.x) * frequency.x;
    vec3 dy = (centredNoise(position + vec3(0.0, stepSize.y, 0.0)) -
        centredNoise(position - vec3(0.0, stepSize.y, 0.0))) * (0.5 / stepSize.y) * frequency.y;
    vec3 dz = (centredNoise(position + vec3(0.0, 0.0, stepSize.z)) -
        centredNoise(position - vec3(0.0, 0.0, stepSize.z))) * (0.5 / stepSize.z) * frequency.z;
    return vec3(dy.z - dz.y, dz.x - dx.z, dx.y - dy.x);
}

vec3 sampleTurbulence(vec3 position, int octaveCount, float lacunarity, float gain)
{
    vec3 total = vec3(0.0);
    float amplitude = 1.0;
    float amplitudeSum = 0.0;
    for (int octave = 0; octave < 8; ++octave)
    {
        if (octave >= octaveCount) break;
        // Absolute, zero-centred octave noise gives the characteristic folded
        // ridges of turbulence while retaining a vector force in all channels.
        total += (abs(centredNoise(position)) * 2.0 - 1.0) * amplitude;
        amplitudeSum += amplitude;
        position *= lacunarity;
        amplitude *= gain;
    }
    return amplitudeSum > 0.0 ? total / amplitudeSum : vec3(0.0);
}

bool applyCollisionResponse(inout ParticleRecord particle, EmitterSimData emitter,
    vec3 normal, float penetration, bool wasColliding)
{
    particle.flags |= PARTICLE_FLAG_COLLIDING;
    if (!wasColliding) particle.flags |= PARTICLE_FLAG_COLLISION_EVENT;
    particle.positionAge.xyz += normal * max(0.0, penetration + 1.0e-5);

    uint response = emitter.collisionConfiguration.y;
    float normalSpeed = dot(particle.velocityLifetime.xyz, normal);
    vec3 normalVelocity = normal * normalSpeed;
    vec3 tangentVelocity = particle.velocityLifetime.xyz - normalVelocity;
    float friction = clamp(emitter.collisionParameters.y, 0.0, 1.0);
    if (response == RESPONSE_BOUNCE)
    {
        if (normalSpeed < 0.0)
            particle.velocityLifetime.xyz = tangentVelocity * (1.0 - friction) -
                normalVelocity * max(0.0, emitter.collisionParameters.x);
    }
    else if (response == RESPONSE_SLIDE)
        particle.velocityLifetime.xyz = tangentVelocity * (1.0 - friction) + normal * max(0.0, normalSpeed);
    else if (response == RESPONSE_STOP)
        particle.velocityLifetime.xyz = vec3(0.0);
    else if (response == RESPONSE_KILL)
        return true;
    else if (response == RESPONSE_SPAWN_SECONDARY)
    {
        particle.velocityLifetime.xyz = vec3(0.0);
        if (!wasColliding) particle.flags |= PARTICLE_FLAG_SPAWN_SECONDARY;
    }
    return false;
}

void main()
{
    uint sourceCount = ACTIVE_LIST_INDEX == 0u ? ACTIVE_COUNT_A : ACTIVE_COUNT_B;
    uint activeOrdinal = gl_GlobalInvocationID.x;
    if (activeOrdinal >= sourceCount) return;

    uint particleIndex = ACTIVE_LIST_INDEX == 0u ? ACTIVE_INDICES_A[activeOrdinal] : ACTIVE_INDICES_B[activeOrdinal];
    ParticleRecord particle = PARTICLES[particleIndex];
    if (particle.emitterIndex >= EMITTER_COUNT)
    {
        killParticle(particleIndex, particle.emitterIndex);
        return;
    }

    EmitterSimData emitter = EMITTERS[particle.emitterIndex];
    float previousAge = particle.positionAge.w;
    particle.positionAge.w += DELTA_SECONDS;
    publishEvents(3u, previousAge, particle, emitter, vec3(0.0));
    if (particle.positionAge.w >= particle.velocityLifetime.w)
    {
        publishEvents(1u, previousAge, particle, emitter, vec3(0.0));
        killParticle(particleIndex, particle.emitterIndex);
        return;
    }

    uint modules = emitter.shapeSeedModulesBudget.z;
    bool wasColliding = (particle.flags & PARTICLE_FLAG_COLLIDING) != 0u;
    particle.flags &= ~(PARTICLE_FLAG_COLLIDING | PARTICLE_FLAG_COLLISION_EVENT | PARTICLE_FLAG_SPAWN_SECONDARY);
    vec3 previousPosition = particle.positionAge.xyz;
    vec3 velocity = particle.velocityLifetime.xyz;

    if ((modules & PARTICLE_MODULE_GRAVITY) != 0u)
        velocity += emitter.gravityAndDrag.xyz * DELTA_SECONDS;

    if ((modules & PARTICLE_MODULE_NOISE) != 0u)
    {
        vec3 samplePosition = particle.positionAge.xyz * emitter.noiseFrequencyStrength.xyz;
        samplePosition += emitter.noiseScrollAndTimeScale.xyz * (SIMULATION_SECONDS * emitter.noiseScrollAndTimeScale.w);
        velocity += centredNoise(samplePosition) * (emitter.noiseFrequencyStrength.w * DELTA_SECONDS);
    }

    if ((modules & PARTICLE_MODULE_CURL_NOISE) != 0u)
    {
        vec3 samplePosition = particle.positionAge.xyz * emitter.curlNoiseFrequencyStrength.xyz;
        samplePosition += emitter.curlNoiseScrollAndTimeScale.xyz *
            (SIMULATION_SECONDS * emitter.curlNoiseScrollAndTimeScale.w);
        velocity += sampleCurlNoise(samplePosition, emitter.curlNoiseFrequencyStrength.xyz) *
            (emitter.curlNoiseFrequencyStrength.w * DELTA_SECONDS);
    }

    if ((modules & PARTICLE_MODULE_TURBULENCE) != 0u)
    {
        vec3 samplePosition = particle.positionAge.xyz * emitter.turbulenceFrequencyStrength.xyz;
        samplePosition += emitter.turbulenceScrollAndTimeScale.xyz *
            (SIMULATION_SECONDS * emitter.turbulenceScrollAndTimeScale.w);
        int octaves = clamp(int(emitter.turbulenceOctavesLacunarityGain.x), 1, 8);
        float lacunarity = max(emitter.turbulenceOctavesLacunarityGain.y, 1.0);
        float gain = clamp(emitter.turbulenceOctavesLacunarityGain.z, 0.0, 1.0);
        velocity += sampleTurbulence(samplePosition, octaves, lacunarity, gain) *
            (emitter.turbulenceFrequencyStrength.w * DELTA_SECONDS);
    }

    if ((modules & PARTICLE_MODULE_VECTOR_FIELD) != 0u && HAS_VECTOR_FIELD != 0)
    {
        vec3 samplePosition = particle.positionAge.xyz * emitter.vectorFieldFrequencyStrength.xyz;
        samplePosition += emitter.vectorFieldScrollAndTimeScale.xyz *
            (SIMULATION_SECONDS * emitter.vectorFieldScrollAndTimeScale.w);
        vec3 field = textureLod(VECTOR_FIELD_TEXTURE, samplePosition, 0.0).xyz * 2.0 - 1.0;
        velocity += field * (emitter.vectorFieldFrequencyStrength.w * DELTA_SECONDS);
    }

    if ((modules & PARTICLE_MODULE_DRAG) != 0u)
        velocity *= max(0.0, 1.0 - max(0.0, emitter.gravityAndDrag.w) * DELTA_SECONDS);

    particle.velocityLifetime.xyz = velocity;
    particle.positionAge.xyz += velocity * DELTA_SECONDS;
    particle.rotation += particle.angularVelocity * DELTA_SECONDS;

    if ((modules & PARTICLE_MODULE_COLLISION) != 0u)
    {
        uint sources = emitter.collisionConfiguration.x;
        float radius = abs(particle.baseSize) * max(0.0, emitter.collisionParameters.z);
        vec3 normal;
        vec3 collisionEventNormal = vec3(0.0);
        float penetration;
        bool killed = false;
        bool recordedCollisionEvent = false;
        // Collision sources run in the staged order from spec section 32.
        if ((sources & COLLISION_SCREEN_SPACE) != 0u && HAS_COLLISION_DEPTH != 0 &&
            screenSpaceContact(particle.positionAge.xyz, previousPosition, radius,
                emitter.collisionParameters.w, normal, penetration))
        {
            if (!wasColliding)
            {
                collisionEventNormal = normal;
                recordedCollisionEvent = true;
            }
            killed = applyCollisionResponse(particle, emitter, normal, penetration, wasColliding);
        }

        if (!killed && (sources & COLLISION_ANALYTICAL) != 0u)
        {
            for (uint colliderIndex = 0u; colliderIndex < COLLIDER_COUNT; ++colliderIndex)
            {
                if (!analyticalContact(COLLIDERS[colliderIndex], particle.positionAge.xyz, radius, normal, penetration))
                    continue;
                if (!wasColliding && !recordedCollisionEvent)
                {
                    collisionEventNormal = normal;
                    recordedCollisionEvent = true;
                }
                if (applyCollisionResponse(particle, emitter, normal, penetration, wasColliding))
                {
                    killed = true;
                    break;
                }
            }
        }

        if (!killed && (sources & COLLISION_SDF) != 0u && HAS_SIGNED_DISTANCE_FIELD != 0 &&
            signedDistanceFieldContact(particle.positionAge.xyz, radius, normal, penetration))
        {
            if (!wasColliding && !recordedCollisionEvent)
                collisionEventNormal = normal;
            killed = applyCollisionResponse(particle, emitter, normal, penetration, wasColliding);
        }

        if ((particle.flags & PARTICLE_FLAG_COLLISION_EVENT) != 0u)
            publishEvents(2u, previousAge, particle, emitter, collisionEventNormal);
        if (killed)
        {
            publishEvents(1u, previousAge, particle, emitter, vec3(0.0));
            killParticle(particleIndex, particle.emitterIndex);
            return;
        }
    }

    PARTICLES[particleIndex] = particle;
    appendSurvivor(particleIndex);
}
)MPP";

	// Consumes one GPU event per work group. Secondary particle bursts allocate,
	// initialise, and append particles directly to the current active list. Spawn
	// rules on those particles feed the alternate queue for a bounded GPU cascade;
	// typed external actions are copied to a separate GPU-resident output range.
	inline char const* ParticleEventProcessComputeShader = R"MPP(#version 430

layout(local_size_x = MPP_PARTICLE_WORK_GROUP_SIZE) in;

struct ParticleRecord
{
    vec4 positionAge;
    vec4 velocityLifetime;
    uint packedColour;
    float baseSize;
    float rotation;
    float angularVelocity;
    uint emitterIndex;
    uint seed;
    uint flags;
    uint padding;
};
struct EmitterSimData
{
    mat4 transform;
    vec4 shapeParameters;
    vec4 initialVelocityMin;
    vec4 initialVelocityMax;
    vec4 colourMin;
    vec4 colourMax;
    vec4 lifetimeSizeRanges;
    vec4 rotationRanges;
    uvec4 shapeSeedModulesBudget;
    uvec4 emissionState;
    vec4 emissionRateAndPadding;
    vec4 parameterMultipliers0;
    vec4 parameterMultipliers1;
    vec4 gravityAndDrag;
    vec4 noiseFrequencyStrength;
    vec4 noiseScrollAndTimeScale;
    vec4 curlNoiseFrequencyStrength;
    vec4 curlNoiseScrollAndTimeScale;
    vec4 turbulenceFrequencyStrength;
    vec4 turbulenceScrollAndTimeScale;
    vec4 turbulenceOctavesLacunarityGain;
    vec4 vectorFieldFrequencyStrength;
    vec4 vectorFieldScrollAndTimeScale;
    uvec4 collisionConfiguration;
    vec4 collisionParameters;
    uvec4 eventRange;
};
struct EventRule { uvec4 configuration; uvec4 parameters; };
struct ParticleEventRecord { vec4 positionAge; vec4 velocityLifetime; vec4 normalAndPadding; uvec4 typeAndSource; uvec4 payloadAndSecondary; };

layout(std430, binding = 0) restrict writeonly buffer ParticlePool { ParticleRecord PARTICLES[]; };
layout(std430, binding = 1) restrict buffer ParticleFreeIndices { uint FREE_INDICES[]; };
layout(std430, binding = 2) restrict writeonly buffer ParticleActiveIndicesA { uint ACTIVE_INDICES_A[]; };
layout(std430, binding = 3) restrict writeonly buffer ParticleActiveIndicesB { uint ACTIVE_INDICES_B[]; };
layout(std430, binding = 4) restrict buffer ParticleCounters
{
    uint FREE_COUNT;
    uint ACTIVE_COUNT_A;
    uint ACTIVE_COUNT_B;
    uint DROPPED_SPAWN_COUNT;
    uint SPAWNED_COUNT;
    uint KILLED_COUNT;
    uint RENDERED_COUNT;
    uint CULLED_COUNT;
    uint LIVE_COUNTS[];
};
layout(std430, binding = 5) restrict readonly buffer ParticleEmitters { EmitterSimData EMITTERS[]; };
layout(std430, binding = 6) restrict buffer ParticleEvents
{
    uvec4 EVENT_COUNTS;
    EventRule EVENT_RULES[MPP_PARTICLE_MAX_EVENT_RULES];
    ParticleEventRecord EVENT_QUEUE_A[MPP_PARTICLE_MAX_GENERATED_EVENTS];
    ParticleEventRecord EVENT_QUEUE_B[MPP_PARTICLE_MAX_GENERATED_EVENTS];
    ParticleEventRecord EXTERNAL_EVENTS[MPP_PARTICLE_MAX_EXTERNAL_EVENTS];
};

uniform uint ACTIVE_LIST_INDEX;
uniform uint EMITTER_COUNT;
uniform uint TEMPLATE_COUNT;
uniform uint SOURCE_QUEUE;

uint hashValue(uint value)
{
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    value ^= value >> 16u;
    return value;
}
float randomScalar(inout uint state)
{
    state = hashValue(state + 0x9e3779b9u);
    return float(state) * (1.0 / 4294967296.0);
}
vec3 randomUnitVector(inout uint state)
{
    float z = randomScalar(state) * 2.0 - 1.0;
    float angle = randomScalar(state) * 6.28318530718;
    float radius = sqrt(max(0.0, 1.0 - z * z));
    return vec3(cos(angle) * radius, z, sin(angle) * radius);
}
vec3 sampleShape(uint shape, vec4 parameters, inout uint state)
{
    if (shape == 0u) return vec3(0.0);
    if (shape == 1u) return parameters.xyz * (randomScalar(state) * 2.0 - 1.0);
    if (shape == 2u) return parameters.xyz * vec3(randomScalar(state) * 2.0 - 1.0,
        randomScalar(state) * 2.0 - 1.0, randomScalar(state) * 2.0 - 1.0);
    if (shape == 3u) return randomUnitVector(state) * (parameters.x * pow(randomScalar(state), 1.0 / 3.0));
    if (shape == 4u)
    {
        vec3 direction = randomUnitVector(state);
        direction.y = abs(direction.y);
        return direction * (parameters.x * pow(randomScalar(state), 1.0 / 3.0));
    }
    if (shape == 5u)
    {
        float angle = randomScalar(state) * 6.28318530718;
        float radius = parameters.x * sqrt(randomScalar(state));
        return vec3(cos(angle) * radius, 0.0, sin(angle) * radius);
    }
    float heightFraction = pow(randomScalar(state), 1.0 / 3.0);
    float angle = randomScalar(state) * 6.28318530718;
    float radius = parameters.x * heightFraction * sqrt(randomScalar(state));
    return vec3(cos(angle) * radius, parameters.y * heightFraction, sin(angle) * radius);
}
bool reserveTemplateParticle(uint templateIndex, uint budget)
{
    uint observed = LIVE_COUNTS[templateIndex];
    while (observed < budget)
    {
        uint previous = atomicCompSwap(LIVE_COUNTS[templateIndex], observed, observed + 1u);
        if (previous == observed) return true;
        observed = previous;
    }
    return false;
}
bool popFreeIndex(out uint particleIndex)
{
    uint observed = FREE_COUNT;
    while (observed > 0u)
    {
        uint previous = atomicCompSwap(FREE_COUNT, observed, observed - 1u);
        if (previous == observed)
        {
            particleIndex = FREE_INDICES[observed - 1u];
            return true;
        }
        observed = previous;
    }
    return false;
}
void appendActiveIndex(uint particleIndex)
{
    if (ACTIVE_LIST_INDEX == 0u)
    {
        uint destination = atomicAdd(ACTIVE_COUNT_A, 1u);
        ACTIVE_INDICES_A[destination] = particleIndex;
    }
    else
    {
        uint destination = atomicAdd(ACTIVE_COUNT_B, 1u);
        ACTIVE_INDICES_B[destination] = particleIndex;
    }
}
void appendCascadeEvent(ParticleEventRecord event)
{
    uint destination = SOURCE_QUEUE == 0u ? atomicAdd(EVENT_COUNTS.y, 1u) : atomicAdd(EVENT_COUNTS.x, 1u);
    if (destination >= MPP_PARTICLE_MAX_GENERATED_EVENTS)
    {
        atomicAdd(EVENT_COUNTS.w, 1u);
        return;
    }
    if (SOURCE_QUEUE == 0u) EVENT_QUEUE_B[destination] = event;
    else EVENT_QUEUE_A[destination] = event;
}
void publishSpawnEvents(ParticleRecord particle, EmitterSimData emitter)
{
    for (uint ordinal = 0u; ordinal < emitter.eventRange.y; ++ordinal)
    {
        EventRule rule = EVENT_RULES[emitter.eventRange.x + ordinal];
        if (rule.configuration.x != 0u &&
            !(rule.configuration.x == 3u && uintBitsToFloat(rule.parameters.x) <= 0.0)) continue;
        ParticleEventRecord event;
        event.positionAge = particle.positionAge;
        event.velocityLifetime = particle.velocityLifetime;
        event.normalAndPadding = vec4(0.0, 0.0, 0.0, uintBitsToFloat(rule.parameters.z));
        event.typeAndSource = uvec4(rule.configuration.xy, particle.emitterIndex, emitter.eventRange.z);
        event.payloadAndSecondary = uvec4(rule.parameters.y, rule.configuration.zw,
            hashValue(particle.seed ^ ordinal));
        appendCascadeEvent(event);
    }
}
void appendExternal(ParticleEventRecord event)
{
    uint destination = atomicAdd(EVENT_COUNTS.z, 1u);
    if (destination < MPP_PARTICLE_MAX_EXTERNAL_EVENTS) EXTERNAL_EVENTS[destination] = event;
    else atomicAdd(EVENT_COUNTS.w, 1u);
}

void main()
{
    uint eventIndex = gl_WorkGroupID.x;
    ParticleEventRecord event = SOURCE_QUEUE == 0u ? EVENT_QUEUE_A[eventIndex] : EVENT_QUEUE_B[eventIndex];
    if (event.typeAndSource.y != 0u)
    {
        if (gl_LocalInvocationID.x == 0u) appendExternal(event);
        return;
    }

    uint targetEmitterIndex = event.payloadAndSecondary.y;
    if (targetEmitterIndex >= EMITTER_COUNT)
    {
        if (gl_LocalInvocationID.x == 0u) atomicAdd(EVENT_COUNTS.w, event.payloadAndSecondary.z);
        return;
    }
    EmitterSimData emitter = EMITTERS[targetEmitterIndex];
    uint templateIndex = emitter.emissionState.w;
    uint targetGeneration = floatBitsToUint(event.normalAndPadding.w);
    if (templateIndex >= TEMPLATE_COUNT || emitter.eventRange.w == 0u || emitter.eventRange.z != targetGeneration)
    {
        if (gl_LocalInvocationID.x == 0u) atomicAdd(EVENT_COUNTS.w, event.payloadAndSecondary.z);
        return;
    }

    for (uint ordinal = gl_LocalInvocationID.x; ordinal < event.payloadAndSecondary.z; ordinal += gl_WorkGroupSize.x)
    {
        if (!reserveTemplateParticle(templateIndex, emitter.shapeSeedModulesBudget.w))
        {
            atomicAdd(DROPPED_SPAWN_COUNT, 1u);
            continue;
        }
        uint particleIndex;
        if (!popFreeIndex(particleIndex))
        {
            atomicAdd(LIVE_COUNTS[templateIndex], 0xffffffffu);
            atomicAdd(DROPPED_SPAWN_COUNT, 1u);
            continue;
        }

        uint seed = hashValue(emitter.shapeSeedModulesBudget.y ^ event.payloadAndSecondary.w ^ ordinal ^ particleIndex);
        uint randomState = seed;
        vec3 localPosition = sampleShape(emitter.shapeSeedModulesBudget.x, emitter.shapeParameters, randomState);
        vec3 velocityMix = vec3(randomScalar(randomState), randomScalar(randomState), randomScalar(randomState));
        vec3 localVelocity = mix(emitter.initialVelocityMin.xyz, emitter.initialVelocityMax.xyz, velocityMix) * emitter.parameterMultipliers0.z;
        float lifetime = mix(emitter.lifetimeSizeRanges.x, emitter.lifetimeSizeRanges.y, randomScalar(randomState)) * emitter.parameterMultipliers0.w;
        float size = mix(emitter.lifetimeSizeRanges.z, emitter.lifetimeSizeRanges.w, randomScalar(randomState)) * emitter.parameterMultipliers0.y;
        float rotation = mix(emitter.rotationRanges.x, emitter.rotationRanges.y, randomScalar(randomState));
        float angularVelocity = mix(emitter.rotationRanges.z, emitter.rotationRanges.w, randomScalar(randomState));
        vec4 colour = mix(emitter.colourMin, emitter.colourMax, vec4(randomScalar(randomState),
            randomScalar(randomState), randomScalar(randomState), randomScalar(randomState)));

        ParticleRecord particle;
        particle.positionAge = vec4(event.positionAge.xyz + mat3(emitter.transform) * localPosition, 0.0);
        particle.velocityLifetime = vec4(mat3(emitter.transform) * localVelocity, lifetime);
        particle.packedColour = packUnorm4x8(clamp(colour, 0.0, 1.0));
        particle.baseSize = size;
        particle.rotation = rotation;
        particle.angularVelocity = angularVelocity;
        particle.emitterIndex = targetEmitterIndex;
        particle.seed = seed;
        particle.flags = 0u;
        particle.padding = 0u;
        PARTICLES[particleIndex] = particle;
        appendActiveIndex(particleIndex);
        publishSpawnEvents(particle, emitter);
    }
}
)MPP";

	// Clears the per-template counts and scatter cursors, and turns the current
	// GPU-owned active count into the exact indirect dispatch used by both count
	// and scatter. No count crosses the CPU boundary.
	inline char const* ParticleCompactionPrepareComputeShader = R"MPP(#version 430

layout(local_size_x = MPP_PARTICLE_WORK_GROUP_SIZE) in;

layout(std430, binding = 4) restrict buffer ParticleCounters
{
    uint FREE_COUNT;
    uint ACTIVE_COUNT_A;
    uint ACTIVE_COUNT_B;
    uint DROPPED_SPAWN_COUNT;
    uint SPAWNED_COUNT;
    uint KILLED_COUNT;
    uint RENDERED_COUNT;
    uint CULLED_COUNT;
    uint TEMPLATE_COUNTS[];
};
layout(std430, binding = 6) restrict buffer ParticleCompactionScratch
{
    uint COMPACTION_VALUES[];
};
layout(std430, binding = 7) restrict writeonly buffer ParticleCompactionDispatchCommand
{
    uint DISPATCH_COMMAND[];
};

uniform uint ACTIVE_LIST_INDEX;
uniform uint TEMPLATE_COUNT;
uniform uint TEMPLATE_CAPACITY;

void main()
{
    uint templateIndex = gl_GlobalInvocationID.x;
    if (templateIndex < TEMPLATE_COUNT)
    {
        TEMPLATE_COUNTS[templateIndex] = 0u;
        COMPACTION_VALUES[templateIndex] = 0u;
        COMPACTION_VALUES[TEMPLATE_CAPACITY + templateIndex] = 0u;
        COMPACTION_VALUES[TEMPLATE_CAPACITY * 2u + templateIndex] = 0u;
    }

    if (templateIndex == 0u)
    {
        uint activeCount = ACTIVE_LIST_INDEX == 0u ? ACTIVE_COUNT_A : ACTIVE_COUNT_B;
        DISPATCH_COMMAND[0] = (activeCount + MPP_PARTICLE_WORK_GROUP_SIZE - 1u) / MPP_PARTICLE_WORK_GROUP_SIZE;
        DISPATCH_COMMAND[1] = 1u;
        DISPATCH_COMMAND[2] = 1u;
    }
}
)MPP";

	// Counts every survivor for template budgets, and visible survivors for draw
	// compaction. Visibility remains GPU-only and is evaluated again by scatter;
	// no separate culling pass or CPU result is introduced.
	inline char const* ParticleCompactionCountComputeShader = R"MPP(#version 430

layout(local_size_x = MPP_PARTICLE_WORK_GROUP_SIZE) in;
layout(std140, binding = 3) uniform CameraFrame
{
    mat4 VIEW_MATRIX;
    mat4 PROJECTION_MATRIX;
    mat4 INVERSE_PROJECTION_MATRIX;
    vec4 VIEWPORT_SIZE;
    vec4 NEAR_FAR_TIME;
};

struct ParticleRecord { vec4 positionAge; vec4 velocityLifetime; uint packedColour; float baseSize; float rotation; float angularVelocity; uint emitterIndex; uint seed; uint flags; uint padding; };
struct EmitterSimData { mat4 transform; vec4 shapeParameters; vec4 initialVelocityMin; vec4 initialVelocityMax; vec4 colourMin; vec4 colourMax; vec4 lifetimeSizeRanges; vec4 rotationRanges; uvec4 shapeSeedModulesBudget; uvec4 emissionState; vec4 emissionRateAndPadding; vec4 parameterMultipliers0; vec4 parameterMultipliers1; vec4 gravityAndDrag; vec4 noiseFrequencyStrength; vec4 noiseScrollAndTimeScale; vec4 curlNoiseFrequencyStrength; vec4 curlNoiseScrollAndTimeScale; vec4 turbulenceFrequencyStrength; vec4 turbulenceScrollAndTimeScale; vec4 turbulenceOctavesLacunarityGain; vec4 vectorFieldFrequencyStrength; vec4 vectorFieldScrollAndTimeScale; uvec4 collisionConfiguration; vec4 collisionParameters; uvec4 eventRange; };
struct TemplateRenderData { uvec4 textureAndAtlas; vec4 tintAndAlpha; vec4 appearance; uvec4 modes; vec4 culling; uvec4 sorting; };

layout(std430, binding = 0) restrict readonly buffer ParticlePool { ParticleRecord PARTICLES[]; };
layout(std430, binding = 2) restrict readonly buffer ParticleActiveIndicesA { uint ACTIVE_INDICES_A[]; };
layout(std430, binding = 3) restrict readonly buffer ParticleActiveIndicesB { uint ACTIVE_INDICES_B[]; };
layout(std430, binding = 4) restrict buffer ParticleCounters { uint FREE_COUNT; uint ACTIVE_COUNT_A; uint ACTIVE_COUNT_B; uint DROPPED_SPAWN_COUNT; uint SPAWNED_COUNT; uint KILLED_COUNT; uint RENDERED_COUNT; uint CULLED_COUNT; uint TEMPLATE_COUNTS[]; };
layout(std430, binding = 5) restrict readonly buffer ParticleEmitters { EmitterSimData EMITTERS[]; };
layout(std430, binding = 6) restrict buffer ParticleCompactionScratch { uint COMPACTION_VALUES[]; };
layout(std430, binding = 7) restrict readonly buffer ParticleTemplates { TemplateRenderData TEMPLATES[]; };

uniform uint ACTIVE_LIST_INDEX;
uniform uint EMITTER_COUNT;
uniform uint TEMPLATE_COUNT;
uniform uint TEMPLATE_CAPACITY;

bool outsidePlane(vec4 plane, vec3 centre, float radius)
{
    return dot(plane, vec4(centre, 1.0)) < -radius * length(plane.xyz);
}

bool particleVisible(ParticleRecord particle, EmitterSimData emitter, TemplateRenderData appearance)
{
    const uint EFFECT_VISIBLE = 1u << 0u;
    if ((uint(emitter.emissionRateAndPadding.y) & EFFECT_VISIBLE) == 0u) return false;

    vec3 viewPosition = (VIEW_MATRIX * vec4(particle.positionAge.xyz, 1.0)).xyz;
    float radius = abs(particle.baseSize) * max(appearance.culling.z, 1.0);
    if (appearance.sorting.y == 0u && appearance.modes.z == 5u) radius *= 1.0 + length(particle.velocityLifetime.xyz);
    vec4 row0 = vec4(PROJECTION_MATRIX[0][0], PROJECTION_MATRIX[1][0], PROJECTION_MATRIX[2][0], PROJECTION_MATRIX[3][0]);
    vec4 row1 = vec4(PROJECTION_MATRIX[0][1], PROJECTION_MATRIX[1][1], PROJECTION_MATRIX[2][1], PROJECTION_MATRIX[3][1]);
    vec4 row2 = vec4(PROJECTION_MATRIX[0][2], PROJECTION_MATRIX[1][2], PROJECTION_MATRIX[2][2], PROJECTION_MATRIX[3][2]);
    vec4 row3 = vec4(PROJECTION_MATRIX[0][3], PROJECTION_MATRIX[1][3], PROJECTION_MATRIX[2][3], PROJECTION_MATRIX[3][3]);
    if (outsidePlane(row3 + row0, viewPosition, radius) || outsidePlane(row3 - row0, viewPosition, radius) ||
        outsidePlane(row3 + row1, viewPosition, radius) || outsidePlane(row3 - row1, viewPosition, radius) ||
        outsidePlane(row3 + row2, viewPosition, radius) || outsidePlane(row3 - row2, viewPosition, radius)) return false;

    if (appearance.culling.x > 0.0 && length(viewPosition) - radius > appearance.culling.x) return false;
    if (appearance.culling.y > 0.0)
    {
        vec4 clip = PROJECTION_MATRIX * vec4(viewPosition, 1.0);
        float divisor = abs(PROJECTION_MATRIX[2][3]) > 0.5 ? max(abs(clip.w), 1.0e-6) : 1.0;
        vec2 radiusPixels = radius * vec2(abs(PROJECTION_MATRIX[0][0]), abs(PROJECTION_MATRIX[1][1])) *
            VIEWPORT_SIZE.xy * 0.5 / divisor;
        if (2.0 * max(radiusPixels.x, radiusPixels.y) < appearance.culling.y) return false;
    }
    return true;
}

void main()
{
    uint activeCount = ACTIVE_LIST_INDEX == 0u ? ACTIVE_COUNT_A : ACTIVE_COUNT_B;
    uint activeOrdinal = gl_GlobalInvocationID.x;
    if (activeOrdinal >= activeCount) return;
    uint particleIndex = ACTIVE_LIST_INDEX == 0u ? ACTIVE_INDICES_A[activeOrdinal] : ACTIVE_INDICES_B[activeOrdinal];
    ParticleRecord particle = PARTICLES[particleIndex];
    if (particle.emitterIndex >= EMITTER_COUNT) return;
    EmitterSimData emitter = EMITTERS[particle.emitterIndex];
    uint templateIndex = emitter.emissionState.w;
    if (templateIndex >= TEMPLATE_COUNT) return;
    atomicAdd(TEMPLATE_COUNTS[templateIndex], 1u);
    if (particleVisible(particle, emitter, TEMPLATES[templateIndex]))
        atomicAdd(COMPACTION_VALUES[TEMPLATE_CAPACITY * 2u + templateIndex], 1u);
}
)MPP";

	// A single invocation is intentional: template tables are capped at 4096,
	// making this constant-sized scan small while avoiding a multi-level scan and
	// still keeping every value GPU-resident. It also authors every indirect draw
	// command, including empty templates.
	inline char const* ParticleCompactionPrefixComputeShader = R"MPP(#version 430

layout(local_size_x = 1) in;

layout(std430, binding = 4) restrict buffer ParticleCounters
{
    uint FREE_COUNT;
    uint ACTIVE_COUNT_A;
    uint ACTIVE_COUNT_B;
    uint DROPPED_SPAWN_COUNT;
    uint SPAWNED_COUNT;
    uint KILLED_COUNT;
    uint RENDERED_COUNT;
    uint CULLED_COUNT;
    uint TEMPLATE_COUNTS[];
};
layout(std430, binding = 6) restrict buffer ParticleCompactionScratch
{
    uint COMPACTION_VALUES[];
};
layout(std430, binding = 5) restrict readonly buffer ParticleTemplates
{
    uvec4 TEXTURE_AND_ATLAS[];
};
layout(std430, binding = 7) restrict writeonly buffer ParticleIndirectCommands
{
    uint INDIRECT_COMMANDS[];
};

uniform uint TEMPLATE_COUNT;
uniform uint TEMPLATE_CAPACITY;

void main()
{
    uint activeOffset = 0u;
    uint visibleOffset = 0u;
    for (uint templateIndex = 0u; templateIndex < TEMPLATE_COUNT; ++templateIndex)
    {
        uint activeCount = TEMPLATE_COUNTS[templateIndex];
        uint visibleCount = COMPACTION_VALUES[TEMPLATE_CAPACITY * 2u + templateIndex];
        COMPACTION_VALUES[templateIndex] = visibleOffset;
        uint commandOffset = templateIndex * 4u;
        INDIRECT_COMMANDS[commandOffset] = 4u;
        // sorting.y is the render mode in the sixth vec4 of each template.
        // Real meshes consume the range through their dedicated command buffer.
        uint renderMode = TEXTURE_AND_ATLAS[templateIndex * 6u + 5u].y;
        INDIRECT_COMMANDS[commandOffset + 1u] = renderMode == 0u ? visibleCount : 0u;
        INDIRECT_COMMANDS[commandOffset + 2u] = visibleOffset * 4u;
        INDIRECT_COMMANDS[commandOffset + 3u] = templateIndex;
        activeOffset += activeCount;
        visibleOffset += visibleCount;
    }
    // Statistics prepare stores the requested spawn count and starting active
    // count in these fields. Visibility never affects lifetime or spawn budgets.
    uint spawned = SPAWNED_COUNT >= DROPPED_SPAWN_COUNT ? SPAWNED_COUNT - DROPPED_SPAWN_COUNT : 0u;
    uint startAndSpawned = KILLED_COUNT + spawned;
    SPAWNED_COUNT = spawned;
    KILLED_COUNT = startAndSpawned >= activeOffset ? startAndSpawned - activeOffset : 0u;
    RENDERED_COUNT = visibleOffset;
    CULLED_COUNT = activeOffset >= visibleOffset ? activeOffset - visibleOffset : 0u;
}
)MPP";

	// Scatters each survivor into its template's prefix-summed range. Atomic
	// cursors make order within a range unspecified, but ranges themselves are
	// exact and non-overlapping.
	inline char const* ParticleCompactionScatterComputeShader = R"MPP(#version 430

layout(local_size_x = MPP_PARTICLE_WORK_GROUP_SIZE) in;
layout(std140, binding = 3) uniform CameraFrame
{
    mat4 VIEW_MATRIX;
    mat4 PROJECTION_MATRIX;
    mat4 INVERSE_PROJECTION_MATRIX;
    vec4 VIEWPORT_SIZE;
    vec4 NEAR_FAR_TIME;
};

struct ParticleRecord
{
    vec4 positionAge;
    vec4 velocityLifetime;
    uint packedColour;
    float baseSize;
    float rotation;
    float angularVelocity;
    uint emitterIndex;
    uint seed;
    uint flags;
    uint padding;
};

struct EmitterSimData
{
    mat4 transform;
    vec4 shapeParameters;
    vec4 initialVelocityMin;
    vec4 initialVelocityMax;
    vec4 colourMin;
    vec4 colourMax;
    vec4 lifetimeSizeRanges;
    vec4 rotationRanges;
    uvec4 shapeSeedModulesBudget;
    uvec4 emissionState;
    vec4 emissionRateAndPadding;
    vec4 parameterMultipliers0;
    vec4 parameterMultipliers1;
    vec4 gravityAndDrag;
    vec4 noiseFrequencyStrength;
    vec4 noiseScrollAndTimeScale;
    vec4 curlNoiseFrequencyStrength;
    vec4 curlNoiseScrollAndTimeScale;
    vec4 turbulenceFrequencyStrength;
    vec4 turbulenceScrollAndTimeScale;
    vec4 turbulenceOctavesLacunarityGain;
    vec4 vectorFieldFrequencyStrength;
    vec4 vectorFieldScrollAndTimeScale;
    uvec4 collisionConfiguration;
    vec4 collisionParameters;
    uvec4 eventRange;
};

struct TemplateRenderData
{
    uvec4 textureAndAtlas;
    vec4 tintAndAlpha;
    vec4 appearance;
    uvec4 modes;
    vec4 culling;
    uvec4 sorting;
};

layout(std430, binding = 0) restrict readonly buffer ParticlePool
{
    ParticleRecord PARTICLES[];
};
layout(std430, binding = 1) restrict writeonly buffer ParticleRenderIndices
{
    uint RENDER_INDICES[];
};
layout(std430, binding = 2) restrict readonly buffer ParticleActiveIndicesA
{
    uint ACTIVE_INDICES_A[];
};
layout(std430, binding = 3) restrict readonly buffer ParticleActiveIndicesB
{
    uint ACTIVE_INDICES_B[];
};
layout(std430, binding = 4) restrict readonly buffer ParticleCounters
{
    uint FREE_COUNT;
    uint ACTIVE_COUNT_A;
    uint ACTIVE_COUNT_B;
    uint DROPPED_SPAWN_COUNT;
    uint SPAWNED_COUNT;
    uint KILLED_COUNT;
    uint RENDERED_COUNT;
    uint CULLED_COUNT;
    uint TEMPLATE_COUNTS[];
};
layout(std430, binding = 5) restrict readonly buffer ParticleEmitters
{
    EmitterSimData EMITTERS[];
};
layout(std430, binding = 6) restrict buffer ParticleCompactionScratch
{
    uint COMPACTION_VALUES[];
};
layout(std430, binding = 7) restrict readonly buffer ParticleTemplates
{
    TemplateRenderData TEMPLATES[];
};

uniform uint ACTIVE_LIST_INDEX;
uniform uint EMITTER_COUNT;
uniform uint TEMPLATE_COUNT;
uniform uint TEMPLATE_CAPACITY;

bool outsidePlane(vec4 plane, vec3 centre, float radius)
{
    return dot(plane, vec4(centre, 1.0)) < -radius * length(plane.xyz);
}

bool particleVisible(ParticleRecord particle, EmitterSimData emitter, TemplateRenderData appearance)
{
    const uint EFFECT_VISIBLE = 1u << 0u;
    if ((uint(emitter.emissionRateAndPadding.y) & EFFECT_VISIBLE) == 0u) return false;
    vec3 viewPosition = (VIEW_MATRIX * vec4(particle.positionAge.xyz, 1.0)).xyz;
    float radius = abs(particle.baseSize) * max(appearance.culling.z, 1.0);
    if (appearance.sorting.y == 0u && appearance.modes.z == 5u) radius *= 1.0 + length(particle.velocityLifetime.xyz);
    vec4 row0 = vec4(PROJECTION_MATRIX[0][0], PROJECTION_MATRIX[1][0], PROJECTION_MATRIX[2][0], PROJECTION_MATRIX[3][0]);
    vec4 row1 = vec4(PROJECTION_MATRIX[0][1], PROJECTION_MATRIX[1][1], PROJECTION_MATRIX[2][1], PROJECTION_MATRIX[3][1]);
    vec4 row2 = vec4(PROJECTION_MATRIX[0][2], PROJECTION_MATRIX[1][2], PROJECTION_MATRIX[2][2], PROJECTION_MATRIX[3][2]);
    vec4 row3 = vec4(PROJECTION_MATRIX[0][3], PROJECTION_MATRIX[1][3], PROJECTION_MATRIX[2][3], PROJECTION_MATRIX[3][3]);
    if (outsidePlane(row3 + row0, viewPosition, radius) || outsidePlane(row3 - row0, viewPosition, radius) ||
        outsidePlane(row3 + row1, viewPosition, radius) || outsidePlane(row3 - row1, viewPosition, radius) ||
        outsidePlane(row3 + row2, viewPosition, radius) || outsidePlane(row3 - row2, viewPosition, radius)) return false;
    if (appearance.culling.x > 0.0 && length(viewPosition) - radius > appearance.culling.x) return false;
    if (appearance.culling.y > 0.0)
    {
        vec4 clip = PROJECTION_MATRIX * vec4(viewPosition, 1.0);
        float divisor = abs(PROJECTION_MATRIX[2][3]) > 0.5 ? max(abs(clip.w), 1.0e-6) : 1.0;
        vec2 radiusPixels = radius * vec2(abs(PROJECTION_MATRIX[0][0]), abs(PROJECTION_MATRIX[1][1])) *
            VIEWPORT_SIZE.xy * 0.5 / divisor;
        if (2.0 * max(radiusPixels.x, radiusPixels.y) < appearance.culling.y) return false;
    }
    return true;
}

void main()
{
    uint activeCount = ACTIVE_LIST_INDEX == 0u ? ACTIVE_COUNT_A : ACTIVE_COUNT_B;
    uint activeOrdinal = gl_GlobalInvocationID.x;
    if (activeOrdinal >= activeCount) return;

    uint particleIndex = ACTIVE_LIST_INDEX == 0u ? ACTIVE_INDICES_A[activeOrdinal] : ACTIVE_INDICES_B[activeOrdinal];
    ParticleRecord particle = PARTICLES[particleIndex];
    if (particle.emitterIndex >= EMITTER_COUNT) return;
    EmitterSimData emitter = EMITTERS[particle.emitterIndex];
    uint templateIndex = emitter.emissionState.w;
    if (templateIndex >= TEMPLATE_COUNT || !particleVisible(particle, emitter, TEMPLATES[templateIndex])) return;

    uint cursor = atomicAdd(COMPACTION_VALUES[TEMPLATE_CAPACITY + templateIndex], 1u);
    RENDER_INDICES[COMPACTION_VALUES[templateIndex] + cursor] = particleIndex;
}
)MPP";

	// Copies each mesh template's GPU-visible count into one command per real mesh.
	// Geometry count/index metadata is CPU-authored only when model resources are
	// resolved; this kernel authors the per-frame GPU-owned instance counts.
	inline char const* ParticleMeshCommandComputeShader = R"MPP(#version 430

layout(local_size_x = MPP_PARTICLE_WORK_GROUP_SIZE) in;
layout(std430, binding = 0) restrict readonly buffer ParticleCompactionScratch
{
    uint COMPACTION_VALUES[];
};
layout(std430, binding = 1) restrict readonly buffer ParticleMeshCommandTemplates
{
    uint MESH_TEMPLATE_INDICES[];
};
layout(std430, binding = 2) restrict buffer ParticleMeshCommands
{
    uint MESH_COMMANDS[];
};
uniform uint MESH_DRAW_COUNT;
uniform uint TEMPLATE_CAPACITY;

void main()
{
    uint drawIndex = gl_GlobalInvocationID.x;
    if (drawIndex >= MESH_DRAW_COUNT) return;
    uint templateIndex = MESH_TEMPLATE_INDICES[drawIndex];
    MESH_COMMANDS[drawIndex * 5u + 1u] = COMPACTION_VALUES[TEMPLATE_CAPACITY * 2u + templateIndex];
}
)MPP";

	// Reads one GPU-authored indirect draw command and turns its instance count
	// into the indirect work size shared by all radix stages for that appearance.
	inline char const* ParticleSortPrepareComputeShader = R"MPP(#version 430

layout(local_size_x = 1) in;
layout(std430, binding = 0) restrict readonly buffer ParticleIndirectCommands { uint INDIRECT_COMMANDS[]; };
layout(std430, binding = 1) restrict writeonly buffer ParticleSortDispatchCommand { uint DISPATCH_COMMAND[]; };
uniform uint TEMPLATE_INDEX;

void main()
{
    uint count = INDIRECT_COMMANDS[TEMPLATE_INDEX * 4u + 1u];
    DISPATCH_COMMAND[0] = (count + MPP_PARTICLE_WORK_GROUP_SIZE - 1u) / MPP_PARTICLE_WORK_GROUP_SIZE;
    DISPATCH_COMMAND[1] = 1u;
    DISPATCH_COMMAND[2] = 1u;
}
)MPP";

	// Generates an IEEE-754 depth key and particle-index pair for one opted-in
	// alpha appearance. Complementing the monotonic float key makes the ascending
	// radix result render far-to-near.
	inline char const* ParticleSortKeyComputeShader = R"MPP(#version 430

layout(local_size_x = MPP_PARTICLE_WORK_GROUP_SIZE) in;
layout(std140, binding = 3) uniform CameraFrame
{
    mat4 VIEW_MATRIX;
    mat4 PROJECTION_MATRIX;
    mat4 INVERSE_PROJECTION_MATRIX;
    vec4 VIEWPORT_SIZE;
    vec4 NEAR_FAR_TIME;
};
struct ParticleRecord { vec4 positionAge; vec4 velocityLifetime; uint packedColour; float baseSize; float rotation; float angularVelocity; uint emitterIndex; uint seed; uint flags; uint padding; };
layout(std430, binding = 0) restrict readonly buffer ParticlePool { ParticleRecord PARTICLES[]; };
layout(std430, binding = 1) restrict readonly buffer ParticleRenderIndices { uint RENDER_INDICES[]; };
layout(std430, binding = 2) restrict readonly buffer ParticleIndirectCommands { uint INDIRECT_COMMANDS[]; };
layout(std430, binding = 3) restrict writeonly buffer ParticleSortRecords { uvec2 SORT_RECORDS[]; };
uniform uint TEMPLATE_INDEX;

uint descendingFloatKey(float value)
{
    uint bits = floatBitsToUint(value);
    uint ascending = (bits & 0x80000000u) != 0u ? ~bits : bits ^ 0x80000000u;
    return ~ascending;
}

void main()
{
    uint command = TEMPLATE_INDEX * 4u;
    uint count = INDIRECT_COMMANDS[command + 1u];
    uint localOrdinal = gl_GlobalInvocationID.x;
    if (localOrdinal >= count) return;
    uint rangeOffset = INDIRECT_COMMANDS[command + 2u] / 4u;
    uint ordinal = rangeOffset + localOrdinal;
    uint particleIndex = RENDER_INDICES[ordinal];
    float viewDepth = -(VIEW_MATRIX * vec4(PARTICLES[particleIndex].positionAge.xyz, 1.0)).z;
    SORT_RECORDS[ordinal] = uvec2(descendingFloatKey(viewDepth), particleIndex);
}
)MPP";

	// One histogram belongs to each work group. Reusing the same scratch from one
	// appearance and nibble to the next avoids both clears and capacity-sized work
	// for additive appearances.
	inline char const* ParticleRadixHistogramComputeShader = R"MPP(#version 430

layout(local_size_x = MPP_PARTICLE_WORK_GROUP_SIZE) in;
layout(std430, binding = 0) restrict readonly buffer ParticleSortRecords { uvec2 SORT_RECORDS[]; };
layout(std430, binding = 1) restrict readonly buffer ParticleIndirectCommands { uint INDIRECT_COMMANDS[]; };
layout(std430, binding = 2) restrict writeonly buffer ParticleRadixHistogram { uint GROUP_BUCKETS[]; };
uniform uint TEMPLATE_INDEX;
uniform uint RADIX_SHIFT;
shared uint LOCAL_BUCKETS[16];

void main()
{
    for (uint bucket = gl_LocalInvocationID.x; bucket < 16u; bucket += gl_WorkGroupSize.x)
        LOCAL_BUCKETS[bucket] = 0u;
    barrier();
    uint command = TEMPLATE_INDEX * 4u;
    uint count = INDIRECT_COMMANDS[command + 1u];
    uint localOrdinal = gl_GlobalInvocationID.x;
    if (localOrdinal < count)
    {
        uint rangeOffset = INDIRECT_COMMANDS[command + 2u] / 4u;
        uint digit = (SORT_RECORDS[rangeOffset + localOrdinal].x >> RADIX_SHIFT) & 15u;
        atomicAdd(LOCAL_BUCKETS[digit], 1u);
    }
    barrier();
    for (uint bucket = gl_LocalInvocationID.x; bucket < 16u; bucket += gl_WorkGroupSize.x)
        GROUP_BUCKETS[gl_WorkGroupID.x * 16u + bucket] = LOCAL_BUCKETS[bucket];
}
)MPP";

	// The capped pool has at most 16,384 groups. A single deterministic scan is
	// small relative to sorting the particles and supplies stable global offsets
	// without subgroup extensions or a CPU-visible count.
	inline char const* ParticleRadixPrefixComputeShader = R"MPP(#version 430

layout(local_size_x = 1) in;
layout(std430, binding = 0) restrict readonly buffer ParticleIndirectCommands { uint INDIRECT_COMMANDS[]; };
layout(std430, binding = 1) restrict buffer ParticleRadixHistogram { uint GROUP_BUCKETS[]; };
uniform uint TEMPLATE_INDEX;

void main()
{
    uint count = INDIRECT_COMMANDS[TEMPLATE_INDEX * 4u + 1u];
    uint groupCount = (count + MPP_PARTICLE_WORK_GROUP_SIZE - 1u) / MPP_PARTICLE_WORK_GROUP_SIZE;
    uint bucketBase = 0u;
    for (uint bucket = 0u; bucket < 16u; ++bucket)
    {
        uint running = 0u;
        for (uint group = 0u; group < groupCount; ++group)
        {
            uint address = group * 16u + bucket;
            uint groupCountForBucket = GROUP_BUCKETS[address];
            GROUP_BUCKETS[address] = bucketBase + running;
            running += groupCountForBucket;
        }
        bucketBase += running;
    }
}
)MPP";

	// Stable scatter is the defining radix property. Each lane computes its rank
	// among earlier lanes with the same nibble; the prefix stage contributes every
	// earlier work group's count. Eight four-bit passes cover the complete key.
	inline char const* ParticleRadixScatterComputeShader = R"MPP(#version 430

layout(local_size_x = MPP_PARTICLE_WORK_GROUP_SIZE) in;
layout(std430, binding = 0) restrict readonly buffer ParticleSortInput { uvec2 INPUT_RECORDS[]; };
layout(std430, binding = 1) restrict writeonly buffer ParticleSortOutput { uvec2 OUTPUT_RECORDS[]; };
layout(std430, binding = 2) restrict readonly buffer ParticleIndirectCommands { uint INDIRECT_COMMANDS[]; };
layout(std430, binding = 3) restrict readonly buffer ParticleRadixHistogram { uint GROUP_BUCKETS[]; };
uniform uint TEMPLATE_INDEX;
uniform uint RADIX_SHIFT;
shared uint LOCAL_DIGITS[MPP_PARTICLE_WORK_GROUP_SIZE];

void main()
{
    uint command = TEMPLATE_INDEX * 4u;
    uint count = INDIRECT_COMMANDS[command + 1u];
    uint localOrdinal = gl_GlobalInvocationID.x;
    uint lane = gl_LocalInvocationID.x;
    uint rangeOffset = INDIRECT_COMMANDS[command + 2u] / 4u;
    uvec2 record = uvec2(0u);
    uint digit = 16u;
    if (localOrdinal < count)
    {
        record = INPUT_RECORDS[rangeOffset + localOrdinal];
        digit = (record.x >> RADIX_SHIFT) & 15u;
    }
    LOCAL_DIGITS[lane] = digit;
    barrier();
    if (localOrdinal >= count) return;
    uint localRank = 0u;
    for (uint earlier = 0u; earlier < lane; ++earlier)
        if (LOCAL_DIGITS[earlier] == digit) ++localRank;
    uint destination = GROUP_BUCKETS[gl_WorkGroupID.x * 16u + digit] + localRank;
    OUTPUT_RECORDS[rangeOffset + destination] = record;
}
)MPP";

	inline char const* ParticleSortFinalizeComputeShader = R"MPP(#version 430

layout(local_size_x = MPP_PARTICLE_WORK_GROUP_SIZE) in;
layout(std430, binding = 0) restrict readonly buffer ParticleSortRecords { uvec2 SORT_RECORDS[]; };
layout(std430, binding = 1) restrict writeonly buffer ParticleRenderIndices { uint RENDER_INDICES[]; };
layout(std430, binding = 2) restrict readonly buffer ParticleIndirectCommands { uint INDIRECT_COMMANDS[]; };
uniform uint TEMPLATE_INDEX;

void main()
{
    uint command = TEMPLATE_INDEX * 4u;
    uint count = INDIRECT_COMMANDS[command + 1u];
    uint localOrdinal = gl_GlobalInvocationID.x;
    if (localOrdinal >= count) return;
    uint rangeOffset = INDIRECT_COMMANDS[command + 2u] / 4u;
    RENDER_INDICES[rangeOffset + localOrdinal] = SORT_RECORDS[rangeOffset + localOrdinal].y;
}
)MPP";

	// Attribute-less instanced billboards. Basis-only modes share square
	// expansion; velocity-stretched mode supplies independent half-extents.
	inline char const* ParticleDrawVertexShader = R"MPP(#version 430

layout(std140, binding = 3) uniform CameraFrame
{
    mat4 VIEW_MATRIX;
    mat4 PROJECTION_MATRIX;
    mat4 INVERSE_PROJECTION_MATRIX;
    vec4 VIEWPORT_SIZE;
    vec4 NEAR_FAR_TIME;
};

const uint BILLBOARD_CAMERA_FACING = 0u;
const uint BILLBOARD_SCREEN_ALIGNED = 1u;
const uint BILLBOARD_CYLINDRICAL = 2u;
const uint BILLBOARD_AXIS_LOCKED = 3u;
const uint BILLBOARD_VELOCITY_ALIGNED = 4u;
const uint BILLBOARD_VELOCITY_STRETCHED = 5u;
const uint ANIMATION_PLAYBACK_MASK = 0xffu;
const uint ANIMATION_FRAME_OVER_LIFE = 1u;
const uint ANIMATION_FIXED_RATE = 2u;
const uint ANIMATION_RANDOM_START = 1u << 8u;

struct ParticleRecord
{
    vec4 positionAge;
    vec4 velocityLifetime;
    uint packedColour;
    float baseSize;
    float rotation;
    float angularVelocity;
    uint emitterIndex;
    uint seed;
    uint flags;
    uint padding;
};

struct EmitterSimData
{
    mat4 transform;
    vec4 shapeParameters;
    vec4 initialVelocityMin;
    vec4 initialVelocityMax;
    vec4 colourMin;
    vec4 colourMax;
    vec4 lifetimeSizeRanges;
    vec4 rotationRanges;
    uvec4 shapeSeedModulesBudget;
    uvec4 emissionState;
    vec4 emissionRateAndPadding;
    vec4 parameterMultipliers0;
    vec4 parameterMultipliers1;
    vec4 gravityAndDrag;
    vec4 noiseFrequencyStrength;
    vec4 noiseScrollAndTimeScale;
    vec4 curlNoiseFrequencyStrength;
    vec4 curlNoiseScrollAndTimeScale;
    vec4 turbulenceFrequencyStrength;
    vec4 turbulenceScrollAndTimeScale;
    vec4 turbulenceOctavesLacunarityGain;
    vec4 vectorFieldFrequencyStrength;
    vec4 vectorFieldScrollAndTimeScale;
    uvec4 collisionConfiguration;
    vec4 collisionParameters;
    uvec4 eventRange;
};

struct TemplateRenderData
{
    uvec4 textureAndAtlas;
    vec4 tintAndAlpha;
    vec4 appearance;
    uvec4 modes;
    vec4 culling;
    uvec4 sorting;
};

layout(std430, binding = 0) restrict readonly buffer ParticlePool { ParticleRecord PARTICLES[]; };
layout(std430, binding = 1) restrict readonly buffer ParticleRenderIndices { uint RENDER_INDICES[]; };
layout(std430, binding = 5) restrict readonly buffer ParticleEmitters { EmitterSimData EMITTERS[]; };
layout(std430, binding = 6) restrict readonly buffer ParticleTemplates { TemplateRenderData TEMPLATES[]; };
uniform sampler2D PARTICLE_CURVE_LUT;

out vec2 PARTICLE_UV;
out vec2 PARTICLE_CORNER;
out vec4 PARTICLE_TINT;
out float PARTICLE_VIEW_DEPTH;
flat out uvec2 PARTICLE_TEXTURE_LAYER_FLAGS;
flat out float PARTICLE_SOFT_DISTANCE;
flat out float PARTICLE_DISTORTION_STRENGTH;

vec3 safeNormal(vec3 value, vec3 fallback)
{
    float magnitudeSquared = dot(value, value);
    return magnitudeSquared > 1e-10 ? value * inversesqrt(magnitudeSquared) : fallback;
}

vec4 sampleCurveRow(float normalizedLife, float row)
{
    vec2 dimensions = vec2(textureSize(PARTICLE_CURVE_LUT, 0));
    vec2 texel = vec2(clamp(normalizedLife, 0.0, 1.0) * (dimensions.x - 1.0) + 0.5, row + 0.5);
    return texture(PARTICLE_CURVE_LUT, texel / dimensions);
}

void billboardBasis(ParticleRecord particle, EmitterSimData emitter, uint mode,
    vec3 cameraPosition, vec3 screenRight, vec3 screenUp, vec3 viewForward,
    out vec3 right, out vec3 up)
{
    vec3 toCamera = safeNormal(cameraPosition - particle.positionAge.xyz, -viewForward);
    if (mode == BILLBOARD_SCREEN_ALIGNED)
    {
        right = screenRight;
        up = screenUp;
    }
    else if (mode == BILLBOARD_CYLINDRICAL)
    {
        up = vec3(0.0, 1.0, 0.0);
        right = safeNormal(cross(up, toCamera), screenRight);
    }
    else if (mode == BILLBOARD_AXIS_LOCKED)
    {
        up = safeNormal(mat3(emitter.transform) * vec3(0.0, 1.0, 0.0), screenUp);
        right = safeNormal(cross(up, toCamera), screenRight);
    }
    else if (mode == BILLBOARD_VELOCITY_ALIGNED || mode == BILLBOARD_VELOCITY_STRETCHED)
    {
        vec3 projectedVelocity = particle.velocityLifetime.xyz - viewForward * dot(particle.velocityLifetime.xyz, viewForward);
        up = safeNormal(projectedVelocity, screenUp);
        right = safeNormal(cross(viewForward, up), screenRight);
    }
    else
    {
        right = safeNormal(cross(screenUp, toCamera), screenRight);
        up = safeNormal(cross(toCamera, right), screenUp);
    }
}

vec2 particleHalfExtents(ParticleRecord particle, uint mode, vec3 viewForward, float size)
{
    if (mode != BILLBOARD_VELOCITY_STRETCHED) return vec2(size);
    vec3 projectedVelocity = particle.velocityLifetime.xyz - viewForward * dot(particle.velocityLifetime.xyz, viewForward);
    // Screen-plane speed contributes a dimensionless stretch ratio: stationary
    // particles remain square and faster motion lengthens only the velocity axis.
    return vec2(size, size * (1.0 + length(projectedVelocity)));
}

vec3 expandParticleQuad(vec3 centre, vec3 right, vec3 up, vec2 corner,
    vec2 halfExtents, float rotation)
{
    vec2 offset = corner * 2.0 - 1.0;
    float sineRotation = sin(rotation);
    float cosineRotation = cos(rotation);
    offset = mat2(cosineRotation, sineRotation, -sineRotation, cosineRotation) * offset;
    return centre + right * offset.x * halfExtents.x + up * offset.y * halfExtents.y;
}

uint flipbookFrame(ParticleRecord particle, TemplateRenderData appearance)
{
    uint frameCount = max(1u, min(appearance.modes.x,
        max(1u, appearance.textureAndAtlas.z) * max(1u, appearance.textureAndAtlas.w)));
    uint animation = appearance.modes.y;
    uint frame = 0u;
    uint playback = animation & ANIMATION_PLAYBACK_MASK;
    if (playback == ANIMATION_FRAME_OVER_LIFE && particle.velocityLifetime.w > 0.0)
    {
        float life = clamp(particle.positionAge.w / particle.velocityLifetime.w, 0.0, 0.99999994);
        frame = min(uint(life * float(frameCount)), frameCount - 1u);
    }
    else if (playback == ANIMATION_FIXED_RATE)
        frame = uint(max(0.0, floor(particle.positionAge.w * max(0.0, appearance.appearance.z)))) % frameCount;
    if ((animation & ANIMATION_RANDOM_START) != 0u)
        frame = (frame + particle.seed % frameCount) % frameCount;
    return frame;
}

void main()
{
    // command.first is rangeOffset * 4: its low bits select a quad corner and
    // its upper bits retain the compact render-list offset.
    uint renderOrdinal = (uint(gl_VertexID) >> 2u) + uint(gl_InstanceID);
    ParticleRecord particle = PARTICLES[RENDER_INDICES[renderOrdinal]];
    EmitterSimData emitter = EMITTERS[particle.emitterIndex];
    TemplateRenderData appearance = TEMPLATES[emitter.emissionState.w];

    vec2 corner = vec2(float(gl_VertexID & 1), float((gl_VertexID >> 1) & 1));
    mat3 inverseViewRotation = transpose(mat3(VIEW_MATRIX));
    vec3 screenRight = safeNormal(inverseViewRotation[0], vec3(1.0, 0.0, 0.0));
    vec3 screenUp = safeNormal(inverseViewRotation[1], vec3(0.0, 1.0, 0.0));
    vec3 viewForward = safeNormal(-inverseViewRotation[2], vec3(0.0, 0.0, -1.0));
    vec3 cameraPosition = inverseViewRotation * -VIEW_MATRIX[3].xyz;
    float normalizedLife = particle.velocityLifetime.w > 0.0 ?
        clamp(particle.positionAge.w / particle.velocityLifetime.w, 0.0, 1.0) : 0.0;
    float rowOffset = appearance.appearance.w;
    vec4 scalarCurves0 = sampleCurveRow(normalizedLife, rowOffset);
    vec4 scalarCurves1 = sampleCurveRow(normalizedLife, rowOffset + 1.0);
    vec3 colourOverLife = sampleCurveRow(normalizedLife, rowOffset + 2.0).rgb;

    vec3 right;
    vec3 up;
    billboardBasis(particle, emitter, appearance.modes.z, cameraPosition,
        screenRight, screenUp, viewForward, right, up);
    float size = particle.baseSize * scalarCurves0.x;
    vec2 halfExtents = particleHalfExtents(particle, appearance.modes.z, viewForward, size);
    // Rotation would turn the long axis away from projected velocity, so the
    // stretched mode deliberately treats velocity as its complete orientation.
    float rotation = appearance.modes.z == BILLBOARD_VELOCITY_STRETCHED ? 0.0 : particle.rotation;
    vec3 worldPosition = expandParticleQuad(particle.positionAge.xyz, right, up,
        corner, halfExtents, rotation);

    uint columns = max(1u, appearance.textureAndAtlas.z);
    uint rows = max(1u, appearance.textureAndAtlas.w);
    uint frame = flipbookFrame(particle, appearance);
    vec2 atlasCell = vec2(float(frame % columns), float(frame / columns));
    PARTICLE_UV = (atlasCell + corner) / vec2(float(columns), float(rows));
    PARTICLE_CORNER = corner;
    PARTICLE_TINT = unpackUnorm4x8(particle.packedColour) * appearance.tintAndAlpha;
    PARTICLE_TINT.rgb *= colourOverLife * appearance.appearance.x * emitter.parameterMultipliers1.y * scalarCurves1.y;
    PARTICLE_TINT.a *= scalarCurves0.y * emitter.parameterMultipliers1.x;
    PARTICLE_VIEW_DEPTH = -(VIEW_MATRIX * vec4(particle.positionAge.xyz, 1.0)).z;
    PARTICLE_TEXTURE_LAYER_FLAGS = appearance.textureAndAtlas.xy;
    PARTICLE_SOFT_DISTANCE = max(0.0, appearance.appearance.y);
    PARTICLE_DISTORTION_STRENGTH = appearance.culling.w;
    gl_Position = PROJECTION_MATRIX * VIEW_MATRIX * vec4(worldPosition, 1.0);
}
)MPP";

	inline char const* ParticleDrawFragmentShader = R"MPP(#version 430

layout(std140, binding = 3) uniform CameraFrame
{
    mat4 VIEW_MATRIX;
    mat4 PROJECTION_MATRIX;
    mat4 INVERSE_PROJECTION_MATRIX;
    vec4 VIEWPORT_SIZE;
    vec4 NEAR_FAR_TIME;
};

in vec2 PARTICLE_UV;
in vec2 PARTICLE_CORNER;
in vec4 PARTICLE_TINT;
in float PARTICLE_VIEW_DEPTH;
flat in uvec2 PARTICLE_TEXTURE_LAYER_FLAGS;
flat in float PARTICLE_SOFT_DISTANCE;
flat in float PARTICLE_DISTORTION_STRENGTH;
uniform sampler2D SCENE_DEPTH;
uniform sampler2DArray PARTICLE_ALBEDO_ARRAY;
uniform int HAS_SCENE_DEPTH;
layout(location = 0) out vec4 FRAGMENT_COLOUR;
layout(location = 1) out vec4 FRAGMENT_BLOOM;

float linearViewDepth(float depth)
{
    float nearPlane = NEAR_FAR_TIME.x;
    float farPlane = NEAR_FAR_TIME.y;
    float ndc = depth * 2.0 - 1.0;
    return (2.0 * nearPlane * farPlane) /
        max(farPlane + nearPlane - ndc * (farPlane - nearPlane), 0.000001);
}

vec3 srgbToLinear(vec3 encoded)
{
    bvec3 usePower = greaterThan(encoded, vec3(0.04045));
    vec3 low = encoded / 12.92;
    vec3 high = pow((encoded + 0.055) / 1.055, vec3(2.4));
    return mix(low, high, usePower);
}

void main()
{
    bool hasAlbedo = (PARTICLE_TEXTURE_LAYER_FLAGS.y & 1u) != 0u;
    vec4 albedo = hasAlbedo ?
        texture(PARTICLE_ALBEDO_ARRAY, vec3(PARTICLE_UV, float(PARTICLE_TEXTURE_LAYER_FLAGS.x))) : vec4(1.0);
    if ((PARTICLE_TEXTURE_LAYER_FLAGS.y & 2u) != 0u) albedo.rgb = srgbToLinear(albedo.rgb);
    float edge = length(PARTICLE_CORNER * 2.0 - 1.0);
    float coverage = 1.0 - smoothstep(0.75, 1.0, edge);
    float softFade = 1.0;
    if (HAS_SCENE_DEPTH != 0 && PARTICLE_SOFT_DISTANCE > 0.0)
    {
        vec2 depthUv = gl_FragCoord.xy / max(VIEWPORT_SIZE.xy, vec2(1.0));
        float sceneDepth = texture(SCENE_DEPTH, depthUv).r;
        if (sceneDepth < 1.0)
            softFade = clamp((linearViewDepth(sceneDepth) - PARTICLE_VIEW_DEPTH) /
                PARTICLE_SOFT_DISTANCE, 0.0, 1.0);
    }
    vec4 colour = PARTICLE_TINT * albedo;
    colour.a *= coverage * softFade;
#if MPP_PARTICLE_DISTORTION
    // RG stores a normalized-screen UV offset. Authored textures use their RG
    // channels as a signed vector; an omitted texture gets a radial shockwave/
    // heat-haze fallback from the billboard centre. Alpha remains the mask.
    vec2 direction = hasAlbedo ? albedo.rg * 2.0 - 1.0 :
        normalize(PARTICLE_CORNER * 2.0 - 1.0 + vec2(1.0e-6));
    FRAGMENT_COLOUR = vec4(direction * PARTICLE_DISTORTION_STRENGTH * colour.a, 0.0, 0.0);
    FRAGMENT_BLOOM = vec4(0.0);
#elif MPP_PARTICLE_WEIGHTED_OIT
    float alpha = clamp(colour.a, 0.0, 1.0);
    // McGuire/Bavoil weighted blended OIT: favour opaque, near fragments while
    // keeping every operation commutative. Revealage is stored as optical depth
    // so both attachments can use the same authored additive blend state.
    float alphaWeight = pow(min(1.0, alpha * 10.0) + 0.01, 3.0);
    float depthWeight = pow(1.0 - gl_FragCoord.z * 0.9, 3.0);
    float weight = clamp(alphaWeight * 1.0e3 * depthWeight, 1.0e-2, 3.0e3);
    FRAGMENT_COLOUR = vec4(colour.rgb * alpha, alpha) * weight;
    FRAGMENT_BLOOM = vec4(-log(max(1.0 - alpha, 1.0e-5)), 0.0, 0.0, 0.0);
#else
    FRAGMENT_COLOUR = colour;
    FRAGMENT_BLOOM = vec4(colour.rgb, colour.a);
#endif
}
)MPP";

	// One billboarded sphere per emitter integrates a bounded proxy volume along
	// the camera ray. This is intentionally emitter-driven: particle count never
	// changes the draw count or creates dynamic-light records.
	inline char const* ParticleVolumetricLightingVertexShader = R"MPP(#version 430

struct VolumetricLightingData
{
    vec4 positionAndRange;
    vec4 colourAndIntensity;
    vec4 volumetricAndPadding;
    uvec4 flagsAndPadding;
};

layout(std430, binding = 0) restrict readonly buffer ParticleVolumetricLighting
{
    VolumetricLightingData LIGHTING[];
};

layout(std140, binding = 3) uniform CameraFrame
{
    mat4 VIEW_MATRIX;
    mat4 PROJECTION_MATRIX;
    mat4 INVERSE_PROJECTION_MATRIX;
    vec4 VIEWPORT_SIZE;
    vec4 NEAR_FAR_TIME;
};

out vec2 VOLUME_LOCAL;
flat out vec4 VOLUME_COLOUR_INTENSITY;
flat out vec2 VOLUME_DEPTH_RANGE;
flat out float VOLUME_INTENSITY;

void main()
{
    VolumetricLightingData lighting = LIGHTING[gl_InstanceID];
    uint flags = lighting.flagsAndPadding.x;
    float range = lighting.positionAndRange.w;
    float intensity = lighting.volumetricAndPadding.x;
    if ((flags & 4u) == 0u || (flags & 8u) == 0u || range <= 0.0 || intensity <= 0.0)
    {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        return;
    }

    const vec2 corners[4] = vec2[4](
        vec2(-1.0, -1.0), vec2(1.0, -1.0),
        vec2(-1.0,  1.0), vec2(1.0,  1.0));
    vec2 corner = corners[gl_VertexID];
    vec3 centreView = (VIEW_MATRIX * vec4(lighting.positionAndRange.xyz, 1.0)).xyz;
    vec3 billboardView = centreView + vec3(corner * range, 0.0);
    VOLUME_LOCAL = corner;
    VOLUME_COLOUR_INTENSITY = lighting.colourAndIntensity;
    VOLUME_DEPTH_RANGE = vec2(-centreView.z, range);
    VOLUME_INTENSITY = intensity;
    gl_Position = PROJECTION_MATRIX * vec4(billboardView, 1.0);
}
)MPP";

	inline char const* ParticleVolumetricLightingFragmentShader = R"MPP(#version 430

layout(std140, binding = 3) uniform CameraFrame
{
    mat4 VIEW_MATRIX;
    mat4 PROJECTION_MATRIX;
    mat4 INVERSE_PROJECTION_MATRIX;
    vec4 VIEWPORT_SIZE;
    vec4 NEAR_FAR_TIME;
};

in vec2 VOLUME_LOCAL;
flat in vec4 VOLUME_COLOUR_INTENSITY;
flat in vec2 VOLUME_DEPTH_RANGE;
flat in float VOLUME_INTENSITY;
uniform sampler2D SCENE_DEPTH;
uniform int HAS_SCENE_DEPTH;
layout(location = 0) out vec4 FRAGMENT_COLOUR;
layout(location = 1) out vec4 FRAGMENT_BLOOM;

float linearViewDepth(float depth)
{
    float nearPlane = NEAR_FAR_TIME.x;
    float farPlane = NEAR_FAR_TIME.y;
    float ndc = depth * 2.0 - 1.0;
    return (2.0 * nearPlane * farPlane) /
        max(farPlane + nearPlane - ndc * (farPlane - nearPlane), 0.000001);
}

void main()
{
    float radiusSquared = dot(VOLUME_LOCAL, VOLUME_LOCAL);
    if (radiusSquared >= 1.0) discard;
    float halfPath = VOLUME_DEPTH_RANGE.y * sqrt(max(1.0 - radiusSquared, 0.0));
    float nearDepth = max(VOLUME_DEPTH_RANGE.x - halfPath, 0.0);
    float farDepth = VOLUME_DEPTH_RANGE.x + halfPath;
    float visibleFraction = 1.0;
    if (HAS_SCENE_DEPTH != 0)
    {
        vec2 uv = gl_FragCoord.xy / max(VIEWPORT_SIZE.xy, vec2(1.0));
        float sampledDepth = texture(SCENE_DEPTH, uv).r;
        if (sampledDepth < 1.0)
            visibleFraction = clamp((linearViewDepth(sampledDepth) - nearDepth) /
                max(farDepth - nearDepth, 0.000001), 0.0, 1.0);
    }
    float integratedPath = 2.0 * sqrt(max(1.0 - radiusSquared, 0.0));
    vec3 radiance = VOLUME_COLOUR_INTENSITY.rgb * VOLUME_COLOUR_INTENSITY.a *
        VOLUME_INTENSITY * integratedPath * visibleFraction;
    FRAGMENT_COLOUR = vec4(radiance, 0.0);
    FRAGMENT_BLOOM = vec4(radiance, 0.0);
}
)MPP";

	inline char const* ParticleDistortionCompositeFragmentShader = R"MPP(
@@Version

@@Texture(sampler2D SCENE);
@@Texture(sampler2D DISTORTION);

void main()
{
    vec2 uv = @In(TEXCOORDS);
    vec2 offset = texture(@Texture(DISTORTION), uv).rg;
    // Clamp at the viewport edge rather than depending on an authored scene
    // sampler's wrap mode, which may legitimately be repeat for another pass.
    @Out(vec4 COLOUR) = texture(@Texture(SCENE), clamp(uv + offset, vec2(0.0), vec2(1.0)));
}
)MPP";

	// Composite the weighted average over the scene. The second accumulation
	// texture contains summed optical depth, whose exponential is mathematically
	// the same order-independent revealage product used by the two-blend-function
	// formulation, without requiring per-attachment blend state.
	inline char const* ParticleWeightedOitResolveFragmentShader = R"MPP(
@@Version

@@Uniform(int HAS_BLOOM);
@@Texture(sampler2D SCENE);
@@Texture(sampler2D ACCUMULATION);
@@Texture(sampler2D OPTICAL_DEPTH);
@@Texture(sampler2D BLOOM);

void main()
{
    vec2 uv = @In(TEXCOORDS);
    vec4 scene = texture(@Texture(SCENE), uv);
    vec4 accumulation = texture(@Texture(ACCUMULATION), uv);
    float opticalDepth = max(texture(@Texture(OPTICAL_DEPTH), uv).r, 0.0);
    float transmittance = exp(-opticalDepth);
    float opacity = 1.0 - transmittance;
    vec3 weightedColour = accumulation.rgb / max(accumulation.a, 1.0e-5);
    vec3 particleColour = accumulation.a > 1.0e-5 ? weightedColour : vec3(0.0);

    @Out(vec4 COLOUR) = vec4(
        particleColour * opacity + scene.rgb * transmittance,
        opacity + scene.a * transmittance);

    vec4 bloom = @Uniform(HAS_BLOOM) != 0 ? texture(@Texture(BLOOM), uv) : vec4(0.0);
    @Out(vec4 BLOOM_MASK) = vec4(
        particleColour * opacity + bloom.rgb * transmittance,
        opacity + bloom.a * transmittance);
}
)MPP";
}
