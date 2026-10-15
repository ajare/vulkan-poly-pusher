// Native Vulkan UniformCollection binding (#95): retained sparse-override
// semantics implemented by packing values into the program's generated
// `_mpp_Uniforms` block image (std140, reflected offsets) and staging the
// image into the descriptor-backed per-frame-context uniform buffers.
#include "mpp/UniformCollection.h"
#include "mpp/Program.h"
#include "mpp/MppException.h"

#include "WindowBridge.h"
#include "PipelineStore.h"
#include "UniformBlockWrite.h"

namespace mpp
{
	void UniformCollection::bindUniforms(ResourcePtr program)
	{
		Program* p = static_cast<Program*>(program.get());
		if (!p || p->getId() == 0)
			THROW_MPP("Cannot bind uniforms through a program that is not loaded.", __LINE__, __FILE__, __func__);

		auto& store = detail::associatedWindow()->pipelines();
		auto const& members = store.uniformMemberTable(p->getId());
		bool changed = false;
		for (auto const& it : mUniformData)
		{
			auto const& ud = it.second;
			// Missing uniforms are no-ops: pass-scoped overrides stay sparse.
			changed |= detail::writeUniformValue(members, store.uniformImage(p->getId()),
				MPP_PROGRAM_MARKUP_UNIFORM(it.first), ud.type, ud.count, ud.numElements, ud.data, p->getName());
		}
		if (changed) store.flushUniforms(p->getId());
	}
}
