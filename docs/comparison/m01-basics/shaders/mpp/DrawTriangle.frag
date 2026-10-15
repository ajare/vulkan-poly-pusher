@@Version
@@Uniform(vec4 TINT);
void main()
{
    @Out(vec4 COLOUR) = @In(COLOUR) * @Uniform(TINT);
}
