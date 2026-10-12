#pragma once

#include <map>

#include "mpp/Resource.h"

namespace mpp
{
	class _MPPAPI String : public Resource
	{
		std::string mData;

	protected:

		void createImpl() {}

		void destroyImpl() {}

		void loadImpl();

		void unloadImpl();

	public:

		String(std::string const& name, RenderSystem* renderSystem, ResourceManager* resourceMgr, ResourceStreamPtr resourceStream);

		~String();

		std::string const& getData();
	};
}
