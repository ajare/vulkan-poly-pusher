#pragma once

#include <vector>
#include <cassert>

#include "mpp/Config.h"

namespace mpp
{
	template<class T>
	class Pool
	{
		std::vector<T*> mObjects;

		size_t mCount;

	private:

		void resize(size_t newSize)
		{
			auto curSize = mObjects.size();

			if (newSize > curSize)
			{
				mObjects.resize(newSize);
				for (size_t i = curSize; i < newSize; ++i)
				{
					mObjects[i] = new T();
				}
			}
		}

	public:

		explicit Pool(size_t initialSize)
			: mCount(0)
		{
			resize(initialSize);
		}

		~Pool()
		{
			for (auto obj : mObjects)
			{
				delete obj;
			}

			mObjects.clear();
		}

		size_t getCount() const
		{
			return mCount;
		}

		size_t getCapacity() const
		{
			return mObjects.size();
		}

		T* acquireObject()
		{
			auto count = getCount();
			auto curSize = getCapacity();

			if (count >= curSize)
			{
				auto newSize = curSize;
				while (newSize <= count)
				{
					newSize *= 3;
					newSize /= 2;
					newSize += 8;
				}

				resize(newSize);
			}

			mCount++;
			return mObjects[count];
		}

		T* getObject(uint32_t index)
		{
			assert(index < getCount());
			return mObjects[index];
		}

		T const* getObject(uint32_t index) const
		{
			assert(index < getCount());
			return mObjects[index];
		}

		void releaseAllObjects()
		{
			while (mCount > 0)
			{
				mObjects[--mCount]->release();
			}
		}
	};
}

