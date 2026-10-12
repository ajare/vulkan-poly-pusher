#pragma once

namespace mpp
{

	struct RenderInfo
	{
		int batchCount{ 0 };
		int programSwitches{ 0 };
		int textureSwitches{ 0 };
		int primitivesRendered{ 0 };
		int trianglesRendered{ 0 };
		int fullscreenQuads{ 0 };

	public:

		void clear()
		{
			batchCount = 0;
			programSwitches = 0;
			textureSwitches = 0;
			primitivesRendered = 0;
			trianglesRendered = 0;
			fullscreenQuads = 0;
		}
	};

}

