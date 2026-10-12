#pragma once

#include <string>

#include "mpp/Config.h"
#include "mpp/data/StructuredData.h"
#include "mpp/Diagnostic.h"
#include "mpp/PbrMaterialSpecification.h"

namespace mpp
{
	// Converts one <PbrMaterial> resource definition to a <BasicMaterial>
	// definition, per the fidelity policy in
	// doc/implemented/LEGACY_PIPELINE_EXPORT_PLAN.md section 4. Metallic/roughness/normal/
	// occlusion/emissive maps and factors, non-Opaque alpha modes, and
	// doubleSided are dropped with Warning diagnostics; a base-colour texture
	// is carried across as TEX1, and a flat baseColourFactor with no texture
	// is baked into a small generated image (written under
	// `bakedTextureDirectory`, named "<name>.BaseColour.png") and used as
	// TEX1 instead.
	//
	// `sourceDefinition` is the raw <PbrMaterial> XML tree (used to copy the
	// Program subtree through verbatim -- no spec-to-XML writer exists
	// anywhere in this codebase for Program/MeshSpecification data, so the
	// only reliable way to carry a material's program across is copying its
	// already-authored XML). `programIsDefault` must be resolved by the
	// caller: it alone has visibility into any separately-referenced Program
	// resource (a <Program><Ref>name</Ref></Program> requires looking that
	// resource up elsewhere in the pipeline document). When false, this
	// throws mpp::Exception and appends an Error diagnostic first -- a
	// hand-written PBR shader cannot be mechanically translated to the
	// legacy contract.
	_MPPAPI mpp::data::StructuredData convertPbrMaterialToBasic(
		std::string const& name,
		PbrMaterialSpecification const& source,
		mpp::data::StructuredData const& sourceDefinition,
		bool programIsDefault,
		std::string const& bakedTextureDirectory,
		DiagnosticBag& diagnostics);
}
