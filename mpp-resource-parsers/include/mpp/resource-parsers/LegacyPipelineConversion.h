#pragma once

#include <string>

#include "Config.h"
#include "mpp/Diagnostic.h"
#include "mpp/LegacyPipelineDocument.h"
#include "mpp/PbrPipelineDocument.h"

namespace mpp::resource_parsers
{
	// Document-level PBR -> Legacy pipeline conversion (doc/implemented/LEGACY_PIPELINE_EXPORT_PLAN.md
	// section 4-5). Lives here rather than in mpp core because it needs
	// FilePbrMaterialStream::parseDefinition to turn each PbrMaterial
	// resource's XML into a PbrMaterialSpecification before handing it to
	// mpp::convertPbrMaterialToBasic -- that parsing capability only exists
	// in mpp-resource-parsers, which depends on mpp core, not the reverse.
	//
	// `bakedTextureDirectory` is where any generated flat-colour textures
	// (see mpp::convertPbrMaterialToBasic) are written; callers exporting a
	// package should point this at a location their asset-localization step
	// will pick up like any other authored texture file.
	_MPPRESOURCEPARSERSAPI LegacyPipelineDocument convertPbrPipelineToLegacy(
		PbrPipelineDocument const& source,
		std::string const& bakedTextureDirectory,
		DiagnosticBag& diagnostics);
}
