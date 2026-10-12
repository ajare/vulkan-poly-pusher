#pragma once

namespace mpp
{
	// Ages each trail's private ring of points, records one position-history
	// sample when needed, and writes dedicated indirect ribbon commands. One
	// invocation owns one trail, so ring mutation needs no atomics or readback.
	inline char const* TrailUpdateComputeShader = R"MPP(#version 430

layout(local_size_x = MPP_TRAIL_WORK_GROUP_SIZE) in;

struct TrailPointRecord
{
    vec4 positionAge;
    vec4 lifetimeDistance;
};
struct TrailState
{
    uvec4 ring;
    vec4 samplePositionDistance;
};
struct TrailControlData
{
    vec4 positionEnabled;
    vec4 lifetimeDistanceUvWidth;
    vec4 tintAndAlpha;
    vec4 appearance;
    uvec4 modes;
};

layout(std430, binding = 0) restrict buffer TrailPoints { TrailPointRecord TRAIL_POINTS[]; };
layout(std430, binding = 1) restrict buffer TrailStates { TrailState TRAIL_STATES[]; };
layout(std430, binding = 2) restrict readonly buffer TrailControls { TrailControlData TRAIL_CONTROLS[]; };
layout(std430, binding = 3) restrict writeonly buffer TrailCommands { uint TRAIL_COMMANDS[]; };

uniform uint TRAIL_COUNT;
uniform float DELTA_SECONDS;

uint pointAddress(uint trailIndex, uint localIndex)
{
    return trailIndex * MPP_TRAIL_MAX_POINTS + localIndex;
}

void clearCommands(uint trailIndex)
{
    for (uint blend = 0u; blend < 2u; ++blend)
    {
        uint command = (blend * MPP_TRAIL_MAX_TRAILS + trailIndex) * 4u;
        TRAIL_COMMANDS[command] = 0u;
        TRAIL_COMMANDS[command + 1u] = 1u;
        TRAIL_COMMANDS[command + 2u] = trailIndex * MPP_TRAIL_MAX_POINTS * 2u;
        TRAIL_COMMANDS[command + 3u] = 0u;
    }
}

void main()
{
    uint trailIndex = gl_GlobalInvocationID.x;
    if (trailIndex >= TRAIL_COUNT) return;
    TrailControlData control = TRAIL_CONTROLS[trailIndex];
    TrailState state = TRAIL_STATES[trailIndex];
    clearCommands(trailIndex);

    uint capacity = clamp(control.modes.w, 2u, uint(MPP_TRAIL_MAX_POINTS));
    if (control.modes.x == 0u)
    {
        state = TrailState(uvec4(0u, 0u, control.modes.z, 0u), vec4(0.0));
        TRAIL_STATES[trailIndex] = state;
        return;
    }
    if (state.ring.z != control.modes.z)
        state = TrailState(uvec4(0u, 0u, control.modes.z, 0u), vec4(0.0));

    uint first = state.ring.x;
    uint count = min(state.ring.y, capacity);
    for (uint ordinal = 0u; ordinal < count; ++ordinal)
    {
        uint localIndex = (first + ordinal) % capacity;
        TRAIL_POINTS[pointAddress(trailIndex, localIndex)].positionAge.w += DELTA_SECONDS;
    }
    while (count > 0u)
    {
        TrailPointRecord oldest = TRAIL_POINTS[pointAddress(trailIndex, first)];
        if (oldest.positionAge.w < oldest.lifetimeDistance.x) break;
        first = (first + 1u) % capacity;
        --count;
    }

    if (control.positionEnabled.w > 0.5)
    {
        vec3 position = control.positionEnabled.xyz;
        float lifetime = max(control.lifetimeDistanceUvWidth.x, 1.0e-6);
        if (count == 0u)
        {
            first = 0u;
            TRAIL_POINTS[pointAddress(trailIndex, first)] =
                TrailPointRecord(vec4(position, 0.0), vec4(lifetime, 0.0, 0.0, 0.0));
            count = 1u;
            state.samplePositionDistance = vec4(position, 0.0);
        }
        else
        {
            uint newestLocal = (first + count - 1u) % capacity;
            TrailPointRecord newest = TRAIL_POINTS[pointAddress(trailIndex, newestLocal)];
            float movement = length(position - newest.positionAge.xyz);
            float cumulativeDistance = newest.lifetimeDistance.y + movement;
            float fixedDistance = length(position - state.samplePositionDistance.xyz);
            bool appendPoint = fixedDistance >= max(control.lifetimeDistanceUvWidth.y, 0.0) &&
                (fixedDistance > 1.0e-6 || control.lifetimeDistanceUvWidth.y <= 0.0);
            if (appendPoint)
            {
                if (count == capacity)
                {
                    first = (first + 1u) % capacity;
                    --count;
                }
                uint destination = (first + count) % capacity;
                TRAIL_POINTS[pointAddress(trailIndex, destination)] =
                    TrailPointRecord(vec4(position, 0.0), vec4(lifetime, cumulativeDistance, 0.0, 0.0));
                ++count;
                state.samplePositionDistance.xyz = position;
            }
            else
            {
                newest.positionAge = vec4(position, 0.0);
                newest.lifetimeDistance = vec4(lifetime, cumulativeDistance, 0.0, 0.0);
                TRAIL_POINTS[pointAddress(trailIndex, newestLocal)] = newest;
            }
            state.samplePositionDistance.w = cumulativeDistance;
        }
    }

    state.ring = uvec4(first, count, control.modes.z, 0u);
    TRAIL_STATES[trailIndex] = state;
    uint blend = min(control.modes.y, 1u);
    uint command = (blend * MPP_TRAIL_MAX_TRAILS + trailIndex) * 4u;
    TRAIL_COMMANDS[command] = count >= 2u ? count * 2u : 0u;
}
)MPP";

	inline char const* TrailDrawVertexShader = R"MPP(#version 430

layout(std140, binding = 3) uniform CameraFrame
{
    mat4 VIEW_MATRIX;
    mat4 PROJECTION_MATRIX;
    mat4 INVERSE_PROJECTION_MATRIX;
    vec4 VIEWPORT_SIZE;
    vec4 NEAR_FAR_TIME;
};

struct TrailPointRecord { vec4 positionAge; vec4 lifetimeDistance; };
struct TrailState { uvec4 ring; vec4 samplePositionDistance; };
struct TrailControlData { vec4 positionEnabled; vec4 lifetimeDistanceUvWidth; vec4 tintAndAlpha; vec4 appearance; uvec4 modes; };
layout(std430, binding = 0) restrict readonly buffer TrailPoints { TrailPointRecord TRAIL_POINTS[]; };
layout(std430, binding = 1) restrict readonly buffer TrailStates { TrailState TRAIL_STATES[]; };
layout(std430, binding = 2) restrict readonly buffer TrailControls { TrailControlData TRAIL_CONTROLS[]; };
uniform sampler2D TRAIL_CURVE_LUT;

out vec2 TRAIL_UV;
out vec4 TRAIL_COLOUR;
out float TRAIL_VIEW_DEPTH;
flat out float TRAIL_SOFT_DISTANCE;

vec3 safeNormal(vec3 value, vec3 fallback)
{
    float magnitudeSquared = dot(value, value);
    return magnitudeSquared > 1.0e-12 ? value * inversesqrt(magnitudeSquared) : fallback;
}

TrailPointRecord pointAt(uint trailIndex, TrailState state, uint capacity, uint ordinal)
{
    uint localIndex = (state.ring.x + min(ordinal, state.ring.y - 1u)) % capacity;
    return TRAIL_POINTS[trailIndex * MPP_TRAIL_MAX_POINTS + localIndex];
}

vec4 sampleTrailRow(float normalizedLife, float row)
{
    vec2 dimensions = vec2(textureSize(TRAIL_CURVE_LUT, 0));
    vec2 texel = vec2(clamp(normalizedLife, 0.0, 1.0) * (dimensions.x - 1.0) + 0.5, row + 0.5);
    return texture(TRAIL_CURVE_LUT, texel / dimensions);
}

void main()
{
    uint trailStride = MPP_TRAIL_MAX_POINTS * 2u;
    uint trailIndex = uint(gl_VertexID) / trailStride;
    uint vertexInTrail = uint(gl_VertexID) % trailStride;
    uint ordinal = vertexInTrail >> 1u;
    float sideCoordinate = float(vertexInTrail & 1u);
    TrailControlData control = TRAIL_CONTROLS[trailIndex];
    TrailState state = TRAIL_STATES[trailIndex];
    uint capacity = clamp(control.modes.w, 2u, uint(MPP_TRAIL_MAX_POINTS));
    TrailPointRecord point = pointAt(trailIndex, state, capacity, ordinal);
    TrailPointRecord previous = pointAt(trailIndex, state, capacity, ordinal > 0u ? ordinal - 1u : ordinal);
    TrailPointRecord next = pointAt(trailIndex, state, capacity, min(ordinal + 1u, state.ring.y - 1u));

    mat3 inverseViewRotation = transpose(mat3(VIEW_MATRIX));
    vec3 screenRight = safeNormal(inverseViewRotation[0], vec3(1.0, 0.0, 0.0));
    vec3 cameraPosition = inverseViewRotation * -VIEW_MATRIX[3].xyz;
    vec3 tangent = safeNormal(next.positionAge.xyz - previous.positionAge.xyz, vec3(0.0, 1.0, 0.0));
    vec3 toCamera = safeNormal(cameraPosition - point.positionAge.xyz, -inverseViewRotation[2]);
    // Cross the trail tangent with the per-point view vector: the resulting
    // width axis makes the reconstructed strip camera-facing in perspective.
    vec3 ribbonSide = safeNormal(cross(tangent, toCamera), screenRight);
    float normalizedLife = point.lifetimeDistance.x > 0.0 ?
        clamp(point.positionAge.w / point.lifetimeDistance.x, 0.0, 1.0) : 1.0;
    float row = control.appearance.z;
    float width = max(0.0, control.lifetimeDistanceUvWidth.w * sampleTrailRow(normalizedLife, row).x);
    vec3 worldPosition = point.positionAge.xyz + ribbonSide * ((sideCoordinate - 0.5) * width);

    // U is accumulated world-space arc length, not point ordinal, so changing
    // sample density does not stretch the ribbon texture. V selects an edge.
    TRAIL_UV = vec2(point.lifetimeDistance.y * control.lifetimeDistanceUvWidth.z, sideCoordinate);
    TRAIL_COLOUR = control.tintAndAlpha;
    TRAIL_COLOUR.rgb *= sampleTrailRow(normalizedLife, row + 1.0).rgb * control.appearance.x;
    TRAIL_VIEW_DEPTH = -(VIEW_MATRIX * vec4(point.positionAge.xyz, 1.0)).z;
    TRAIL_SOFT_DISTANCE = max(0.0, control.appearance.y);
    gl_Position = PROJECTION_MATRIX * VIEW_MATRIX * vec4(worldPosition, 1.0);
}
)MPP";

	inline char const* TrailDrawFragmentShader = R"MPP(#version 430

layout(std140, binding = 3) uniform CameraFrame
{
    mat4 VIEW_MATRIX;
    mat4 PROJECTION_MATRIX;
    mat4 INVERSE_PROJECTION_MATRIX;
    vec4 VIEWPORT_SIZE;
    vec4 NEAR_FAR_TIME;
};

in vec2 TRAIL_UV;
in vec4 TRAIL_COLOUR;
in float TRAIL_VIEW_DEPTH;
flat in float TRAIL_SOFT_DISTANCE;
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
    float edgeCoverage = 1.0 - smoothstep(0.85, 1.0, abs(TRAIL_UV.y * 2.0 - 1.0));
    float softFade = 1.0;
    if (HAS_SCENE_DEPTH != 0 && TRAIL_SOFT_DISTANCE > 0.0)
    {
        vec2 depthUv = gl_FragCoord.xy / max(VIEWPORT_SIZE.xy, vec2(1.0));
        float sceneDepth = texture(SCENE_DEPTH, depthUv).r;
        if (sceneDepth < 1.0)
            softFade = clamp((linearViewDepth(sceneDepth) - TRAIL_VIEW_DEPTH) / TRAIL_SOFT_DISTANCE, 0.0, 1.0);
    }
    vec4 colour = TRAIL_COLOUR;
    colour.a *= edgeCoverage * softFade;
    FRAGMENT_COLOUR = colour;
    FRAGMENT_BLOOM = vec4(colour.rgb, colour.a);
}
)MPP";
}
