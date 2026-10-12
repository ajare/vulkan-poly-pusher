#pragma once

namespace mpp
{
	inline char const* BuiltInPbrVertexShader = R"MPP(@@Version

void main()
{
    vec3 worldPosition = @Vec3(@MMatrix * @Vec4(@In(POSITION)));
    vec3 worldNormal = normalize(@NormalMatrix * @Vec3(@In(NORMAL)));
    vec3 worldTangent = normalize(@NormalMatrix * @Vec3(@In(TANGENT)));

    @Out(vec3 WORLD_POSITION) = worldPosition;
    @Out(vec3 NORMAL) = worldNormal;
    @Out(vec4 TANGENT) = vec4(worldTangent, @In(TANGENT).w);
    @Out(vec2 TEXCOORDS) = @In(TEXCOORDS);

    gl_Position = @MCPMatrix * @Vec4(@In(POSITION));
}
)MPP";
	inline char const* BuiltInPbrFragmentShader = R"MPP(@@Version

// Raw/legacy use keeps the historical complete contract. New materials receive
// an explicit define block after @@Version before parser/build.
#ifndef PBR_SPEC_LEGACY_FULL_CONTRACT
#define PBR_SPEC_LEGACY_FULL_CONTRACT 1
#define PBR_SPEC_BASE_COLOUR_MAP 1
#define PBR_SPEC_METALLIC 1
#define PBR_SPEC_ROUGHNESS 1
#define PBR_SPEC_METALLIC_ROUGHNESS_MAP 1
#define PBR_SPEC_METALLIC_MAP 0
#define PBR_SPEC_ROUGHNESS_MAP 0
#define PBR_SPEC_NORMAL_MAP 1
#define PBR_SPEC_OCCLUSION 1
#define PBR_SPEC_EMISSIVE 1
#define PBR_SPEC_EMISSIVE_MAP 1
#define PBR_SPEC_ALPHA_MASK 0
#define PBR_SPEC_ALPHA_BLEND 0
#define PBR_SPEC_DOUBLE_SIDED 0
#define PBR_SPEC_WATER 0
#endif

@@Uniform(vec4 PBR_BASE_COLOUR_FACTOR);
// Renderer-owned, not material-owned. The prefiltered specular cubemap stores
// roughness 0..1 across its whole mip chain, whose length depends on the
// authored prefilter resolution. The renderer supplies (mipLevels - 1) for the
// map actually bound to PBR_PREFILTERED_SPECULAR_MAP, so roughness maps onto
// the chain that exists rather than an assumed one.
@@Uniform(float PBR_PREFILTERED_MAX_LOD);
#if PBR_SPEC_METALLIC || PBR_SPEC_LEGACY_FULL_CONTRACT
@@Uniform(float PBR_METALLIC_FACTOR);
#endif
#if PBR_SPEC_ROUGHNESS || PBR_SPEC_LEGACY_FULL_CONTRACT
@@Uniform(float PBR_ROUGHNESS_FACTOR);
#endif
#if PBR_SPEC_EMISSIVE || PBR_SPEC_LEGACY_FULL_CONTRACT
@@Uniform(vec3 PBR_EMISSIVE_FACTOR);
#endif
#if PBR_SPEC_METALLIC_MAP
@@Uniform(int PBR_METALLIC_CHANNEL);
#endif
#if PBR_SPEC_ROUGHNESS_MAP
@@Uniform(int PBR_ROUGHNESS_CHANNEL);
#endif
#if PBR_SPEC_NORMAL_MAP || PBR_SPEC_LEGACY_FULL_CONTRACT
@@Uniform(float PBR_NORMAL_SCALE);
#endif
#if PBR_SPEC_OCCLUSION || PBR_SPEC_LEGACY_FULL_CONTRACT
@@Uniform(float PBR_OCCLUSION_STRENGTH);
#endif
#if PBR_SPEC_LEGACY_FULL_CONTRACT
@@Uniform(int PBR_ALPHA_MODE);
@@Uniform(int PBR_DOUBLE_SIDED);
#endif
#if PBR_SPEC_ALPHA_MASK || PBR_SPEC_LEGACY_FULL_CONTRACT
@@Uniform(float PBR_ALPHA_CUTOFF);
#endif
#if PBR_SPEC_WATER
// Renderer-owned, exactly like PBR_PREFILTERED_MAX_LOD: (mipLevels - 1) of the
// resolved scene colour actually bound to PBR_SCENE_COLOUR_RESOLVED, so the
// roughness blur maps onto the chain the render graph allocated rather than an
// assumed one.
@@Uniform(float PBR_SCENE_COLOUR_MAX_LOD);
// Material-owned water tuning. Ripple animation:
@@Uniform(float PBR_WATER_DISTORTION_SCALE);
@@Uniform(float PBR_WATER_DISTORTION_STRENGTH);
@@Uniform(vec2 PBR_WATER_SCROLL_SPEED);
// Reflection blur beyond the base material roughness:
@@Uniform(float PBR_WATER_MICRO_ROUGHNESS);
// Ray march tuning:
@@Uniform(float PBR_WATER_SSR_MAX_DISTANCE);
@@Uniform(int PBR_WATER_SSR_STEPS);
@@Uniform(float PBR_WATER_SSR_THICKNESS);
// Confidence falloff:
@@Uniform(float PBR_WATER_EDGE_FADE);
@@Uniform(float PBR_WATER_GRAZING_FALLBACK_START);
@@Uniform(float PBR_WATER_GRAZING_FALLBACK_END);
#endif

#if PBR_SPEC_BASE_COLOUR_MAP || PBR_SPEC_LEGACY_FULL_CONTRACT
@@Texture(sampler2D PBR_BASE_COLOUR_MAP);
#endif
#if PBR_SPEC_METALLIC_ROUGHNESS_MAP || PBR_SPEC_LEGACY_FULL_CONTRACT
@@Texture(sampler2D PBR_METALLIC_ROUGHNESS_MAP);
#endif
#if PBR_SPEC_METALLIC_MAP
@@Texture(sampler2D PBR_METALLIC_MAP);
#endif
#if PBR_SPEC_ROUGHNESS_MAP
@@Texture(sampler2D PBR_ROUGHNESS_MAP);
#endif
#if PBR_SPEC_NORMAL_MAP || PBR_SPEC_LEGACY_FULL_CONTRACT
@@Texture(sampler2D PBR_NORMAL_MAP);
#endif
#if PBR_SPEC_OCCLUSION || PBR_SPEC_LEGACY_FULL_CONTRACT
@@Texture(sampler2D PBR_OCCLUSION_MAP);
#endif
#if PBR_SPEC_EMISSIVE_MAP || PBR_SPEC_LEGACY_FULL_CONTRACT
@@Texture(sampler2D PBR_EMISSIVE_MAP);
#endif
@@Texture(samplerCube PBR_IRRADIANCE_MAP);
@@Texture(samplerCube PBR_PREFILTERED_SPECULAR_MAP);
@@Texture(sampler2D PBR_BRDF_LUT);
@@Texture(sampler2DShadow SHADOW_MAP);
@@Texture(samplerCubeShadow POINT_SHADOW_MAP);
#if PBR_SPEC_WATER
// Renderer-bound by the water graph pass, never material-authored: the frozen,
// mip-chained copy of the opaque scene colour, and the opaque depth buffer the
// ray march tests against. SceneDepth is a plain sampler2D here rather than the
// comparison sampler SHADOW_MAP uses -- the march needs the depth value, not a
// shadow test.
@@Texture(sampler2D PBR_SCENE_COLOUR_RESOLVED);
@@Texture(sampler2D PBR_SCENE_DEPTH);
// Material-authored ripple octaves. Both default to the neutral flat normal, so
// an untextured water material is a clean mirror rather than a compile error.
@@Texture(sampler2D PBR_WATER_NORMAL_MAP);
@@Texture(sampler2D PBR_WATER_DETAIL_NORMAL_MAP);
#endif

// Per-frame camera state is shared by water's view-space ray march and the
// pipeline-owned view-space shading-normal output.
layout(std140, binding = 3) uniform CameraFrame
{
    mat4 VIEW_MATRIX;
    mat4 PROJECTION_MATRIX;
    mat4 INVERSE_PROJECTION_MATRIX;
    vec4 VIEWPORT_SIZE;    // width, height, 1/width, 1/height
    vec4 NEAR_FAR_TIME;    // near, far, seconds, unused
};

layout(std140, binding = 2) uniform ShadowFrame
{
    mat4 LIGHT_VIEW_PROJECTION;
    vec4 MAP_TEXEL_SIZE_AND_RADIUS;
    vec4 BIAS_AND_ENABLED;
    vec4 POINT_POSITION_AND_RANGE;
    vec4 SHADOW_TYPE_AND_LIGHT_INDEX;
};

struct PbrLight
{
    vec4 colourIntensity;
    vec4 positionRange;
    vec4 directionType;
};

layout(std140, binding = 1) uniform PbrLights
{
    vec4 AMBIENT_AND_COUNT;
    PbrLight LIGHTS[8];
};

const float PI = 3.14159265359;

float distributionGgx(vec3 normal, vec3 halfway, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float nDotH = max(dot(normal, halfway), 0.0);
    float nDotH2 = nDotH * nDotH;
    float denominator = nDotH2 * (a2 - 1.0) + 1.0;
    return a2 / max(PI * denominator * denominator, 0.000001);
}

vec2 encodeOctahedralNormal(vec3 normal)
{
    normal /= abs(normal.x) + abs(normal.y) + abs(normal.z);
    vec2 oct = normal.xy;
    if (normal.z < 0.0) oct = (1.0 - abs(oct.yx)) * sign(oct.xy);
    return oct * 0.5 + 0.5;
}

float geometrySchlickGgx(float nDotV, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return nDotV / max(nDotV * (1.0 - k) + k, 0.000001);
}

float geometrySmith(vec3 normal, vec3 viewDirection, vec3 lightDirection, float roughness)
{
    return geometrySchlickGgx(max(dot(normal, viewDirection), 0.0), roughness) *
        geometrySchlickGgx(max(dot(normal, lightDirection), 0.0), roughness);
}

vec3 fresnelSchlick(float cosine, vec3 f0)
{
    return f0 + (1.0 - f0) * pow(1.0 - cosine, 5.0);
}

vec3 fresnelSchlickRoughness(float cosine, vec3 f0, float roughness)
{
    return f0 + (max(vec3(1.0 - roughness), f0) - f0) * pow(1.0 - cosine, 5.0);
}

float pointShadowVisibility(vec3 worldPosition, vec3 normal, vec3 lightDirection)
{
    vec3 lightToFragment = worldPosition - POINT_POSITION_AND_RANGE.xyz;
    float range = POINT_POSITION_AND_RANGE.w;
    float distanceToLight = length(lightToFragment);
    if (distanceToLight >= range) return 1.0;
    float nDotL = max(dot(normal, lightDirection), 0.0);
    float bias = BIAS_AND_ENABLED.x + BIAS_AND_ENABLED.y * (1.0 - nDotL);
    float compareDepth = distanceToLight / range - bias;
    float visibility;
    if (MAP_TEXEL_SIZE_AND_RADIUS.w < 0.5)
    {
        visibility = texture(@Texture(POINT_SHADOW_MAP), vec4(lightToFragment, compareDepth));
    }
    else
    {
        // Sampling offset directions lets the cubemap resolve taps across a
        // face edge instead of clamping them in one projected face.
        vec3 direction = lightToFragment / max(distanceToLight, 0.00001);
        vec3 reference = abs(direction.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(0.0, 1.0, 0.0);
        vec3 tangent = normalize(cross(reference, direction));
        vec3 bitangent = cross(direction, tangent);
        float radius = 2.0 * MAP_TEXEL_SIZE_AND_RADIUS.x * MAP_TEXEL_SIZE_AND_RADIUS.z;
        visibility = 0.0;
        for (int y = -1; y <= 1; ++y)
        {
            for (int x = -1; x <= 1; ++x)
            {
                vec3 tapDirection = normalize(direction + tangent * (float(x) * radius) + bitangent * (float(y) * radius));
                visibility += texture(@Texture(POINT_SHADOW_MAP), vec4(tapDirection, compareDepth));
            }
        }
        visibility /= 9.0;
    }

    float fade = clamp((distanceToLight / range - BIAS_AND_ENABLED.w) /
        max(1.0 - BIAS_AND_ENABLED.w, 0.00001), 0.0, 1.0);
    fade = fade * fade * (3.0 - 2.0 * fade);
    return mix(visibility, 1.0, fade);
}

float directionalShadowVisibility(vec3 worldPosition, vec3 normal, vec3 lightDirection)
{
    if (BIAS_AND_ENABLED.z < 0.5)
    {
        return 1.0;
    }

    vec4 shadowPosition = LIGHT_VIEW_PROJECTION * vec4(worldPosition, 1.0);
    shadowPosition.xyz /= max(shadowPosition.w, 0.00001);
    vec3 shadowCoords = shadowPosition.xyz * 0.5 + 0.5;
    if (shadowCoords.x <= 0.0 || shadowCoords.x >= 1.0 || shadowCoords.y <= 0.0 || shadowCoords.y >= 1.0 ||
        shadowCoords.z <= 0.0 || shadowCoords.z >= 1.0)
    {
        return 1.0;
    }

    float nDotL = max(dot(normal, lightDirection), 0.0);
    float bias = BIAS_AND_ENABLED.x + BIAS_AND_ENABLED.y * (1.0 - nDotL);
    vec3 compareCoords = vec3(shadowCoords.xy, shadowCoords.z - bias);
    if (MAP_TEXEL_SIZE_AND_RADIUS.w < 0.5)
    {
        return texture(@Texture(SHADOW_MAP), compareCoords);
    }

    float visibility = 0.0;
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            vec2 offset = vec2(float(x), float(y)) * MAP_TEXEL_SIZE_AND_RADIUS.xy * MAP_TEXEL_SIZE_AND_RADIUS.z;
            visibility += texture(@Texture(SHADOW_MAP), vec3(compareCoords.xy + offset, compareCoords.z));
        }
    }
    return visibility / 9.0;
}

#if PBR_SPEC_WATER
// View-space Z (negative away from the camera) for a depth-buffer sample.
// Unprojecting through INVERSE_PROJECTION_MATRIX rather than a near/far formula
// keeps this correct for whatever projection the camera supplied, including the
// TAA-jittered one, since that is the matrix the scene was rasterized with.
float waterViewDepth(vec2 uv, float depth)
{
    vec4 view = INVERSE_PROJECTION_MATRIX * vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    return view.z / view.w;
}

// Point-samples the depth buffer. Filtering depth is meaningless and actively
// harmful here: across a silhouette a linear tap returns the blend of the
// occluder and whatever is behind it, which is a depth no surface actually
// occupies. The march then finds a perfect crossing somewhere on that phantom
// ramp for every ray that passes near an edge, hanging a skirt of full-confidence
// reflection below every object. texelFetch also makes this independent of how a
// pipeline author declared the depth image's filters, and clamping to the bound
// texture's own size keeps it correct for the 1x1 neutral fallback.
float waterSceneDepth(vec2 uv)
{
    ivec2 size = textureSize(@Texture(PBR_SCENE_DEPTH), 0);
    ivec2 texel = clamp(ivec2(uv * vec2(size)), ivec2(0), size - ivec2(1));
    return texelFetch(@Texture(PBR_SCENE_DEPTH), texel, 0).r;
}

vec2 waterProject(vec3 viewPosition, out float valid)
{
    vec4 clip = PROJECTION_MATRIX * vec4(viewPosition, 1.0);
    valid = clip.w > 0.0001 ? 1.0 : 0.0;
    return (clip.xy / max(clip.w, 0.0001)) * 0.5 + 0.5;
}

// How much the depth buffer agrees with itself around a hit. A hit that lands on
// a depth discontinuity is one the buffer cannot vouch for: from a front-face-only
// depth image, "landed on the occluder" and "passed just outside it" look
// identical there, and neighbouring rays resolve the ambiguity differently. That
// is what fringes the edge of every reflected object with spikes. Fading those
// toward the cubemap costs a couple of pixels of rim on a genuine reflection and
// removes the fringe.
float waterHitStability(vec2 uv, float hitZ, float thickness)
{
    ivec2 size = textureSize(@Texture(PBR_SCENE_DEPTH), 0);
    ivec2 texel = clamp(ivec2(uv * vec2(size)), ivec2(1), max(size - ivec2(2), ivec2(1)));
    float left = waterViewDepth(uv, texelFetch(@Texture(PBR_SCENE_DEPTH), texel + ivec2(-1, 0), 0).r);
    float right = waterViewDepth(uv, texelFetch(@Texture(PBR_SCENE_DEPTH), texel + ivec2(1, 0), 0).r);
    float down = waterViewDepth(uv, texelFetch(@Texture(PBR_SCENE_DEPTH), texel + ivec2(0, -1), 0).r);
    float up = waterViewDepth(uv, texelFetch(@Texture(PBR_SCENE_DEPTH), texel + ivec2(0, 1), 0).r);
    float spread = max(max(abs(left - hitZ), abs(right - hitZ)), max(abs(down - hitZ), abs(up - hitZ)));
    return 1.0 - smoothstep(thickness, thickness * 3.0, spread);
}

// Interleaved-gradient noise: a cheap per-pixel value in [0,1) with no time term,
// so it dithers without shimmering between frames.
float waterDither(vec2 pixel)
{
    return fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715))));
}

// Fixed-step view-space march against the opaque depth buffer, refined by a
// short binary search on the crossing interval. Returns the confidence in the hit
// it wrote to hitUv: 1 where the ray lands cleanly on a surface, falling to 0 as
// the crossing turns into a pass behind a silhouette, and 0 outright on a miss, a
// step that leaves the viewport, or a ray that crosses behind the near plane.
// Every one of those is a fallback case, so the caller never distinguishes them.
//
// The coarse loop only looks for a sign change -- the step is far too long to
// carry a depth tolerance, and demanding one there is what makes a march either
// tunnel through everything (tolerance below the step) or smear silhouettes
// (tolerance above it). The thickness test belongs after refinement, where the
// two candidates it has to separate finally look different; see below.
float waterMarch(vec3 origin, vec3 direction, out vec2 hitUv)
{
    hitUv = vec2(0.0);
    int steps = clamp(@Uniform(PBR_WATER_SSR_STEPS), 1, 128);
    float stepSize = max(@Uniform(PBR_WATER_SSR_MAX_DISTANCE), 0.0001) / float(steps);
    float thickness = max(@Uniform(PBR_WATER_SSR_THICKNESS), 0.0001);
    // Blended water writes no depth, so the buffer under its own pixels holds
    // whatever is beneath it -- the pool floor, not the water. A ray that starts
    // out already behind that surface has not hit it; it has to come back in
    // front before a crossing means anything.
    float originValid;
    vec2 originUv = waterProject(origin, originValid);
    bool armed = origin.z >= waterViewDepth(originUv, waterSceneDepth(originUv));
    // Offsetting each pixel's sample lattice within one step is what keeps the
    // edge of a hit region from combing. Without it, whether a ray lands a sample
    // inside a silhouette depends on where its fixed lattice happens to fall, and
    // because neighbouring rays slide that lattice smoothly across the edge the
    // result is a regular fringe of hairs rather than noise. Dithered, the same
    // error becomes per-pixel and the roughness blur absorbs it.
    float jitter = waterDither(gl_FragCoord.xy);
    vec3 previous = origin;
    for (int i = 0; i < 128; ++i)
    {
        if (i >= steps) break;
        vec3 marchPoint = origin + direction * (stepSize * (float(i) + jitter));
        float valid;
        vec2 uv = waterProject(marchPoint, valid);
        if (valid < 0.5) break;
        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) break;
        float sceneZ = waterViewDepth(uv, waterSceneDepth(uv));
        // View Z is negative away from the camera, so the ray has passed behind
        // the depth buffer once its Z is the more negative of the two.
        bool behind = marchPoint.z < sceneZ;
        if (!behind) armed = true;
        else if (armed)
        {
            vec3 closer = previous;
            vec3 further = marchPoint;
            for (int refine = 0; refine < 5; ++refine)
            {
                vec3 middle = (closer + further) * 0.5;
                float middleValid;
                vec2 middleUv = waterProject(middle, middleValid);
                float middleSceneZ = waterViewDepth(middleUv, waterSceneDepth(middleUv));
                if (middle.z < middleSceneZ) { further = middle; uv = middleUv; } else { closer = middle; }
            }
            // Landing on a surface and passing behind its silhouette are the same
            // sign change, and only the refined point tells them apart. A real hit
            // converges to where the ray meets the surface, so the two depths agree;
            // a near-miss converges to the silhouette edge, where the ray is still
            // however far behind the occluder it was always going to pass. Without
            // this the occluder gets extruded along the ray and every sphere
            // reflects as a column.
            //
            // Fading over the last half of the tolerance rather than cutting at it
            // matters: the two cases meet along the silhouette, where a hard test
            // alternates per pixel and combs the edge of every reflection.
            float hitSceneZ = waterViewDepth(uv, waterSceneDepth(uv));
            float confidence = (1.0 - smoothstep(thickness * 0.5, thickness, hitSceneZ - further.z)) *
                waterHitStability(uv, hitSceneZ, thickness);
            if (confidence > 0.0)
            {
                hitUv = uv;
                return confidence;
            }
            // Not a landing, just the ray passing behind this surface. The depth
            // buffer only knows front faces, so it cannot say anything is really
            // there -- keep marching instead of treating every foreground silhouette
            // as an opaque wall. Stopping here leaves comb-like gaps under a
            // reflection wherever neighbouring rays clear the occluder and this one
            // does not. Disarming makes the next crossing bracket a genuine
            // front-to-behind pair again once the ray is back in front.
            armed = false;
        }
        previous = marchPoint;
    }
    return 0.0;
}
#endif

void main()
{
#if PBR_SPEC_BASE_COLOUR_MAP || PBR_SPEC_LEGACY_FULL_CONTRACT
    vec4 baseColour = texture(@Texture(PBR_BASE_COLOUR_MAP), @In(TEXCOORDS)) * @Uniform(PBR_BASE_COLOUR_FACTOR);
#else
    vec4 baseColour = @Uniform(PBR_BASE_COLOUR_FACTOR);
#endif
#if PBR_SPEC_LEGACY_FULL_CONTRACT
    if (@Uniform(PBR_ALPHA_MODE) == 1 && baseColour.a < @Uniform(PBR_ALPHA_CUTOFF)) discard;
#elif PBR_SPEC_ALPHA_MASK
    if (baseColour.a < @Uniform(PBR_ALPHA_CUTOFF)) discard;
#endif

    vec3 normal = normalize(@In(NORMAL));
#if PBR_SPEC_NORMAL_MAP || PBR_SPEC_LEGACY_FULL_CONTRACT
    vec4 tangentInput = @In(TANGENT);
    vec3 tangent = normalize(tangentInput.xyz - normal * dot(normal, tangentInput.xyz));
    vec3 bitangent = normalize(cross(normal, tangent)) * tangentInput.w;
    vec3 normalSample = texture(@Texture(PBR_NORMAL_MAP), @In(TEXCOORDS)).xyz * 2.0 - 1.0;
    normalSample.xy *= @Uniform(PBR_NORMAL_SCALE);
    normal = normalize(mat3(tangent, bitangent, normal) * normalSample);
#endif
#if PBR_SPEC_LEGACY_FULL_CONTRACT
    if (@Uniform(PBR_DOUBLE_SIDED) != 0 && !gl_FrontFacing) normal = -normal;
#elif PBR_SPEC_DOUBLE_SIDED
    if (!gl_FrontFacing) normal = -normal;
#endif
#if PBR_SPEC_WATER
    // Water rejects occluded fragments against the opaque depth buffer it already
    // samples, rather than through a depth attachment. The render graph gives each
    // version of an image its own physical target, so a pass cannot both sample a
    // depth buffer and attach it: it would test against an uninitialized copy, and
    // attaching what it samples is a feedback loop besides. An unbound depth
    // sampler reads the far plane, so this never discards by accident.
    vec2 waterScreenUv = gl_FragCoord.xy * VIEWPORT_SIZE.zw;
    if (gl_FragCoord.z > waterSceneDepth(waterScreenUv)) discard;
    {
        // Two octaves of scrolling normals at different scale and direction. One
        // tiling layer reads as a moving texture; two beating against each other
        // read as water. This is what stops a clean SSR hit looking like a mirror.
        vec4 waterTangentInput = @In(TANGENT);
        vec3 waterTangent = normalize(waterTangentInput.xyz - normal * dot(normal, waterTangentInput.xyz));
        vec3 waterBitangent = normalize(cross(normal, waterTangent)) * waterTangentInput.w;
        float waterScale = max(@Uniform(PBR_WATER_DISTORTION_SCALE), 0.0001);
        vec2 waterScroll = @Uniform(PBR_WATER_SCROLL_SPEED) * NEAR_FAR_TIME.z;
        vec3 rippleA = texture(@Texture(PBR_WATER_NORMAL_MAP), @In(TEXCOORDS) * waterScale + waterScroll).xyz * 2.0 - 1.0;
        vec3 rippleB = texture(@Texture(PBR_WATER_DETAIL_NORMAL_MAP), @In(TEXCOORDS) * waterScale * 2.17 - waterScroll * 1.6).xyz * 2.0 - 1.0;
        vec3 ripple = rippleA + rippleB;
        // Scaling only the tangential components is the same convention as
        // PBR_NORMAL_SCALE, and it makes strength 0 exactly the undistorted
        // surface -- the regression guard the SSR/cubemap blend is checked against.
        ripple.xy *= @Uniform(PBR_WATER_DISTORTION_STRENGTH);
        normal = normalize(mat3(waterTangent, waterBitangent, normal) * normalize(ripple));
    }
#endif

#if PBR_SPEC_METALLIC_ROUGHNESS_MAP || PBR_SPEC_LEGACY_FULL_CONTRACT
    vec3 metallicRoughness = texture(@Texture(PBR_METALLIC_ROUGHNESS_MAP), @In(TEXCOORDS)).rgb;
#endif
#if PBR_SPEC_LEGACY_FULL_CONTRACT
    float metallic = clamp(metallicRoughness.b * @Uniform(PBR_METALLIC_FACTOR), 0.0, 1.0);
    float roughness = clamp(metallicRoughness.g * @Uniform(PBR_ROUGHNESS_FACTOR), 0.04, 1.0);
#elif PBR_SPEC_METALLIC
#if PBR_SPEC_METALLIC_MAP
    float metallic = clamp(texture(@Texture(PBR_METALLIC_MAP), @In(TEXCOORDS))[clamp(@Uniform(PBR_METALLIC_CHANNEL), 0, 3)] * @Uniform(PBR_METALLIC_FACTOR), 0.0, 1.0);
#elif PBR_SPEC_METALLIC_ROUGHNESS_MAP
    float metallic = clamp(metallicRoughness.b * @Uniform(PBR_METALLIC_FACTOR), 0.0, 1.0);
#else
    float metallic = clamp(@Uniform(PBR_METALLIC_FACTOR), 0.0, 1.0);
#endif
#else
    const float metallic = 0.0;
#endif
#if !PBR_SPEC_LEGACY_FULL_CONTRACT
#if PBR_SPEC_ROUGHNESS
#if PBR_SPEC_ROUGHNESS_MAP
    float roughness = clamp(texture(@Texture(PBR_ROUGHNESS_MAP), @In(TEXCOORDS))[clamp(@Uniform(PBR_ROUGHNESS_CHANNEL), 0, 3)] * @Uniform(PBR_ROUGHNESS_FACTOR), 0.04, 1.0);
#elif PBR_SPEC_METALLIC_ROUGHNESS_MAP
    float roughness = clamp(metallicRoughness.g * @Uniform(PBR_ROUGHNESS_FACTOR), 0.04, 1.0);
#else
    float roughness = clamp(@Uniform(PBR_ROUGHNESS_FACTOR), 0.04, 1.0);
#endif
#else
    const float roughness = 0.04;
#endif
#endif
#if PBR_SPEC_OCCLUSION || PBR_SPEC_LEGACY_FULL_CONTRACT
    float occlusion = mix(1.0, texture(@Texture(PBR_OCCLUSION_MAP), @In(TEXCOORDS)).r, @Uniform(PBR_OCCLUSION_STRENGTH));
#else
    const float occlusion = 1.0;
#endif
#if PBR_SPEC_EMISSIVE || PBR_SPEC_LEGACY_FULL_CONTRACT
#if PBR_SPEC_EMISSIVE_MAP || PBR_SPEC_LEGACY_FULL_CONTRACT
    vec3 emissive = texture(@Texture(PBR_EMISSIVE_MAP), @In(TEXCOORDS)).rgb * @Uniform(PBR_EMISSIVE_FACTOR);
#else
    vec3 emissive = @Uniform(PBR_EMISSIVE_FACTOR);
#endif
#else
    const vec3 emissive = vec3(0.0);
#endif

    vec3 viewDirection = normalize(@ViewPos - @In(WORLD_POSITION));
    vec3 f0 = mix(vec3(0.04), baseColour.rgb, metallic);
    vec3 direct = vec3(0.0);
    int lightCount = int(AMBIENT_AND_COUNT.a);
    for (int i = 0; i < lightCount; ++i)
    {
        PbrLight light = LIGHTS[i];
        float isPoint = light.directionType.w;
        vec3 toLight = light.positionRange.xyz - @In(WORLD_POSITION);
        float distanceToLight = max(length(toLight), 0.0001);
        vec3 pointDirection = toLight / distanceToLight;
        vec3 directionalDirection = normalize(-light.directionType.xyz);
        vec3 lightDirection = mix(directionalDirection, pointDirection, isPoint);
        float attenuation = mix(1.0, 1.0 / (distanceToLight * distanceToLight), isPoint);
        if (isPoint > 0.5 && light.positionRange.w > 0.0 && distanceToLight > light.positionRange.w)
        {
            attenuation = 0.0;
        }

        vec3 halfway = normalize(viewDirection + lightDirection);
        float ndf = distributionGgx(normal, halfway, roughness);
        float geometry = geometrySmith(normal, viewDirection, lightDirection, roughness);
        vec3 fresnel = fresnelSchlick(max(dot(halfway, viewDirection), 0.0), f0);
        vec3 specular = ndf * geometry * fresnel /
            max(4.0 * max(dot(normal, viewDirection), 0.0) * max(dot(normal, lightDirection), 0.0), 0.0001);
        vec3 kS = fresnel;
        vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);
        float nDotL = max(dot(normal, lightDirection), 0.0);
        vec3 radiance = light.colourIntensity.rgb * light.colourIntensity.a * attenuation;
        float shadow = 1.0;
        if (BIAS_AND_ENABLED.z > 0.5 && i == int(SHADOW_TYPE_AND_LIGHT_INDEX.y))
            shadow = SHADOW_TYPE_AND_LIGHT_INDEX.x > 0.5
                ? pointShadowVisibility(@In(WORLD_POSITION), normal, lightDirection)
                : directionalShadowVisibility(@In(WORLD_POSITION), normal, lightDirection);
        direct += (kD * baseColour.rgb / PI + specular) * radiance * nDotL * shadow;
    }

    float nDotV = max(dot(normal, viewDirection), 0.0);
    vec3 fresnel = fresnelSchlickRoughness(nDotV, f0, roughness);
    vec3 kS = fresnel;
    vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);
    vec3 irradiance = texture(@Texture(PBR_IRRADIANCE_MAP), normal).rgb;
    vec3 diffuse = irradiance * baseColour.rgb;
    vec3 reflection = reflect(-viewDirection, normal);
    vec3 prefiltered = textureLod(@Texture(PBR_PREFILTERED_SPECULAR_MAP), reflection,
        roughness * @Uniform(PBR_PREFILTERED_MAX_LOD)).rgb;
#if PBR_SPEC_WATER
    // SSR replaces the cubemap sample only where the march is trustworthy. The
    // cubemap sample above is the fallback, and it already used the distorted
    // reflection vector, so the two sources agree wherever they are mixed and
    // confidence 0 reproduces the ordinary IBL specular path exactly.
    vec3 waterViewPosition = vec3(VIEW_MATRIX * vec4(@In(WORLD_POSITION), 1.0));
    vec3 waterViewNormal = normalize(mat3(VIEW_MATRIX) * normal);
    vec3 waterViewReflection = normalize(reflect(normalize(waterViewPosition), waterViewNormal));
    vec2 waterHitUv;
    float waterConfidence = waterMarch(waterViewPosition, waterViewReflection, waterHitUv);
    // A hit near the viewport border is about to leave the screen; popping there
    // is the most visible SSR artifact, so fade it out over an authored margin.
    vec2 waterEdgeDistance = min(waterHitUv, vec2(1.0) - waterHitUv);
    waterConfidence *= clamp(min(waterEdgeDistance.x, waterEdgeDistance.y) /
        max(@Uniform(PBR_WATER_EDGE_FADE), 0.0001), 0.0, 1.0);
    // Grazing angles march nearly parallel to the screen: the reflection stretches
    // badly and usually has no scene data to hit. Hand those to the cubemap.
    float waterGrazingStart = @Uniform(PBR_WATER_GRAZING_FALLBACK_START);
    float waterGrazingEnd = min(@Uniform(PBR_WATER_GRAZING_FALLBACK_END), waterGrazingStart - 0.0001);
    waterConfidence *= smoothstep(waterGrazingEnd, waterGrazingStart, nDotV);
    float waterMaxLod = max(@Uniform(PBR_SCENE_COLOUR_MAX_LOD), 0.0);
    float waterLod = clamp((roughness + @Uniform(PBR_WATER_MICRO_ROUGHNESS)) * waterMaxLod, 0.0, waterMaxLod);
    vec3 waterSsr = textureLod(@Texture(PBR_SCENE_COLOUR_RESOLVED), waterHitUv, waterLod).rgb;
    prefiltered = mix(prefiltered, waterSsr, clamp(waterConfidence, 0.0, 1.0));
#endif
    vec2 brdf = texture(@Texture(PBR_BRDF_LUT), vec2(nDotV, roughness)).rg;
    vec3 specular = prefiltered * (fresnel * brdf.x + brdf.y);
    vec3 ambient = (kD * diffuse + specular) * occlusion + AMBIENT_AND_COUNT.rgb * baseColour.rgb;

    // Opaque and masked materials must not inherit an undefined/blended
    // framebuffer alpha. Only Blend materials expose the authored alpha.
#if PBR_SPEC_LEGACY_FULL_CONTRACT
    float outputAlpha = @Uniform(PBR_ALPHA_MODE) == 2 ? baseColour.a : 1.0;
#elif PBR_SPEC_ALPHA_BLEND
    float outputAlpha = baseColour.a;
#else
    const float outputAlpha = 1.0;
#endif
    @Out(vec4 COLOUR) = vec4(ambient + direct + emissive, outputAlpha);
    // Location 1 is ignored by the single-target manual PBR path. GraphPBR
    // attaches it as an HDR bloom mask, so authored emissive is isolated from
    // otherwise bright direct/albedo lighting.
    @Out(vec4 BLOOM_MASK) = vec4(emissive, 1.0);
    // Final material normal, after normal maps, double-sided handling, and water
    // ripples. CameraFrame converts world space to GTAO's view-space contract.
    @Out(vec2 SHADING_NORMAL) = encodeOctahedralNormal(normalize(mat3(VIEW_MATRIX) * normal));
}
)MPP";
}
