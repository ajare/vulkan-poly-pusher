#pragma once

#include <string>

/*
 * Default 3d program.
 *
 */
const std::string VertexShader3dTemplate =
R"(
@@Version

void main()
{
    @Out(vec3 FRAGPOSITION) = @Vec3(@MMatrix * @Vec4(@In(POSITION)));
    @Out(vec3 NORMAL) = normalize(@NormalMatrix * @Vec3(@In(NORMAL)));
    @Out(vec2 TEXCOORDS) = @In(TEXCOORDS);
    @Out(vec4 COLOUR) = @In(COLOUR);

    gl_Position = @MCPMatrix * @Vec4(@In(POSITION));
}
)";

const std::string FragmentShader3dTemplate =
R"(
@@Version

@@Uniform(float GAMMA);
## Texture
@@Texture(sampler2D TEX1);
##
@@Texture(sampler2DShadow SHADOW_MAP);
@@Texture(samplerCubeShadow POINT_SHADOW_MAP);

layout(std140, binding = 2) uniform ShadowFrame
{
    mat4 LIGHT_VIEW_PROJECTION;
    vec4 MAP_TEXEL_SIZE_AND_RADIUS;
    vec4 BIAS_AND_ENABLED;
    vec4 POINT_POSITION_AND_RANGE;
    vec4 SHADOW_TYPE_AND_LIGHT_INDEX;
};

layout(std140, binding = 3) uniform CameraFrame
{
    mat4 VIEW_MATRIX;
    mat4 PROJECTION_MATRIX;
    mat4 INVERSE_PROJECTION_MATRIX;
    vec4 VIEWPORT_SIZE;
    vec4 NEAR_FAR_TIME;
};

vec2 encodeOctahedralNormal(vec3 normal)
{
    normal /= abs(normal.x) + abs(normal.y) + abs(normal.z);
    vec2 oct = normal.xy;
    if (normal.z < 0.0) oct = (1.0 - abs(oct.yx)) * sign(oct.xy);
    return oct * 0.5 + 0.5;
}

struct Light
{
    vec3 position;
    vec3 colour;
};

layout(std140, binding = 0) uniform Block
{
	@@Uniform(vec3 AMBIENT);
	@@Uniform(Light LIGHTS[2]);
	@@Uniform(int NUM_LIGHTS);
};

float lambert(vec3 n, vec3 l)
{
    float result = dot(n, l);
    return max(result, 0.0);
}

float phong(vec3 v, vec3 n, vec3 l)
{
    float strength = 0.5;
    float exponent = 32;

    vec3 r = reflect(-l, n);
    float spec = pow(max(dot(v, r), 0.0), exponent);
    return strength * spec;
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
        // Offset the lookup direction, not a projected cube face. Cube sampling
        // resolves each tap across face boundaries, and the tangent basis keeps
        // the texel-radius kernel in world-direction space.
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

void main()
{
    vec3 normalDir = @In(NORMAL);
    vec3 viewDir = normalize(@ViewPos - @In(FRAGPOSITION));

    vec3 colourContrib = @Uniform(AMBIENT);

    for (int i = 0; i < @Uniform(NUM_LIGHTS); i++)
    {
        vec3 lightDir = normalize(@Uniform(LIGHTS[i]).position - @In(FRAGPOSITION));

        // Lighting model
        float diffuse = lambert(normalDir, lightDir);
        float specular = phong(viewDir, normalDir, lightDir);
        
        float shadow = 1.0;
        if (BIAS_AND_ENABLED.z > 0.5 && i == int(SHADOW_TYPE_AND_LIGHT_INDEX.y))
            shadow = SHADOW_TYPE_AND_LIGHT_INDEX.x > 0.5
                ? pointShadowVisibility(@In(FRAGPOSITION), normalDir, lightDir)
                : directionalShadowVisibility(@In(FRAGPOSITION), normalDir, lightDir);
        colourContrib += @Uniform(LIGHTS[i]).colour * (diffuse + specular) * shadow;
    }

    vec4 shadedColour = vec4(colourContrib, 1.0) * @In(COLOUR);

## Texture
    @Out(vec4 COLOUR) = texture(@Texture(TEX1), @In(TEXCOORDS).xy) * shadedColour;
## Else
    @Out(vec4 COLOUR) = shadedColour;
##
	@Out(COLOUR).rgb = pow(@Out(COLOUR).rgb, vec3(1.0 / @Uniform(GAMMA)));
    // Location 1 is reserved for the PBR emissive mask. Legacy materials have
    // no emissive term, but retain the stable forward-pipeline MRT contract.
    @Out(vec4 BLOOM_MASK) = vec4(0.0);
    @Out(vec2 SHADING_NORMAL) = encodeOctahedralNormal(normalize(mat3(VIEW_MATRIX) * normalDir));
}
)";

/*
 * Fullscreen 2d shader.
 *
 */
const std::string VertexShaderFullscreenTemplate =
R"(
@@Version

void main()
{
	vec4 transVertex = @MCPMatrix * @Vec4(@In(POSITION));
	vec2 centredPos = vec2(transVertex.x - @HalfWindowSize.x, transVertex.y - @HalfWindowSize.y);

	@Out(vec2 TEXCOORDS) = @In(TEXCOORDS);
	gl_Position = vec4(centredPos / @HalfWindowSize, 0, 1);
}
)";

// Don't apply gamma correction as this is used to render framebuffers which have
// already been gamma-corrected.
// Depth-only programs used by generic shadow domains. They depend only on
// position, so meshes from PBR and non-PBR materials share the same caster path.
const std::string VertexShaderShadowDepthTemplate =
R"(
@@Version

void main()
{
    gl_Position = @MCPMatrix * @Vec4(@In(POSITION));
}
)";

const std::string FragmentShaderShadowDepthTemplate =
R"(
@@Version

void main()
{
}
)";

const std::string VertexShaderAlphaShadowDepthTemplate =
R"(
@@Version

void main()
{
    @Out(vec2 SHADOW_TEXCOORDS) = @In(TEXCOORDS);
    gl_Position = @MCPMatrix * @Vec4(@In(POSITION));
}
)";

const std::string FragmentShaderAlphaShadowDepthTemplate =
R"(
@@Version

@@Uniform(float SHADOW_ALPHA_CUTOFF);
@@Uniform(float SHADOW_ALPHA_FACTOR);
@@Texture(sampler2D SHADOW_ALPHA_MAP);

void main()
{
    if (texture(@Texture(SHADOW_ALPHA_MAP), @In(SHADOW_TEXCOORDS)).a * @Uniform(SHADOW_ALPHA_FACTOR) < @Uniform(SHADOW_ALPHA_CUTOFF)) discard;
}
)";

const std::string VertexShaderPointShadowDepthTemplate =
R"(
@@Version

void main()
{
    @Out(vec3 SHADOW_WORLD_POSITION) = @Vec3(@MMatrix * @Vec4(@In(POSITION)));
    gl_Position = @MCPMatrix * @Vec4(@In(POSITION));
}
)";

const std::string FragmentShaderPointShadowDepthTemplate =
R"(
@@Version

layout(std140, binding = 2) uniform ShadowFrame
{
    mat4 LIGHT_VIEW_PROJECTION;
    vec4 MAP_TEXEL_SIZE_AND_RADIUS;
    vec4 BIAS_AND_ENABLED;
    vec4 POINT_POSITION_AND_RANGE;
    vec4 SHADOW_TYPE_AND_LIGHT_INDEX;
};

@@Uniform(int POINT_SHADOW_CASTER_CLIP_ENABLED);
@@Uniform(vec4 POINT_SHADOW_RETAINED_PLANE);
@@Uniform(vec4 POINT_SHADOW_OPENING_CENTRE_TOLERANCE);
@@Uniform(vec4 POINT_SHADOW_OPENING_TANGENT_HALF_WIDTH);
@@Uniform(vec4 POINT_SHADOW_OPENING_BITANGENT_HALF_HEIGHT);

void applyPointShadowCasterClip(vec3 worldPosition)
{
    if (@Uniform(POINT_SHADOW_CASTER_CLIP_ENABLED) == 0) return;
    vec4 plane = @Uniform(POINT_SHADOW_RETAINED_PLANE);
    float planeDistance = dot(plane.xyz, worldPosition) + plane.w;
    float tolerance = @Uniform(POINT_SHADOW_OPENING_CENTRE_TOLERANCE).w;
    if (planeDistance < -tolerance) discard;
    vec3 openingOffset = worldPosition -
        @Uniform(POINT_SHADOW_OPENING_CENTRE_TOLERANCE).xyz;
    if (abs(planeDistance) <= tolerance &&
        abs(dot(openingOffset,
            @Uniform(POINT_SHADOW_OPENING_TANGENT_HALF_WIDTH).xyz)) <=
            @Uniform(POINT_SHADOW_OPENING_TANGENT_HALF_WIDTH).w &&
        abs(dot(openingOffset,
            @Uniform(POINT_SHADOW_OPENING_BITANGENT_HALF_HEIGHT).xyz)) <=
            @Uniform(POINT_SHADOW_OPENING_BITANGENT_HALF_HEIGHT).w)
        discard;
}

void main()
{
    applyPointShadowCasterClip(@In(SHADOW_WORLD_POSITION));
    gl_FragDepth = length(@In(SHADOW_WORLD_POSITION) - POINT_POSITION_AND_RANGE.xyz) / POINT_POSITION_AND_RANGE.w;
}
)";

const std::string VertexShaderPointAlphaShadowDepthTemplate =
R"(
@@Version

void main()
{
    @Out(vec3 SHADOW_WORLD_POSITION) = @Vec3(@MMatrix * @Vec4(@In(POSITION)));
    @Out(vec2 SHADOW_TEXCOORDS) = @In(TEXCOORDS);
    gl_Position = @MCPMatrix * @Vec4(@In(POSITION));
}
)";

const std::string FragmentShaderPointAlphaShadowDepthTemplate =
R"(
@@Version

@@Uniform(float SHADOW_ALPHA_CUTOFF);
@@Uniform(float SHADOW_ALPHA_FACTOR);
@@Texture(sampler2D SHADOW_ALPHA_MAP);

layout(std140, binding = 2) uniform ShadowFrame
{
    mat4 LIGHT_VIEW_PROJECTION;
    vec4 MAP_TEXEL_SIZE_AND_RADIUS;
    vec4 BIAS_AND_ENABLED;
    vec4 POINT_POSITION_AND_RANGE;
    vec4 SHADOW_TYPE_AND_LIGHT_INDEX;
};

@@Uniform(int POINT_SHADOW_CASTER_CLIP_ENABLED);
@@Uniform(vec4 POINT_SHADOW_RETAINED_PLANE);
@@Uniform(vec4 POINT_SHADOW_OPENING_CENTRE_TOLERANCE);
@@Uniform(vec4 POINT_SHADOW_OPENING_TANGENT_HALF_WIDTH);
@@Uniform(vec4 POINT_SHADOW_OPENING_BITANGENT_HALF_HEIGHT);

void applyPointShadowCasterClip(vec3 worldPosition)
{
    if (@Uniform(POINT_SHADOW_CASTER_CLIP_ENABLED) == 0) return;
    vec4 plane = @Uniform(POINT_SHADOW_RETAINED_PLANE);
    float planeDistance = dot(plane.xyz, worldPosition) + plane.w;
    float tolerance = @Uniform(POINT_SHADOW_OPENING_CENTRE_TOLERANCE).w;
    if (planeDistance < -tolerance) discard;
    vec3 openingOffset = worldPosition -
        @Uniform(POINT_SHADOW_OPENING_CENTRE_TOLERANCE).xyz;
    if (abs(planeDistance) <= tolerance &&
        abs(dot(openingOffset,
            @Uniform(POINT_SHADOW_OPENING_TANGENT_HALF_WIDTH).xyz)) <=
            @Uniform(POINT_SHADOW_OPENING_TANGENT_HALF_WIDTH).w &&
        abs(dot(openingOffset,
            @Uniform(POINT_SHADOW_OPENING_BITANGENT_HALF_HEIGHT).xyz)) <=
            @Uniform(POINT_SHADOW_OPENING_BITANGENT_HALF_HEIGHT).w)
        discard;
}

void main()
{
    if (texture(@Texture(SHADOW_ALPHA_MAP), @In(SHADOW_TEXCOORDS)).a * @Uniform(SHADOW_ALPHA_FACTOR) < @Uniform(SHADOW_ALPHA_CUTOFF)) discard;
    applyPointShadowCasterClip(@In(SHADOW_WORLD_POSITION));
    gl_FragDepth = length(@In(SHADOW_WORLD_POSITION) - POINT_POSITION_AND_RANGE.xyz) / POINT_POSITION_AND_RANGE.w;
}
)";

const std::string FragmentShaderFullscreenTemplate =
R"(
@@Version

@@Uniform(float GAMMA);
@@Uniform(vec4 DIFFUSE);
@@Texture(sampler2D TEX1);

void main()
{
	@Out(vec4 COLOUR) = texture(@Texture(TEX1), @In(TEXCOORDS)) * @Uniform(DIFFUSE);
}
)";

const std::string FragmentShaderEquirectangularToCubemapTemplate =
R"(
@@Version

@@Uniform(int FACE);
@@Uniform(vec2 OUTPUT_SIZE);
@@Texture(sampler2D EQUIRECTANGULAR);

void main()
{
    vec2 pixel = gl_FragCoord.xy / @Uniform(OUTPUT_SIZE);
    float u = pixel.x * 2.0 - 1.0;
    float v = pixel.y * 2.0 - 1.0;
    vec3 direction;
    if (@Uniform(FACE) == 0) direction = vec3( 1.0, -v, -u);
    else if (@Uniform(FACE) == 1) direction = vec3(-1.0, -v,  u);
    else if (@Uniform(FACE) == 2) direction = vec3( u,  1.0,  v);
    else if (@Uniform(FACE) == 3) direction = vec3( u, -1.0, -v);
    else if (@Uniform(FACE) == 4) direction = vec3( u, -v,  1.0);
    else direction = vec3(-u, -v, -1.0);
    direction = normalize(direction);
    vec2 uv = vec2(atan(direction.z, direction.x) / (2.0 * 3.14159265359) + 0.5,
                   0.5 - asin(clamp(direction.y, -1.0, 1.0)) / 3.14159265359);
    @Out(vec4 COLOUR) = texture(@Texture(EQUIRECTANGULAR), uv);
}
)";

const std::string FragmentShaderDiffuseIrradianceTemplate =
R"(
@@Version

@@Uniform(int FACE);
@@Uniform(vec2 OUTPUT_SIZE);
@@Uniform(int SAMPLE_COUNT);
@@Texture(samplerCube ENVIRONMENT);

vec3 faceDirection(vec2 pixel)
{
    float u = pixel.x * 2.0 - 1.0;
    float v = pixel.y * 2.0 - 1.0;
    if (@Uniform(FACE) == 0) return normalize(vec3( 1.0, -v, -u));
    if (@Uniform(FACE) == 1) return normalize(vec3(-1.0, -v,  u));
    if (@Uniform(FACE) == 2) return normalize(vec3( u,  1.0,  v));
    if (@Uniform(FACE) == 3) return normalize(vec3( u, -1.0, -v));
    if (@Uniform(FACE) == 4) return normalize(vec3( u, -v,  1.0));
    return normalize(vec3(-u, -v, -1.0));
}

float radicalInverse(uint value)
{
    value = (value << 16u) | (value >> 16u);
    value = ((value & 0x55555555u) << 1u) | ((value & 0xAAAAAAAAu) >> 1u);
    value = ((value & 0x33333333u) << 2u) | ((value & 0xCCCCCCCCu) >> 2u);
    value = ((value & 0x0F0F0F0Fu) << 4u) | ((value & 0xF0F0F0F0u) >> 4u);
    value = ((value & 0x00FF00FFu) << 8u) | ((value & 0xFF00FF00u) >> 8u);
    return float(value) * 2.3283064365386963e-10;
}

void main()
{
    vec3 normal = faceDirection(gl_FragCoord.xy / @Uniform(OUTPUT_SIZE));
    vec3 up = abs(normal.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(up, normal));
    vec3 bitangent = cross(normal, tangent);
    vec3 sum = vec3(0.0);
    int samples = clamp(@Uniform(SAMPLE_COUNT), 1, 1024);
    for (int index = 0; index < 1024; ++index)
    {
        if (index >= samples) break;
        vec2 xi = vec2((float(index) + 0.5) / float(samples), radicalInverse(uint(index)));
        float phi = 6.28318530718 * xi.y;
        float cosTheta = sqrt(1.0 - xi.x);
        float sinTheta = sqrt(xi.x);
        vec3 local = vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
        vec3 light = normalize(tangent * local.x + bitangent * local.y + normal * local.z);
        // cosTheta above is sqrt(1-xi.x), so these directions already carry the
        // pdf cos(theta)/pi. The cosine and the 1/pi both cancel out of the
        // estimator, leaving E/pi -- which is exactly what the PBR shader wants
        // for Lambert diffuse -- as the plain unweighted mean of the radiance.
        // Re-applying the cosine here and normalising by its sum would integrate
        // against a cos^2 lobe instead, concentrating ambient light too tightly
        // around the normal. A constant environment hides the error entirely.
        sum += texture(@Texture(ENVIRONMENT), light).rgb;
    }
    @Out(vec4 COLOUR) = vec4(sum / float(samples), 1.0);
}
)";

const std::string FragmentShaderPrefilteredSpecularTemplate =
R"(
@@Version

@@Uniform(int FACE);
@@Uniform(vec2 OUTPUT_SIZE);
@@Uniform(float ROUGHNESS);
@@Uniform(float SOURCE_RESOLUTION);
@@Uniform(int SAMPLE_COUNT);
@@Texture(samplerCube ENVIRONMENT);

vec3 faceDirection(vec2 pixel)
{
    float u = pixel.x * 2.0 - 1.0; float v = pixel.y * 2.0 - 1.0;
    if (@Uniform(FACE) == 0) return normalize(vec3( 1.0, -v, -u));
    if (@Uniform(FACE) == 1) return normalize(vec3(-1.0, -v,  u));
    if (@Uniform(FACE) == 2) return normalize(vec3( u,  1.0,  v));
    if (@Uniform(FACE) == 3) return normalize(vec3( u, -1.0, -v));
    if (@Uniform(FACE) == 4) return normalize(vec3( u, -v,  1.0));
    return normalize(vec3(-u, -v, -1.0));
}
float radicalInverse(uint value)
{
    value=(value<<16u)|(value>>16u); value=((value&0x55555555u)<<1u)|((value&0xAAAAAAAAu)>>1u); value=((value&0x33333333u)<<2u)|((value&0xCCCCCCCCu)>>2u); value=((value&0x0F0F0F0Fu)<<4u)|((value&0xF0F0F0F0u)>>4u); value=((value&0x00FF00FFu)<<8u)|((value&0xFF00FF00u)>>8u); return float(value)*2.3283064365386963e-10;
}
vec3 importanceSampleGGX(vec2 xi, float roughness, vec3 normal)
{
    float a=roughness*roughness, a2=a*a; float phi=6.28318530718*xi.x;
    float cosTheta=sqrt((1.0-xi.y)/max(1.0+(a2-1.0)*xi.y,0.00001)); float sinTheta=sqrt(max(1.0-cosTheta*cosTheta,0.0));
    vec3 halfVector=vec3(cos(phi)*sinTheta,sin(phi)*sinTheta,cosTheta);
    vec3 up=abs(normal.y)<0.999?vec3(0.0,1.0,0.0):vec3(1.0,0.0,0.0); vec3 tangent=normalize(cross(up,normal)); vec3 bitangent=cross(normal,tangent);
    return normalize(tangent*halfVector.x+bitangent*halfVector.y+normal*halfVector.z);
}
void main()
{
    vec3 normal = faceDirection(gl_FragCoord.xy / @Uniform(OUTPUT_SIZE));
    vec3 view = normal; vec3 sum = vec3(0.0); float weight = 0.0;
    float roughness = clamp(@Uniform(ROUGHNESS), 0.0, 1.0);
    int samples = clamp(@Uniform(SAMPLE_COUNT), 1, 1024);
    for (int index = 0; index < 1024; ++index)
    {
        if (index >= samples) break;
        vec2 xi = vec2((float(index) + 0.5) / float(samples), radicalInverse(uint(index)));
        vec3 halfVector = importanceSampleGGX(xi, roughness, normal);
        vec3 light = normalize(2.0 * dot(view, halfVector) * halfVector - view);
        float nDotL = max(dot(normal, light), 0.0);
        if (nDotL > 0.0)
        {
            float nDotH = max(dot(normal, halfVector), 0.0);
            float vDotH = max(dot(view, halfVector), 0.0);
            float a = roughness * roughness, a2 = a * a;
            float denominator = nDotH * nDotH * (a2 - 1.0) + 1.0;
            float distribution = a2 / max(3.14159265359 * denominator * denominator, 0.00001);
            float pdf = max(distribution * nDotH / max(4.0 * vDotH, 0.00001), 0.00001);
            float texelSolidAngle = 4.0 * 3.14159265359 / (6.0 * @Uniform(SOURCE_RESOLUTION) * @Uniform(SOURCE_RESOLUTION));
            float sampleSolidAngle = 1.0 / (float(samples) * pdf);
            float lod = roughness <= 0.00001 ? 0.0 : max(0.0, 0.5 * log2(sampleSolidAngle / texelSolidAngle));
            sum += textureLod(@Texture(ENVIRONMENT), light, lod).rgb * nDotL;
            weight += nDotL;
        }
    }
    @Out(vec4 COLOUR)=vec4(sum/max(weight,0.00001),1.0);
}
)";

const std::string FragmentShaderPbrBrdfIntegrationTemplate =
R"(
@@Version

@@Uniform(int SAMPLE_COUNT);
@@Uniform(vec2 OUTPUT_SIZE);

float radicalInverse(uint value)
{
    value=(value<<16u)|(value>>16u); value=((value&0x55555555u)<<1u)|((value&0xAAAAAAAAu)>>1u); value=((value&0x33333333u)<<2u)|((value&0xCCCCCCCCu)>>2u); value=((value&0x0F0F0F0Fu)<<4u)|((value&0xF0F0F0F0u)>>4u); value=((value&0x00FF00FFu)<<8u)|((value&0xFF00FF00u)>>8u); return float(value)*2.3283064365386963e-10;
}
vec3 importanceSampleGGX(vec2 xi,float roughness,vec3 normal)
{
    float a=roughness*roughness,a2=a*a,phi=6.28318530718*xi.x; float cosTheta=sqrt((1.0-xi.y)/max(1.0+(a2-1.0)*xi.y,0.00001)); float sinTheta=sqrt(max(1.0-cosTheta*cosTheta,0.0)); vec3 halfVector=vec3(cos(phi)*sinTheta,sin(phi)*sinTheta,cosTheta); vec3 tangent=vec3(1.0,0.0,0.0),bitangent=vec3(0.0,1.0,0.0); return normalize(tangent*halfVector.x+bitangent*halfVector.y+normal*halfVector.z);
}
float geometrySchlickGGX(float nDotV,float roughness)
{
    float a=roughness*roughness,k=a*a*0.5; return nDotV/max(nDotV*(1.0-k)+k,0.00001);
}
float geometrySmith(float nDotV,float nDotL,float roughness) { return geometrySchlickGGX(nDotV,roughness)*geometrySchlickGGX(nDotL,roughness); }
void main()
{
    vec2 uv = gl_FragCoord.xy / @Uniform(OUTPUT_SIZE);
    float nDotV = clamp(uv.x, 0.0001, 1.0);
    float roughness = clamp(uv.y, 0.0, 1.0);
    vec3 view = vec3(sqrt(max(1.0 - nDotV * nDotV, 0.0)), 0.0, nDotV);
    vec3 normal = vec3(0.0, 0.0, 1.0); float a = 0.0, b = 0.0;
    int samples = clamp(@Uniform(SAMPLE_COUNT), 1, 1024);
    for (int index = 0; index < 1024; ++index)
    {
        if (index >= samples) break;
        vec2 xi = vec2((float(index) + 0.5) / float(samples), radicalInverse(uint(index)));
        vec3 halfVector = importanceSampleGGX(xi, roughness, normal);
        vec3 light = normalize(2.0 * dot(view, halfVector) * halfVector - view);
        float nDotL = max(light.z, 0.0), nDotH = max(halfVector.z, 0.0), vDotH = max(dot(view, halfVector), 0.0);
        if (nDotL > 0.0)
        {
            float visibility = geometrySmith(nDotV, nDotL, roughness) * vDotH / max(nDotH * nDotV, 0.00001);
            float fresnel = pow(1.0 - vDotH, 5.0);
            a += (1.0 - fresnel) * visibility; b += fresnel * visibility;
        }
    }
    @Out(vec4 COLOUR)=vec4(a/float(samples),b/float(samples),0.0,1.0);
}
)";

// Diagnostic graph-image visualization used by PipelineEditor and GPU tools.
const std::string FragmentShaderTextureDiagnosticTemplate =
R"(
@@Version

@@Uniform(int MODE);
@@Uniform(float EXPOSURE);
@@Uniform(float GAMMA);
@@Uniform(float DEPTH_NEAR);
@@Uniform(float DEPTH_FAR);
@@Texture(sampler2D SOURCE);

vec3 aces(vec3 value)
{
    return clamp((value * (2.51 * value + 0.03)) / (value * (2.43 * value + 0.59) + 0.14), 0.0, 1.0);
}

vec3 heat(float value)
{
    value = clamp(value, 0.0, 1.0);
    return clamp(vec3(1.5 - abs(4.0 * value - 3.0), 1.5 - abs(4.0 * value - 2.0), 1.5 - abs(4.0 * value - 1.0)), 0.0, 1.0);
}

void main()
{
    vec4 source = texture(@Texture(SOURCE), @In(TEXCOORDS));
    vec3 result = source.rgb;
    if (@Uniform(MODE) == 1) result = vec3(source.r);
    else if (@Uniform(MODE) == 2) result = vec3(source.g);
    else if (@Uniform(MODE) == 3) result = vec3(source.b);
    else if (@Uniform(MODE) == 4) result = vec3(source.a);
    else if (@Uniform(MODE) == 5) result = vec3(dot(source.rgb, vec3(0.2126, 0.7152, 0.0722)));
    else if (@Uniform(MODE) == 6)
    {
        float z = source.r * 2.0 - 1.0;
        float linearDepth = (2.0 * @Uniform(DEPTH_NEAR) * @Uniform(DEPTH_FAR)) /
            max(@Uniform(DEPTH_FAR) + @Uniform(DEPTH_NEAR) - z * (@Uniform(DEPTH_FAR) - @Uniform(DEPTH_NEAR)), 0.000001);
        result = vec3(1.0 - clamp((linearDepth - @Uniform(DEPTH_NEAR)) / max(@Uniform(DEPTH_FAR) - @Uniform(DEPTH_NEAR), 0.000001), 0.0, 1.0));
    }
    else if (@Uniform(MODE) == 7) result = aces(max(source.rgb, vec3(0.0)) * @Uniform(EXPOSURE));
    else if (@Uniform(MODE) == 8) result = heat(log2(1.0 + max(dot(source.rgb, vec3(0.2126, 0.7152, 0.0722)) * @Uniform(EXPOSURE), 0.0)) / 8.0);
    result = pow(max(result, vec3(0.0)), vec3(1.0 / max(@Uniform(GAMMA), 0.0001)));
    @Out(vec4 COLOUR) = vec4(result, 1.0);
}
)";

// Temporary HDR presentation shader used by the opt-in PBR pipeline. Surface
// shading remains replaceable while the PBR material model is introduced.
const std::string FragmentShaderEnvironmentDebugCubeTemplate =
R"(
@@Version

@@Uniform(mat4 INVERSE_VIEW_PROJECTION);
@@Uniform(vec3 CAMERA_POSITION);
@@Texture(samplerCube ENVIRONMENT);

void main()
{
    vec2 ndc = @In(TEXCOORDS) * 2.0 - 1.0;
    vec4 world = @Uniform(INVERSE_VIEW_PROJECTION) * vec4(ndc, 1.0, 1.0);
    vec3 direction = normalize(world.xyz / world.w - @Uniform(CAMERA_POSITION));
    @Out(vec4 COLOUR) = vec4(texture(@Texture(ENVIRONMENT), direction).rgb, 1.0);
    @Out(vec4 BLOOM_MASK) = vec4(0.0);
}
)";

const std::string FragmentShaderToneMapTemplate =
R"(
@@Version

@@Uniform(float EXPOSURE);
@@Uniform(float GAMMA);
@@Uniform(int TONE_MAP_OPERATOR);
@@Texture(sampler2D TEX1);

void main()
{
	vec3 colour = texture(@Texture(TEX1), @In(TEXCOORDS)).rgb * @Uniform(EXPOSURE);
	if (@Uniform(TONE_MAP_OPERATOR) == 0)
	{
		colour = colour / (colour + vec3(1.0));
	}
	else
	{
		// Narkowicz ACES filmic approximation.
		colour = clamp((colour * (2.51 * colour + 0.03)) / (colour * (2.43 * colour + 0.59) + 0.14), 0.0, 1.0);
	}
	@Out(vec4 COLOUR) = vec4(pow(colour, vec3(1.0 / @Uniform(GAMMA))), 1.0);
}
)";

/*
 * Text shader.
 *
 */
/*
 * Pipeline-owned bloom shaders. Bloom is image-space and therefore works for
 * any material rendered into the pipeline scene target.
 */
const std::string FragmentShaderFxaaTemplate =
R"(
@@Version

@@Texture(sampler2D TEX1);

float luma(vec3 colour)
{
    return dot(colour, vec3(0.299, 0.587, 0.114));
}

void main()
{
    vec2 texel = 1.0 / vec2(textureSize(@Texture(TEX1), 0));
    vec2 uv = @In(TEXCOORDS);
    vec4 centre = texture(@Texture(TEX1), uv);
    float lumaM = luma(centre.rgb);
    float lumaN = luma(texture(@Texture(TEX1), uv + vec2(0.0, -texel.y)).rgb);
    float lumaS = luma(texture(@Texture(TEX1), uv + vec2(0.0, texel.y)).rgb);
    float lumaW = luma(texture(@Texture(TEX1), uv + vec2(-texel.x, 0.0)).rgb);
    float lumaE = luma(texture(@Texture(TEX1), uv + vec2(texel.x, 0.0)).rgb);
    float minimumLuma = min(lumaM, min(min(lumaN, lumaS), min(lumaW, lumaE)));
    float maximumLuma = max(lumaM, max(max(lumaN, lumaS), max(lumaW, lumaE)));
    float range = maximumLuma - minimumLuma;
    vec3 result = centre.rgb;
    if (range >= max(0.0312, maximumLuma * 0.125))
    {
    float lumaNW = luma(texture(@Texture(TEX1), uv + vec2(-texel.x, -texel.y)).rgb);
    float lumaNE = luma(texture(@Texture(TEX1), uv + vec2(texel.x, -texel.y)).rgb);
    float lumaSW = luma(texture(@Texture(TEX1), uv + vec2(-texel.x, texel.y)).rgb);
    float lumaSE = luma(texture(@Texture(TEX1), uv + vec2(texel.x, texel.y)).rgb);
    float horizontal = abs(-2.0 * lumaW + lumaNW + lumaSW) + abs(-2.0 * lumaM + lumaN + lumaS) * 2.0 + abs(-2.0 * lumaE + lumaNE + lumaSE);
    float vertical = abs(-2.0 * lumaN + lumaNW + lumaNE) + abs(-2.0 * lumaM + lumaW + lumaE) * 2.0 + abs(-2.0 * lumaS + lumaSW + lumaSE);
    bool horizontalEdge = horizontal >= vertical;
    float negativeLuma = horizontalEdge ? lumaN : lumaW;
    float positiveLuma = horizontalEdge ? lumaS : lumaE;
    float negativeGradient = abs(negativeLuma - lumaM);
    float positiveGradient = abs(positiveLuma - lumaM);
    bool negativeDirection = negativeGradient >= positiveGradient;
    float gradient = max(negativeGradient, positiveGradient);
    float localAverage = 0.5 * (lumaM + (negativeDirection ? negativeLuma : positiveLuma));
    vec2 stepDirection = horizontalEdge ? vec2(texel.x, 0.0) : vec2(0.0, texel.y);
    vec2 edgeOffset = horizontalEdge ? vec2(0.0, texel.y * (negativeDirection ? -0.5 : 0.5)) : vec2(texel.x * (negativeDirection ? -0.5 : 0.5), 0.0);
    vec2 edgeUv = uv + edgeOffset;
    vec2 negativeUv = edgeUv - stepDirection;
    vec2 positiveUv = edgeUv + stepDirection;
    float negativeDelta = luma(texture(@Texture(TEX1), negativeUv).rgb) - localAverage;
    float positiveDelta = luma(texture(@Texture(TEX1), positiveUv).rgb) - localAverage;
    bool negativeEnd = abs(negativeDelta) >= gradient * 0.25;
    bool positiveEnd = abs(positiveDelta) >= gradient * 0.25;
    for (int iteration = 0; iteration < 12 && (!negativeEnd || !positiveEnd); ++iteration)
    {
        float quality = iteration < 4 ? 1.0 : (iteration < 8 ? 2.0 : 4.0);
        if (!negativeEnd) { negativeUv -= stepDirection * quality; negativeDelta = luma(texture(@Texture(TEX1), negativeUv).rgb) - localAverage; negativeEnd = abs(negativeDelta) >= gradient * 0.25; }
        if (!positiveEnd) { positiveUv += stepDirection * quality; positiveDelta = luma(texture(@Texture(TEX1), positiveUv).rgb) - localAverage; positiveEnd = abs(positiveDelta) >= gradient * 0.25; }
    }
    float negativeDistance = horizontalEdge ? uv.x - negativeUv.x : uv.y - negativeUv.y;
    float positiveDistance = horizontalEdge ? positiveUv.x - uv.x : positiveUv.y - uv.y;
    bool useNegative = negativeDistance < positiveDistance;
    float shortestDistance = min(negativeDistance, positiveDistance);
    float edgeSpan = max(negativeDistance + positiveDistance, 0.000001);
    float pixelOffset = -shortestDistance / edgeSpan + 0.5;
    float selectedDelta = useNegative ? negativeDelta : positiveDelta;
    if ((selectedDelta < 0.0) == (lumaM < localAverage)) pixelOffset = 0.0;
    float averageLuma = (2.0 * (lumaN + lumaS + lumaW + lumaE) + lumaNW + lumaNE + lumaSW + lumaSE) / 12.0;
    float subpixel = clamp(abs(averageLuma - lumaM) / max(range, 0.000001), 0.0, 1.0);
    subpixel = subpixel * subpixel * (3.0 - 2.0 * subpixel);
    subpixel = subpixel * subpixel * 0.75;
    float finalOffset = max(pixelOffset, subpixel);
    vec2 finalUv = uv + (horizontalEdge ? vec2(0.0, texel.y * finalOffset * (negativeDirection ? -1.0 : 1.0)) : vec2(texel.x * finalOffset * (negativeDirection ? -1.0 : 1.0), 0.0));
    result = texture(@Texture(TEX1), finalUv).rgb;
    }
    @Out(vec4 COLOUR) = vec4(result, centre.a);
}
)";

const std::string FragmentShaderTaaTemplate =
R"(
@@Version

@@Uniform(mat4 INVERSE_CURRENT_VIEW_PROJECTION);
@@Uniform(mat4 PREVIOUS_VIEW_PROJECTION);
@@Texture(sampler2D CURRENT_COLOUR);
@@Texture(sampler2D CURRENT_DEPTH);
@@Texture(sampler2D HISTORY_COLOUR);
@@Texture(sampler2D HISTORY_DEPTH);

void main()
{
    ivec2 size = textureSize(@Texture(CURRENT_COLOUR), 0);
    ivec2 pixel = clamp(ivec2(gl_FragCoord.xy), ivec2(0), size - ivec2(1));
    vec2 uv = (vec2(pixel) + vec2(0.5)) / vec2(size);
    vec4 current = texelFetch(@Texture(CURRENT_COLOUR), pixel, 0);
    float depth = texelFetch(@Texture(CURRENT_DEPTH), pixel, 0).r;
    vec4 clip = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 world = @Uniform(INVERSE_CURRENT_VIEW_PROJECTION) * clip;
    world /= max(abs(world.w), 0.000001);
    vec4 previousClip = @Uniform(PREVIOUS_VIEW_PROJECTION) * world;
    vec2 previousUv = previousClip.xy / max(abs(previousClip.w), 0.000001) * 0.5 + 0.5;
    float expectedPreviousDepth = previousClip.z / max(abs(previousClip.w), 0.000001) * 0.5 + 0.5;
    bool valid = previousClip.w > 0.0 && all(greaterThanEqual(previousUv, vec2(0.0))) && all(lessThanEqual(previousUv, vec2(1.0)));
    float storedPreviousDepth = texture(@Texture(HISTORY_DEPTH), previousUv).r;
    valid = valid && abs(storedPreviousDepth - expectedPreviousDepth) <= 0.01;
    vec4 neighbourhoodMin = current;
    vec4 neighbourhoodMax = current;
    for (int y = -1; y <= 1; ++y)
    for (int x = -1; x <= 1; ++x)
    {
        vec4 sampleValue = texelFetch(@Texture(CURRENT_COLOUR), clamp(pixel + ivec2(x, y), ivec2(0), size - ivec2(1)), 0);
        neighbourhoodMin = min(neighbourhoodMin, sampleValue);
        neighbourhoodMax = max(neighbourhoodMax, sampleValue);
    }
    vec4 history = clamp(texture(@Texture(HISTORY_COLOUR), previousUv), neighbourhoodMin, neighbourhoodMax);
    @Out(vec4 COLOUR) = valid ? mix(current, history, 0.9) : current;
}
)";

const std::string FragmentShaderSsaaLanczosTemplate =
R"(
@@Version

@@Uniform(vec2 DIRECTION);
@@Uniform(vec2 OUTPUT_SIZE);
@@Texture(sampler2D TEX1);

float sinc(float value)
{
    if (abs(value) < 0.00001) return 1.0;
    float angle = 3.141592653589793 * value;
    return sin(angle) / angle;
}

float lanczos(float value)
{
    value = abs(value);
    return value < 3.0 ? sinc(value) * sinc(value / 3.0) : 0.0;
}

void main()
{
    ivec2 sourceSize = textureSize(@Texture(TEX1), 0);
    vec2 scale = vec2(sourceSize) / @Uniform(OUTPUT_SIZE);
    bool horizontal = @Uniform(DIRECTION).x > 0.5;
    float axisScale = horizontal ? scale.x : scale.y;
    float sourcePosition = (horizontal ? gl_FragCoord.x : gl_FragCoord.y) * axisScale - 0.5;
    int centre = int(floor(sourcePosition));
    int fixedCoordinate = int(floor(horizontal ? gl_FragCoord.y : gl_FragCoord.x));
    float filterScale = max(axisScale, 1.0);
    vec4 sum = vec4(0.0);
    float total = 0.0;
    for (int offset = -9; offset <= 9; ++offset)
    {
        int coordinate = centre + offset;
        float weight = lanczos((sourcePosition - float(coordinate)) / filterScale);
        if (weight == 0.0) continue;
        ivec2 sampleCoordinate = horizontal ? ivec2(clamp(coordinate, 0, sourceSize.x - 1), clamp(fixedCoordinate, 0, sourceSize.y - 1)) : ivec2(clamp(fixedCoordinate, 0, sourceSize.x - 1), clamp(coordinate, 0, sourceSize.y - 1));
        sum += texelFetch(@Texture(TEX1), sampleCoordinate, 0) * weight;
        total += weight;
    }
    @Out(vec4 COLOUR) = sum / max(total, 0.000001);
}
)";

const std::string FragmentShaderSsaoRawTemplate =
R"(
@@Version

@@Uniform(mat4 PROJECTION);
@@Uniform(mat4 INVERSE_PROJECTION);
@@Uniform(float RADIUS);
@@Uniform(float INTENSITY);
@@Uniform(float BIAS);
@@Uniform(float POWER);
@@Uniform(int SAMPLE_COUNT);
@@Texture(sampler2D DEPTH);

vec3 viewPosition(vec2 uv, float depth)
{
    vec4 position = @Uniform(INVERSE_PROJECTION) * vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    return position.xyz / position.w;
}

float noise(vec2 value)
{
    return fract(sin(dot(value, vec2(12.9898, 78.233))) * 43758.5453);
}

void main()
{
    vec2 uv = @In(TEXCOORDS);
    float centreDepth = texture(@Texture(DEPTH), uv).r;
    vec3 position = viewPosition(uv, centreDepth);
    // Forward rendering has no normals attachment. Reconstruct the geometric
    // view-space normal from neighbouring depth samples instead. Derivatives
    // stay outside depth-dependent control flow so silhouette quads are defined.
    vec3 normal = normalize(cross(dFdx(position), dFdy(position)));
    if (normal.z < 0.0) normal = -normal;
    float ambient = 1.0;
    float radius = max(@Uniform(RADIUS), 0.0);
    if (radius > 0.0 && centreDepth < 0.999999)
    {
        float angle = noise(gl_FragCoord.xy) * 6.28318530718;
        vec3 randomVector = vec3(cos(angle), sin(angle), 0.0);
        vec3 tangentCandidate = randomVector - normal * dot(randomVector, normal);
        if (dot(tangentCandidate, tangentCandidate) < 0.01)
            tangentCandidate = cross(normal, abs(normal.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0));
        vec3 tangent = normalize(tangentCandidate);
        vec3 bitangent = cross(normal, tangent);

        int count = clamp(@Uniform(SAMPLE_COUNT), 1, 64);
        float occluded = 0.0;
        float considered = 0.0;
        for (int index = 0; index < 64; ++index)
        {
            if (index >= count) break;
            float fraction = (float(index) + 0.5) / float(count);
            float phi = 6.28318530718 * fract(float(index) * 0.61803398875 + noise(gl_FragCoord.xy));
            float radial = sqrt(fraction);
            vec3 hemisphere = vec3(cos(phi) * radial, sin(phi) * radial, sqrt(1.0 - fraction));
            vec3 direction = tangent * hemisphere.x + bitangent * hemisphere.y + normal * hemisphere.z;
            vec3 samplePosition = position + direction * radius;
            vec4 projected = @Uniform(PROJECTION) * vec4(samplePosition, 1.0);
            vec2 sampleUv = projected.xy / projected.w * 0.5 + 0.5;
            if (sampleUv.x <= 0.0 || sampleUv.x >= 1.0 || sampleUv.y <= 0.0 || sampleUv.y >= 1.0) continue;
            float sampledDepth = texture(@Texture(DEPTH), sampleUv).r;
            if (sampledDepth >= 0.999999) continue;
            float sampledZ = viewPosition(sampleUv, sampledDepth).z;
            float rangeWeight = smoothstep(0.0, 1.0, radius / max(abs(position.z - sampledZ), 0.0001));
            occluded += (sampledZ >= samplePosition.z + max(@Uniform(BIAS), 0.0) ? 1.0 : 0.0) * rangeWeight;
            considered += 1.0;
        }
        float rawOcclusion = considered > 0.0 ? occluded / considered : 0.0;
        ambient = pow(clamp(1.0 - rawOcclusion * max(@Uniform(INTENSITY), 0.0), 0.0, 1.0), max(@Uniform(POWER), 0.0));
    }
    @Out(vec4 COLOUR) = vec4(ambient, ambient, ambient, 1.0);
}
)";

const std::string FragmentShaderGtaoRawTemplate =
R"(
@@Version

@@Uniform(mat4 PROJECTION);
@@Uniform(mat4 INVERSE_PROJECTION);
@@Uniform(float RADIUS);
@@Uniform(float INTENSITY);
@@Uniform(float THICKNESS);
@@Uniform(float HORIZON_BIAS);
@@Uniform(float FALLOFF_START);
@@Uniform(float FALLOFF_END);
@@Uniform(int SLICE_COUNT);
@@Uniform(int STEPS_PER_SLICE);
@@Uniform(float POWER);
@@Uniform(int NORMAL_SOURCE);
@@Texture(sampler2D DEPTH);
@@Texture(sampler2D NORMALS);

vec3 gtaoViewPosition(vec2 uv, float depth)
{
    vec4 position = @Uniform(INVERSE_PROJECTION) * vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    return position.xyz / position.w;
}

float gtaoNoise(vec2 value)
{
    return fract(sin(dot(value, vec2(12.9898, 78.233))) * 43758.5453);
}

vec3 gtaoDecodeOctahedralNormal(vec2 encoded)
{
    vec2 oct = encoded * 2.0 - 1.0;
    vec3 normal = vec3(oct, 1.0 - abs(oct.x) - abs(oct.y));
    if (normal.z < 0.0) normal.xy = (1.0 - abs(normal.yx)) * sign(normal.xy);
    return normalize(normal);
}

void main()
{
    vec2 uv = @In(TEXCOORDS);
    float centreDepth = texture(@Texture(DEPTH), uv).r;
    float ambient = 1.0;
    float radius = max(@Uniform(RADIUS), 0.0);
    if (centreDepth < 0.999999 && radius > 0.0)
    {
        vec3 position = gtaoViewPosition(uv, centreDepth);
        vec3 normal = normalize(cross(dFdx(position), dFdy(position)));
        if (@Uniform(NORMAL_SOURCE) != 0)
            normal = gtaoDecodeOctahedralNormal(texture(@Texture(NORMALS), uv).rg);
        else if (normal.z < 0.0) normal = -normal;
        int slices = clamp(@Uniform(SLICE_COUNT), 1, 16);
        int steps = clamp(@Uniform(STEPS_PER_SLICE), 1, 16);
        float projectedRadius = radius * abs(@Uniform(PROJECTION)[1][1]) / max(-position.z, 0.001) * 0.5;
        float rotation = gtaoNoise(gl_FragCoord.xy) * 3.14159265359;
        float occlusion = 0.0;
        for (int slice = 0; slice < 16; ++slice)
        {
            if (slice >= slices) break;
            float angle = rotation + (float(slice) + 0.5) * 3.14159265359 / float(slices);
            vec2 axis = vec2(cos(angle), sin(angle));
            for (int side = -1; side <= 1; side += 2)
            {
                float horizon = 0.0;
                for (int stepIndex = 0; stepIndex < 16; ++stepIndex)
                {
                    if (stepIndex >= steps) break;
                    float fraction = (float(stepIndex) + 1.0) / float(steps);
                    vec2 sampleUv = uv + axis * float(side) * projectedRadius * fraction;
                    if (sampleUv.x <= 0.0 || sampleUv.x >= 1.0 || sampleUv.y <= 0.0 || sampleUv.y >= 1.0) continue;
                    float sampleDepth = texture(@Texture(DEPTH), sampleUv).r;
                    if (sampleDepth >= 0.999999) continue;
                    vec3 delta = gtaoViewPosition(sampleUv, sampleDepth) - position;
                    float distanceToSample = length(delta);
                    if (distanceToSample <= 0.0001 || distanceToSample > radius) continue;
                    float startDistance = clamp(@Uniform(FALLOFF_START), 0.0, 1.0) * radius;
                    float endDistance = max(clamp(@Uniform(FALLOFF_END), 0.0, 1.0) * radius, startDistance + 0.0001);
                    float rangeWeight = 1.0 - smoothstep(startDistance, endDistance, distanceToSample);
                    float thicknessWeight = clamp(max(@Uniform(THICKNESS), 0.0001) / (abs(delta.z) + max(@Uniform(THICKNESS), 0.0001)), 0.0, 1.0);
                    float elevation = max(dot(normal, delta / distanceToSample) - max(@Uniform(HORIZON_BIAS), 0.0), 0.0);
                    horizon = max(horizon, elevation * rangeWeight * thicknessWeight);
                }
                occlusion += horizon;
            }
        }
        float visibility = clamp(1.0 - max(@Uniform(INTENSITY), 0.0) * occlusion / float(slices * 2), 0.0, 1.0);
        ambient = pow(visibility, max(@Uniform(POWER), 0.0001));
    }
    @Out(vec4 COLOUR) = vec4(ambient, ambient, ambient, 1.0);
}
)";

const std::string FragmentShaderSsaoBlurTemplate =
R"(
@@Version

@@Uniform(int BLUR_RADIUS);
@@Texture(sampler2D AMBIENT_OCCLUSION);
@@Texture(sampler2D DEPTH);

void main()
{
    ivec2 size = textureSize(@Texture(AMBIENT_OCCLUSION), 0);
    ivec2 centre = clamp(ivec2(gl_FragCoord.xy), ivec2(0), size - ivec2(1));
    float centreDepth = texelFetch(@Texture(DEPTH), centre, 0).r;
    int radius = clamp(@Uniform(BLUR_RADIUS), 0, 8);
    float sum = 0.0;
    float weightSum = 0.0;
    for (int y = -8; y <= 8; ++y)
    for (int x = -8; x <= 8; ++x)
    {
        if (abs(x) > radius || abs(y) > radius) continue;
        ivec2 samplePixel = clamp(centre + ivec2(x, y), ivec2(0), size - ivec2(1));
        float sampleDepth = texelFetch(@Texture(DEPTH), samplePixel, 0).r;
        float spatialWeight = 1.0 / (1.0 + float(x * x + y * y));
        // Preserve depth discontinuities while smoothing the stochastic SSAO term.
        float depthWeight = exp(-abs(sampleDepth - centreDepth) * 100.0);
        float weight = spatialWeight * depthWeight;
        sum += texelFetch(@Texture(AMBIENT_OCCLUSION), samplePixel, 0).r * weight;
        weightSum += weight;
    }
    float ambient = sum / max(weightSum, 0.00001);
    @Out(vec4 COLOUR) = vec4(ambient, ambient, ambient, 1.0);
}
)";

const std::string FragmentShaderSsaoCombineTemplate =
R"(
@@Version

@@Texture(sampler2D SCENE);
@@Texture(sampler2D AMBIENT_OCCLUSION);

void main()
{
    vec4 scene = texture(@Texture(SCENE), @In(TEXCOORDS));
    float ambient = texture(@Texture(AMBIENT_OCCLUSION), @In(TEXCOORDS)).r;
    @Out(vec4 COLOUR) = vec4(scene.rgb * ambient, scene.a);
}
)";

const std::string FragmentShaderSsaoCombineModulatedTemplate =
R"(
@@Version

@@Texture(sampler2D SCENE);
@@Texture(sampler2D AMBIENT_OCCLUSION);
@@Texture(sampler2D MODULATION);

void main()
{
    vec4 scene = texture(@Texture(SCENE), @In(TEXCOORDS));
    float ambient = texture(@Texture(AMBIENT_OCCLUSION), @In(TEXCOORDS)).r;
    float modulation = texture(@Texture(MODULATION), @In(TEXCOORDS)).r;
    @Out(vec4 COLOUR) = vec4(scene.rgb * mix(1.0, ambient, modulation), scene.a);
}
)";

const std::string FragmentShaderBloomExtractTemplate =
R"(
@@Version

@@Uniform(float THRESHOLD);
@@Texture(sampler2D TEX1);

void main()
{
    vec3 colour = texture(@Texture(TEX1), @In(TEXCOORDS)).rgb;
    @Out(vec4 COLOUR) = vec4(max(colour - vec3(@Uniform(THRESHOLD)), vec3(0.0)), 1.0);
}
)";

const std::string FragmentShaderBloomBlurTemplate =
R"(
@@Version

@@Uniform(vec2 DIRECTION);
@@Texture(sampler2D TEX1);

void main()
{
    vec2 texel = 1.0 / vec2(textureSize(@Texture(TEX1), 0));
    vec2 offset = @Uniform(DIRECTION) * texel;
    vec3 colour = texture(@Texture(TEX1), @In(TEXCOORDS)).rgb * 0.227027;
    colour += texture(@Texture(TEX1), @In(TEXCOORDS) + offset * 1.384615).rgb * 0.316216;
    colour += texture(@Texture(TEX1), @In(TEXCOORDS) - offset * 1.384615).rgb * 0.316216;
    colour += texture(@Texture(TEX1), @In(TEXCOORDS) + offset * 3.230769).rgb * 0.070270;
    colour += texture(@Texture(TEX1), @In(TEXCOORDS) - offset * 3.230769).rgb * 0.070270;
    @Out(vec4 COLOUR) = vec4(colour, 1.0);
}
)";

const std::string FragmentShaderBloomCombineTemplate =
R"(
@@Version

@@Uniform(float INTENSITY);
@@Texture(sampler2D SCENE);
@@Texture(sampler2D BLOOM);

void main()
{
    vec3 scene = texture(@Texture(SCENE), @In(TEXCOORDS)).rgb;
    vec3 bloom = texture(@Texture(BLOOM), @In(TEXCOORDS)).rgb;
    @Out(vec4 COLOUR) = vec4(scene + bloom * @Uniform(INTENSITY), 1.0);
}
)";

const std::string VertexShaderTextTemplate =
R"(
@@Version

void main()
{
	vec4 transVertex = @MCPMatrix * @Vec4(@In(POSITION));

	vec2 centredPos = transVertex.xy - @HalfWindowSize;

## Points
	// gl_PointSize is undefined until assigned. Use the point-size input for
	// positioning as well as assigning the built-in output below.
	centredPos += @PointSize / 2.0;
	@Out(vec4 TEXCOORDS) = @In(TEXCOORDS);
## Else
    @Out(vec2 TEXCOORDS) = @In(TEXCOORDS);
##

## Colours
	@Out(vec4 COLOUR) = @In(COLOUR);
##
	gl_Position = vec4(centredPos / @HalfWindowSize, 0, 1);
## Points
	gl_PointSize = @PointSize;
##
}
)";

const std::string FragmentShaderTextTemplate =
R"(
@@Version

@@Uniform(float GAMMA);
@@Uniform(vec4 COLOUR);
@@Texture(sampler2D TEX1);

void main()
{
## Points
	vec2 uv = mix(@In(TEXCOORDS).xy, @In(TEXCOORDS).zw, vec2(gl_PointCoord.x, 1.0 - gl_PointCoord.y));
	float coverage = texture(@Texture(TEX1), uv).a;
## Else
	float coverage = texture(@Texture(TEX1), @In(TEXCOORDS)).a;
##
	vec4 textColour = @Uniform(COLOUR);
## Colours
	textColour *= @In(COLOUR);
##
	@Out(vec4 COLOUR) = vec4(
		pow(textColour.rgb, vec3(1.0 / @Uniform(GAMMA))),
		textColour.a * coverage);
}
)";

/*
 * Basic 2d shader.
 *
 */
const std::string VertexShader2dTemplate =
R"(
@@Version

void main()
{
## Points&Rotation
	vec2 d = normalize(@In(ROTATION).xy);
	@Out(mat4 TEXROTATION) =
	mat4(d.y, d.x, 0.0, 0.0,
		-d.x, d.y, 0.0, 0.0,
		0.0, 0.0, 1.0, 0.0,
		0.0, 0.0, 0.0, 1.0);
## Triangles&Rotation
	vec2 d = normalize(@In(ROTATION).xy);
	mat4 rotationMatrix =
	mat4(d.y, d.x, 0.0, 0.0,
		-d.x, d.y, 0.0, 0.0,
		0.0, 0.0, 1.0, 0.0,
		0.0, 0.0, 0.0, 1.0);
## Colour
	@Out(vec4 COLOUR) = @Vec4(@In(COLOUR));
## TexCoords2
	@Out(vec2 TEXCOORDS) = @In(TEXCOORDS);
## TexCoords4
	@Out(vec4 TEXCOORDS) = @In(TEXCOORDS);
## Triangles&Rotation
	// Rotate around TEXCOORDS.zw
	vec4 transVertex = @MCPMatrix * vec4(@In(POSITION).xy, 0, 1);
	vec4 transCentroid = @MCPMatrix * vec4(@In(POSITION).zw, 0, 1);
	vec2 offset = transVertex.xy - transCentroid.xy;
	offset = vec2(rotationMatrix * vec4(offset, 0.0, 1.0));
	transVertex.xy = transCentroid.xy + offset.xy;
## Else
	vec4 transVertex = @MCPMatrix * vec4(@In(POSITION).xy, 0, 1);
##
	transVertex.y = @HalfWindowSize.y * 2 - transVertex.y;
	vec2 centredPos = vec2(transVertex.x - @HalfWindowSize.x, transVertex.y - @HalfWindowSize.y);
	gl_Position = vec4(centredPos / @HalfWindowSize, 0, 1);
## Points
	gl_PointSize = @PointSize;
##
}
)";

const std::string FragmentShader2dTemplate =
R"(
@@Version

@@Uniform(float GAMMA);
## Diffuse
@@Uniform(vec4 DIFFUSE);
## Texture
@@Texture(sampler2D TEX1);
##

void main()
{
## Colour
	vec4 colour = @Vec4(@In(COLOUR));
## Else
	vec4 colour = vec4(1.0, 1.0, 1.0, 1.0);
##
## Diffuse
    colour *= @Uniform(DIFFUSE);
##

## !Points&Texture
	vec2 tc = @In(TEXCOORDS).st;
## Points&!Rotation&Texture&!Atlas
	// Use gl_PointCoord
	vec2 tc = gl_PointCoord;
## Points&!Rotation&Texture&Atlas
	// Use supplied texture coords for an atlas
	vec2 tc = mix(@In(TEXCOORDS).st, @In(TEXCOORDS).pq, vec2(gl_PointCoord.x, 1.0 - gl_PointCoord.y));

## Points&Rotation&Texture&!Atlas
	// Rotate a single image
	const vec2 offset = vec2(0.5, 0.5);
	vec2 tc = vec2(gl_PointCoord.x, 1.0 - gl_PointCoord.y);
	tc -= offset;
	tc = vec2(@In(TEXROTATION) * vec4(tc, 0.0, 1.0));
	tc += offset;
## Points&Rotation&Texture&Atlas
	// Rotate an image within an atlas
	const vec2 offset = vec2(0.5, 0.5);
	vec2 tc = vec2(gl_PointCoord.x, 1.0 - gl_PointCoord.y);
	tc -= offset;
	tc = vec2(@In(TEXROTATION) * vec4(tc, 0.0, 1.0));
	tc += offset;
	tc = vec2(mix(@In(TEXCOORDS).s, @In(TEXCOORDS).p, tc.s), mix(@In(TEXCOORDS).t, @In(TEXCOORDS).q, tc.t));
## Texture
	@Out(vec4 COLOUR) = texture(@Texture(TEX1), tc) * colour;
## Else
	@Out(vec4 COLOUR) = colour;
##

	@Out(COLOUR).rgb = pow(@Out(COLOUR).rgb, vec3(1.0 / @Uniform(GAMMA)));
}
)";

/*
 * Circle
 *
 */

const std::string VertexShader2dCircle =
R"(
@@Version

void main()
{
## Triangles
    @Out(vec4 OPTIONS) = vec4(@In(POSITION).zw, @In(OPTIONS).xy);
## Else
	@Out(vec4 OPTIONS) = vec4(0.0, 0.0, @In(OPTIONS).xy);
##

	@Out(vec4 BORDERCOLOUR) = @In(BORDERCOLOUR);
	@Out(vec4 INNERCOLOUR) = @Vec4(@In(INNERCOLOUR));

	vec4 transVertex = @MCPMatrix * vec4(@In(POSITION).xy, 0, 1);
	vec2 centredPos = vec2(transVertex.x - @HalfWindowSize.x, transVertex.y - @HalfWindowSize.y);
	gl_Position = vec4(centredPos / @HalfWindowSize, 0, 1);
}
)";

const std::string FragmentShader2dCircle =
R"(
@@Version

@@Uniform(float GAMMA);
## Diffuse
@@Uniform(vec4 DIFFUSE);
##

void main()
{
	vec4 borderColour = @In(BORDERCOLOUR);
	vec4 innerColour = @In(INNERCOLOUR);
	vec2 options = vec2(@In(OPTIONS).zw);

## Diffuse
	borderColour *= @Uniform(DIFFUSE);
	innerColour *= @Uniform(DIFFUSE);
##

## Points
	vec2 tc = gl_PointCoord;
## Else
	vec2 tc = @In(OPTIONS).xy;
##
	tc -= 0.5;

	float outerBorder = 0.25;
	float innerBorder = (options.x - options.y) / (options.x * 4);

	float d = dot(tc, tc);
	if (d <= innerBorder)
	{
		@Out(vec4 COLOUR) = innerColour;
	}
	else if (d <= outerBorder)
	{
		@Out(COLOUR) = borderColour;
	}
	else
	{
		@Out(COLOUR) = vec4(0.0);
	}

	@Out(COLOUR).rgb = pow(@Out(COLOUR).rgb, vec3(1.0 / @Uniform(GAMMA)));
}
)";

const std::string FragmentShader2dCircleAntialiased =
R"(
@@Version

@@Uniform(float GAMMA);
## Diffuse
@@Uniform(vec4 DIFFUSE);
##

void main()
{
	vec4 borderColour = @In(BORDERCOLOUR);
	vec4 innerColour = @In(INNERCOLOUR);
	vec2 options = vec2(@In(OPTIONS).zw);

## Diffuse
	borderColour *= @Uniform(DIFFUSE);
	innerColour *= @Uniform(DIFFUSE);
##

## Points
	vec2 tc = gl_PointCoord;
## Else
	vec2 tc = @In(OPTIONS).xy;
##
	tc -= 0.5;

	float outerBorder = 0.25;
	float innerBorder = (options.x - options.y) / (options.x * 4);
	float alphaBorder = outerBorder - (outerBorder * 2 / options.x);

	float d = dot(tc, tc);
	if (d <= innerBorder)
	{
		@Out(vec4 COLOUR) = innerColour;
	}
	else if (d <= outerBorder)
	{
		if (d < alphaBorder)
		{
			@Out(COLOUR) = borderColour;
		}
		else
		{
			float blend = 1.0 - (d - alphaBorder) / (outerBorder - alphaBorder);
			@Out(COLOUR) = vec4(borderColour.xyz, blend);
		}
	}
	else
	{
		@Out(COLOUR) = vec4(0.0);
	}

	@Out(COLOUR).rgb = pow(@Out(COLOUR).rgb, vec3(1.0 / @Uniform(GAMMA)));
}
)";