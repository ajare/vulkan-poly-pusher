#include <cassert>

#include "mpp/Config.h"
#include "mpp/Font.h"

using namespace std;

namespace mpp
{
	/*
	 * Constructor.
	 *
	 */
	Font::Font(ResourcePtr texture)
		: ResourceWrangler("Font")
		, mTexture(texture)
	{
		mTexture->acquire(this);
	}

	Font::~Font()
	{
		mTexture->release(this);
	}

	/*
	 * Set a glyph.
	 *
	 */
	void Font::setTexture(ResourcePtr texture)
	{
		mTexture->release(this);
		mTexture = texture;
		mTexture->acquire(this);
	}

	/*
	 * Set a glyph.
	 *
	 */
	void Font::setGlyph(uint8_t index, int x, int y, int width, int height, int kern, int raise)
	{
		Glyph& g = mGlyphs[index];
		
		g.x = x;
		g.y = y;
		g.width = width;
		g.height = height;
		g.kern = kern;
		g.raise = raise;

		float textureWidth = (float)((Texture&)*mTexture).getWidth();
		float textureHeight = (float)((Texture&)*mTexture).getHeight();

		g.u0_ = g.x / textureWidth;
		g.v0_ = g.y / textureHeight;
		g.u1_ = (g.x + g.width) / textureWidth;
		g.v1_ = (g.y + g.height) / textureHeight;
	}

	/*
	* Get a glyph.
	*
	*/
	Font::Glyph const& Font::getGlyph(uint8_t index)
	{
		return  mGlyphs[index];
	}

	/*
	* Get the glyph texture.
	*
	*/
	ResourcePtr Font::getTexture()
	{
		return mTexture;
	}

}