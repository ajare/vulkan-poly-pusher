#pragma once
#include <memory>
#include "mpp/Config.h"
#include "mpp/PbrPipelineDocument.h"
#include "mpp/ResourceStream.h"
namespace mpp { class _MPPAPI PbrPipelineStream : public ResourceStream { std::shared_ptr<PbrPipelineDocument> mDocument; protected: void loadImpl() override {} public: explicit PbrPipelineStream(ResourceManager* resourceMgr); void setDocument(std::shared_ptr<PbrPipelineDocument> document); std::shared_ptr<PbrPipelineDocument> const& getDocument() const; }; }
