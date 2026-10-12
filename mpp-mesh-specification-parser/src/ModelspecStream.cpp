#if defined(_MSC_VER) && _MSC_VER < 1930
#  include <vld.h> // Memory tracking
#endif

#include <stdexcept>

#include "utils/StringUtils.h"

#include "mpp/ProgrammaticBasicMaterialStream.h"
#include "mpp/ResourceStreamSerializer.h"

#include "mpp/resource-parsers/MeshSpecificationParser.h"
#include "mpp/resource-parsers/FileBasicMaterialStream.h"
#include "mpp/resource-parsers/FilePbrMaterialStream.h"

#include "mpp/mesh-specification-parser/ModelspecStream.h"

namespace mpp
{
	namespace mesh_specification_parser
	{

		using namespace std;

		ModelspecStream::ModelspecStream(string const& filepath)
			: FileStream(filepath)
		{
		}

		void ModelspecStream::load()
		{
			auto const& data = getStructuredData();

			// Parse data.  Root element should be 'ModelSpecification'
			auto rootName = data.getName();

			if (rootName != "ModelSpecification" && rootName != "Resource")
			{
				string errMsg = "Error loading " + getFilepath() + ".  Root element is neither 'ModelSpecification' nor 'Resource'.";
				throw runtime_error(errMsg);
			}

			for (auto it = data.begin(); it != data.end(); ++it)
			{
				auto const& entry = *it;
				string value = utils::StringUtils::toUpper(entry.second.getValue());

				if (entry.first == "MeshSpecification")
				{
					resource_parsers::MeshSpecificationParser parser(getFilepath());
					mMeshSpec = parser.parse(entry.second);
				}
				else if (entry.first == "Materials")
				{
					auto const& materials = entry.second;
					for (auto mit = materials.begin(); mit != materials.end(); ++mit)
					{
						auto const& mentry = *mit;
						if (mentry.first == "Material" || mentry.first == "BasicMaterial" || mentry.first == "PbrMaterial")
						{
							// Legacy Material is BasicMaterial compatibility input. New ModelSpecs
							// select the concrete stream directly with their element tag.
							auto name = mentry.second.getEntry("name").getValue();
							ResourceStreamPtr mstream;
							if (mentry.first == "PbrMaterial")
								mstream = make_shared<resource_parsers::FilePbrMaterialStream>(nullptr, getFilepath(), mentry.second, mMeshSpec, false);
							else
							{
								auto const& data = mentry.first == "Material" ? mentry.second.getEntry("Resource") : mentry.second;
								mstream = make_shared<resource_parsers::FileBasicMaterialStream>(nullptr, getFilepath(), data, mMeshSpec, false);
							}
							mstream->load();
							
							// Serialize the materialstream

							// Add material
							if (mMaterials.find(name) != mMaterials.end())
							{
								string errMsg = "Error loading " + getFilepath() + ".  Duplicate material named '" + name + "' specified.";
								throw runtime_error(errMsg);
							}

							mMaterials[name] = mstream;
						}
					}
				}
			}
		}

		mesh::MeshSpecification const& ModelspecStream::getMeshSpecification() const
		{
			return mMeshSpec;
		}

		map<string, ResourceStreamPtr> const& ModelspecStream::getMaterials() const
		{
			return mMaterials;
		}

		void ModelspecStream::serialize(ofstream& fp)
		{
			// Write all materials using shared functions from ResourceStreamSerializer
			mpp::ResourceStreamSerializer ser(nullptr);

			for (auto const& material: mMaterials)
			{
				ser.serialize(material.second, fp);
			}
		}
	}

}