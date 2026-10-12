#pragma once

namespace mpp
{
	struct Vertex2d
	{
		float x, y;
		float u, v;
		float r, g, b, a;

		enum class RenderType
		{
			None,
			Points,
			Lines,
			Triangles,
		};
	};

}