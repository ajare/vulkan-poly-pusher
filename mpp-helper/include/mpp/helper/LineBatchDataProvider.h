#pragma once

#include <mpp/BatchDataProvider.h>
#include <mpp/RenderSystem.h>
#include <mpp/ResourceManager.h>

#include <mpp/mesh/VertexTypeSpecification.h>

#include "Config.h"

/*
There are two classes:
- LineBatchDataProvider
- LineBatchRenderer

LineBatchDataProvider is templated with a required position type and an optional colour type.
There is a specialisation for no colour which does not provide a colour() function.

To render a line batch, you create a subclass of LineBatchDataProvider, eg as follows:

class MyDataProvider : public LineBatchDataProvider<mpp::mesh::DataTypeFloat, mpp::mesh::DataTypeUnsignedByte>

Or, if you want to be able to specify types:

template<typename PosType, typename ColType = mpp::mesh::DataTypeNone>
class MyDataProvider : public LineBatchDataProvider<PosType, ColType> {};

// Specialization (to be used) for our data provider
template<>
class MyDataProvider<mpp::mesh::DataTypeFloat, mpp::mesh::DataTypeUnsignedByte>	: public LineBatchDataProvider<mpp::mesh::DataTypeFloat, mpp::mesh::DataTypeUnsignedByte>
{
	// ...
};

The above means that you will need to create a custom implementation for each specialization, of course.

You then need to implement the position() and colour() functions.  A typical way to do this is to precalculate the line data, or update it in the update() function, and
then just read off the indexed value from a vector stored in the class, for instance:

class MyDataProvider : public LineBatchDataProvider<mpp::mesh::DataTypeFloat, mpp::mesh::DataTypeUnsignedByte>
{
	struct Line
	{
		float x[2], y[2];
		uint8_t colour[4];
	};

private:

	std::shared_ptr<DataObject> mData;

	std::vector<Line> mLines;

	bool mDirty{ true };

public:

	explicit MyDataProvider(std::shared_ptr<DataObject> data)
		: mData(data)
	{
		update(0.0f);
	}

	void position(uint32_t index, float& x0, float& y0, float& x1, float& y1)
	{
		x0 = mLines[index].x[0];
		y0 = mLines[index].y[0];
		x1 = mLines[index].x[1];
		y1 = mLines[index].y[1];
	}

	void colour(uint32_t index, uint8_t& red, uint8_t& green, uint8_t& blue, uint8_t& alpha)
	{
		red = mLines[index].colour[0];
		green = mLines[index].colour[1];
		blue = mLines[index].colour[2];
		alpha = mLines[index].colour[3];
	}

	mpp::Colour diffuse()
	{
		return mpp::Colour::White;
	}

	void setDirty()
	{
		mDirty = true;
	}

	void update(float frameTime)
	{
		if (mDirty)
		{
			mLines.clear();

			// Go through mDataObject and fill in mLines
			// ...

			setNumLines(mLines.size());
			mDirty = false;
		}
	}
};
*/

namespace mpp
{
	namespace helper
	{

		// Base data provider classes: one for colour data, and one specialization where it's set to None
		template<typename PosType, typename ColType = mpp::mesh::DataTypeNone>
		class LineBatchDataProvider : public BatchDataProvider
		{
		public:

			virtual void position(uint32_t index, typename PosType::builtin_type& x0, typename PosType::builtin_type& y0, typename PosType::builtin_type& x1, typename PosType::builtin_type& y1) = 0;

			virtual void colour(uint32_t index, typename ColType::builtin_type& red, typename ColType::builtin_type& green, typename ColType::builtin_type& blue, typename ColType::builtin_type& alpha) = 0;

			virtual mpp::Colour diffuse() = 0;
		};

		template<typename PosType>
		class LineBatchDataProvider<PosType, mpp::mesh::DataTypeNone> : public BatchDataProvider
		{
		public:

			virtual void position(uint32_t index, typename PosType::builtin_type& x0, typename PosType::builtin_type& y0, typename PosType::builtin_type& x1, typename PosType::builtin_type& y1) = 0;

			virtual mpp::Colour diffuse() = 0;
		};

	}
}