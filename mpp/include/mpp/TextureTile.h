#pragma once

#include "mpp/Resource.h"
#include "mpp/Texture.h"

namespace mpp
{
	struct _MPPAPI TextureTile
	{
		float u[2], v[2];
		int width, height;
		ResourcePtr texture;

		TextureTile()
			: width(0)
			, height(0)
		{
			u[0] = 0;
			u[1] = 1;
			v[0] = 0;
			v[1] = 1;
		}

		TextureTile(ResourcePtr tex, float u0, float v0, float u1, float v1)
		{
			u[0] = u0;
			v[0] = v0;
			u[1] = u1;
			v[1] = v1;
			texture = tex;

			auto rawTexture = (Texture*)tex.get();
			width = (int)((u1 - u0) * rawTexture->getWidth());
			height = (int)((v1 - v0) * rawTexture->getHeight());
		}
	};
}
