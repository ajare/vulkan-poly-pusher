#include <iterator>

#include "mpp/ProgramStream.h"
#include "mpp/MppException.h"

using namespace std;

namespace mpp
{

	/*
 	 * Constructor.
	 *
	 */
	ProgramStream::ProgramStream(ResourceManager* resourceMgr)
		: ResourceStream(resourceMgr, "Program")
	{
	}

	/*
	 * Set vertex shader source.
	 *
	 */
	void ProgramStream::setVertexSource(string const& src)
	{
		mVertexSource = src;
	}

	/*
	 * Set geometry shader source.
	 *
	 */
	void ProgramStream::setGeometrySource(string const& src)
	{
		mGeometrySource = src;
	}

	/*
	* Set fragment shader source.
	*
	*/
	void ProgramStream::setFragmentSource(string const& src)
	{
		mFragmentSource = src;
	}

	/*
	 * Get vertex shader source.
	 *
	 */
	string const& ProgramStream::getVertexSource() const
	{
		return mVertexSource;
	}

	/*
	 * Get geometry shader source.
	 *
	 */
	string const& ProgramStream::getGeometrySource() const
	{
		return mGeometrySource;
	}

	/*
	 * Get fragment shader source.
	 *
	 */
	string const& ProgramStream::getFragmentSource() const
	{
		return mFragmentSource;
	}

	void ProgramStream::setFragmentPreamble(string const& preamble)
	{
		mFragmentPreamble = preamble;
	}

	/*
	 * Get all source concatenated 
	 *
	 */
	string ProgramStream::getConcatenatedSource()
	{
		// Length-prefix each section so boundaries cannot collide (for example, a
		// suffix moved from one shader stage to the next). The mesh section is the
		// full canonical layout key, not its compact 32-bit digest.
		auto appendSection = [](string& key, char const* name, string const& value)
		{
			key += name;
			key += '=' + to_string(value.size()) + ':';
			key += value;
		};
		string key;
		appendSection(key, "mesh", getMeshSpecification().getHashString());
		appendSection(key, "vertex", getVertexSource());
		appendSection(key, "geometry", getGeometrySource());
		appendSection(key, "fragment", getFragmentSource());
		return key;
	}

	mesh::MeshSpecification const& ProgramStream::getMeshSpecification() const
	{
		auto parser = mParser;
		return parser->getMeshSpecification();
	}

	/*
	 * Load.
	 *
	 */
	void ProgramStream::loadImpl()
	{
		auto parser = mParser;
		if (!mFragmentPreamble.empty())
		{
			auto source = parser->getInputFragmentSource();
			if (source.find(mFragmentPreamble) == string::npos)
			{
				auto marker = source.find("@@Version");
				if (marker == string::npos) THROW_MPP("Fragment shader preamble requires an @@Version directive.", __LINE__, __FILE__, __func__);
				auto insertion = source.find('\n', marker);
				source.insert(insertion == string::npos ? source.size() : insertion + 1, mFragmentPreamble);
				parser->setFragmentSource(source);
			}
		}

		parser->build(mAttribs);

		setVertexSource(parser->getGeneratedVertexSource());
		setFragmentSource(parser->getGeneratedFragmentSource());
	}

	vector<program::Attribute> ProgramStream::getInAttributes() const
	{
		auto parser = mParser;
		return parser->getInAttributes();
	}

	vector<string> ProgramStream::getUniforms() const
	{
		auto parser = mParser;
		return parser->getUniforms();
	}

	vector<string> ProgramStream::getTextures() const
	{
		auto parser = mParser;
		return parser->getTextures();
	}
}