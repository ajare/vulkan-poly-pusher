#pragma once
#include <memory>
#include "mpp/Config.h"
#include "mpp/PbrPipelineDocument.h"
#include "mpp/Resource.h"
namespace mpp { class _MPPAPI PbrPipelineTemplate : public Resource { std::shared_ptr<PbrPipelineDocument> mDocument; protected: void createImpl() override; void destroyImpl() override; void loadImpl() override{} void unloadImpl() override{} public: PbrPipelineTemplate(std::string const&name,RenderSystem*rs,ResourceManager*rm,ResourceStreamPtr stream); std::shared_ptr<PbrPipelineDocument> const& getDocument()const; }; }
