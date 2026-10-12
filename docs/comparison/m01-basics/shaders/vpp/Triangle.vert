@@Version
void main()
{
    vec3 normal = @NormalMatrix * vec3(0, 0, 1);
    vec3 world = (@MMatrix * vec4(@In(POSITION), 1)).xyz;
    float light = 0.5 + 0.5 * abs(dot(normalize(@ViewPos - world), normal));
    @Out(vec4 COLOUR) = @In(COLOUR) * vec4(vec3(light), 1);
    gl_Position = @MCPMatrix * vec4(@In(POSITION), 1);
    // Vulkan clip depth is [0,w]; client transforms retain MPP's [-w,w].
    gl_Position.z = (gl_Position.z + gl_Position.w) * 0.5;
}
