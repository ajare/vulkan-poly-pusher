#pragma once

#include <vector>

namespace mpp
{

    class InternalFont
    {
        unsigned char* mData;

        int mRawDataLength;

		int mWidth, mHeight;

	private:

		int decodePNG(std::vector<unsigned char>& out_image, int& image_width, int& image_height, const unsigned char* in_png, size_t in_size, bool convert_to_rgba32 = true);

    public:

        /*
         * Constructor.
         *
         */
        InternalFont();

        /*
         * Destructor.
         *
         */
        ~InternalFont();

        /*
         * Get image width.
         *
         */
        int getWidth() const
        {
			return mWidth;
        }

        /*
         * Get image height.
         *
        */
        int getHeight() const
        {
			return mHeight;
        }

        /*
         * Get raw image data.
         *
         */
        unsigned char* getData() const
        {
            return mData;
        }
    };

}
