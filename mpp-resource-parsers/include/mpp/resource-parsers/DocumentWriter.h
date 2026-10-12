#pragma once

#include <string>

#include "Config.h"
#include "mpp/data/StructuredData.h"

namespace mpp::resource_parsers::detail
{
	// Atomically writes a document tree to filepath, picking XML or YAML by
	// filepath's extension (via the same FileStream factory list). Writes to
	// filepath + ".tmp" first and renames over the destination, matching the
	// atomic-write behaviour each hand-written *Serializer previously
	// duplicated for XML specifically.
	void _MPPRESOURCEPARSERSAPI writeDocument(mpp::data::StructuredData const& root, std::string const& filepath);
}
