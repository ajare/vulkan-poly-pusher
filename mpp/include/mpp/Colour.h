#pragma once

#include <cstdint>
#include <format>
#include <string>

#include "mpp/Config.h"

#pragma pack(push, 1)

namespace mpp
{

	struct _MPPAPI Colour
	{
		float red, green, blue, alpha;

	public:

		Colour()
			: red(1.0f)
			, green(1.0f)
			, blue(1.0f)
			, alpha(1.0f)

		{
		}

		Colour(float _red, float _green, float _blue, float _alpha = 1.0f)
			: red(_red)
			, green(_green)
			, blue(_blue)
			, alpha(_alpha)
		{
		}

		void toGreyscale()
		{
			float grey = red * 0.3f + green * 0.595f + blue * 0.105f;
			red = green = blue = grey;
		}

		Colour trans(float alphaMod)
		{
			return Colour(red, green, blue, alpha * alphaMod);
		}

		float operator[](int index) const
		{
			return *((float*)&red + index);
		}

		Colour operator*(float value) const
		{
			return Colour(red * value, green * value, blue * value, alpha);
		}

		Colour operator*(int value) const
		{
			return Colour(red * value, green * value, blue * value, alpha);
		}

		Colour operator/(float value) const
		{
			return Colour(red / value, green / value, blue / value, alpha);
		}

		Colour operator/(int value) const
		{
			return Colour(red / value, green / value, blue / value, alpha);
		}

		Colour& operator*=(float value)
		{
			red *= value;
			green *= value;
			blue *= value;

			return *this;
		}

		Colour& operator/=(float value)
		{
			red /= value;
			green /= value;
			blue /= value;

			return *this;
		}

		std::string toStringFloat(std::string const& start, std::string const& end, std::string const& sep) const
		{
			return std::format("{0}{3}{1}{4}{1}{5}{1}{6}{2}", start, sep, end, red, green, blue, alpha);
		}

		std::string toStringUint8(std::string const& start, std::string const& end, std::string const& sep) const
		{
			return std::format("{0}{3}{1}{4}{1}{5}{1}{6}{2}", start, sep, end, (uint8_t)(red * 255.0f), (uint8_t)(green * 255.0f), (uint8_t)(blue * 255.0f), (uint8_t)(alpha * 255.0f));
		}

		std::string toStringHex(std::string const& start, std::string const& end, std::string const& sep) const
		{
			const char* hexChars = "0123456789ABCDEF";

			uint8_t r = (uint8_t)(red * 255.0f);
			uint8_t g = (uint8_t)(green * 255.0f);
			uint8_t b = (uint8_t)(blue * 255.0f);
			uint8_t a = (uint8_t)(alpha * 255.0f);

			std::string rh("  "), gh("  "), bh("  "), ah("  ");

			rh[0] = hexChars[r / 16];
			rh[1] = hexChars[r % 16];
			gh[0] = hexChars[g / 16];
			gh[1] = hexChars[g % 16];
			bh[0] = hexChars[b / 16];
			bh[1] = hexChars[b % 16];
			ah[0] = hexChars[a / 16];
			ah[1] = hexChars[a % 16];

			return std::format("{0}{3}{1}{4}{1}{5}{1}{6}{2}", start, sep, end, rh, gh, bh, ah);
		}

		static Colour Black;
		static Colour White;
		static Colour Grey75;
		static Colour Grey50;
		static Colour Grey25;
		static Colour Red;
		static Colour Green;
		static Colour Blue;
		static Colour Yellow;
		static Colour Cyan;
		static Colour Magenta;
		static Colour Orange;
	};

}

#pragma pack(pop)
