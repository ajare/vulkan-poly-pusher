@@Version

void main()
{
	@Out(vec3 NORMAL) = normalize(@NormalMatrix * @Vec3(@In(NORMAL)));

	vec4 vertPos = @MCPMatrix * @Vec4(@In(POSITION));
	
	@Out(vec3 POSITION) = vertPos.xyz;
	@Out(vec2 TEXCOORDS) = @In(TEXCOORDS);
	@Out(vec4 COLOUR) = @In(COLOUR);
	
	gl_Position = vertPos;
}
