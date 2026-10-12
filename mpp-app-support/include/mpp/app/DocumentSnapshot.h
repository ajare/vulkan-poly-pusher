#pragma once

#include <cstdint>
#include <memory>
#include <utility>

namespace mpp::app
{
	template<typename T>
	class DocumentSnapshot
	{
		uint64_t mGeneration{ 0 };
		std::shared_ptr<T const> mValue;

	public:
		DocumentSnapshot() = default;

		DocumentSnapshot(uint64_t generation, std::shared_ptr<T const> value)
			: mGeneration(generation), mValue(std::move(value))
		{
		}

		uint64_t generation() const { return mGeneration; }

		std::shared_ptr<T const> const& value() const { return mValue; }

		explicit operator bool() const { return static_cast<bool>(mValue); }
	};

	class DocumentGeneration
	{
		uint64_t mCurrent{ 0 };

	public:
		uint64_t current() const { return mCurrent; }

		uint64_t next() { return ++mCurrent; }

		bool isCurrent(uint64_t generation) const { return generation == mCurrent; }
	};
}
