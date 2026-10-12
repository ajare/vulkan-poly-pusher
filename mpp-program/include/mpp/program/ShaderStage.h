#pragma once

#include <string>
#include <vector>
#include <algorithm>

#include "Attribute.h"
#include "Uniform.h"
#include "Texture.h"

namespace mpp
{
	namespace program
	{

		struct ShaderStage
		{
			enum class Type
			{
				Vertex,
				Geometry,
				Fragment,
				NumStages,
			};

			Type type;
			std::string inputSource, source;
			std::string generated;
			int mainLine{ -1 };

			std::vector<Attribute> inAttribs, outAttribs;
			std::vector<Uniform> uniforms;
			std::vector<Texture> textures;

		public:

			void clear();

			bool required() const;

			bool provided() const;

			bool inAttributeExists(std::string const& attrib) const;

			bool outAttributeExists(std::string const& attrib) const;

			size_t getVariableSize(std::string const& attrib) const;

			mesh::Vertex::Component getVariableComponent(std::string const& attrib) const;
		};

	}
}