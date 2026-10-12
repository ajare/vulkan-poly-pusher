#include <algorithm>
#include <cstdint>
#include <limits>

#include "mpp/ClipRectangle.h"

using namespace std;

namespace mpp
{

	/*
	 * Default constructor.
	 *
	 */
	ClipRectangle::ClipRectangle()
		: x(0)
		, y(0)
		, width(0)
		, height(0)
	{
	}

	/*
	 * Constructor.
	 *
	 */
	ClipRectangle::ClipRectangle (int _x, int _y, int _width, int _height)
		: x(_x)
		, y(_y)
		, width(_width)
		, height(_height)	
	{
	}

	/*
	 * Clip the rectangle against another, producing a refined clipping area.
	 *
	 */
	ClipRectangle ClipRectangle::intersect(ClipRectangle const& clipper) const
	{
		struct Bounds
		{
			int64_t left, top, right, bottom;
		};
		auto normalize = [](ClipRectangle const& rectangle)
		{
			auto const x0 = static_cast<int64_t>(rectangle.x);
			auto const y0 = static_cast<int64_t>(rectangle.y);
			auto const x1 = x0 + static_cast<int64_t>(rectangle.width);
			auto const y1 = y0 + static_cast<int64_t>(rectangle.height);
			return Bounds{ min(x0, x1), min(y0, y1), max(x0, x1), max(y0, y1) };
		};

		auto const source = normalize(*this);
		auto const other = normalize(clipper);
		auto const left = max(source.left, other.left);
		auto const top = max(source.top, other.top);
		auto const right = min(source.right, other.right);
		auto const bottom = min(source.bottom, other.bottom);

		auto clampCoordinate = [](int64_t value)
		{
			return static_cast<int>(clamp(value,
				static_cast<int64_t>(numeric_limits<int>::min()),
				static_cast<int64_t>(numeric_limits<int>::max())));
		};
		if (right <= left || bottom <= top)
		{
			return ClipRectangle(clampCoordinate(left), clampCoordinate(top), 0, 0);
		}

		// Coordinates and dimensions are public ints, while endpoint arithmetic
		// may span more than INT_MAX. Keep the representable portion and saturate
		// dimensions instead of overflowing into a negative glScissor size.
		auto const representableLeft = max(left, static_cast<int64_t>(numeric_limits<int>::min()));
		auto const representableTop = max(top, static_cast<int64_t>(numeric_limits<int>::min()));
		if (representableLeft > numeric_limits<int>::max() || representableTop > numeric_limits<int>::max())
		{
			return ClipRectangle(clampCoordinate(left), clampCoordinate(top), 0, 0);
		}
		auto const width = min(right - representableLeft, static_cast<int64_t>(numeric_limits<int>::max()));
		auto const height = min(bottom - representableTop, static_cast<int64_t>(numeric_limits<int>::max()));
		if (width <= 0 || height <= 0)
		{
			return ClipRectangle(clampCoordinate(left), clampCoordinate(top), 0, 0);
		}
		return ClipRectangle(static_cast<int>(representableLeft), static_cast<int>(representableTop), static_cast<int>(width), static_cast<int>(height));
	}

}