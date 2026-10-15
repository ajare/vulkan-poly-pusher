@@Version
@@Uniform(mat4 TRANSFORM);
void main()
{
    @Out(vec4 COLOUR) = @In(COLOUR);
    gl_Position = @Uniform(TRANSFORM) * vec4(@In(POSITION), 1);
    gl_Position.z = (gl_Position.z + gl_Position.w) * 0.5;
}
