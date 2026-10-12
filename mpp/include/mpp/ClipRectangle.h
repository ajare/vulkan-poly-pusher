#pragma once

#include "mpp/Config.h"

namespace mpp
{
	struct _MPPAPI ClipRectangle
	{
		int x, y, width, height;

	public:

		ClipRectangle();

		ClipRectangle(int _x, int _y, int _width, int _height);

		// Returns the half-open intersection [x, x + width) × [y, y + height).
		// Negative dimensions are normalized before intersection. Disjoint or
		// edge-touching rectangles produce zero width and height.
		ClipRectangle intersect(ClipRectangle const& clipper) const;
	};
}
