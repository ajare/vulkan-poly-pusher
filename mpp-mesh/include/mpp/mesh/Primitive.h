#pragma once

namespace mpp
{
	namespace mesh
	{

		struct Primitive
		{
			enum class Type
			{
				Points,
				Lines,
				Triangles,
			};

		public:

			static size_t size(Type type)
			{
				switch (type)
				{
				case Type::Points:
					return 1;

				case Type::Lines:
					return 2;

				case Type::Triangles:
					return 3;

				default:
					return 0;
				}
			}
		};

	}
}