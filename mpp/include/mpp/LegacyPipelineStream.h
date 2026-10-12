#pragma once
#include <memory>
#include "mpp/Config.h"
#include "mpp/LegacyPipelineDocument.h"
#include "mpp/ResourceStream.h"
namespace mpp { class _MPPAPI LegacyPipelineStream : public ResourceStream { std::shared_ptr<LegacyPipelineDocument> mDocument; protected: void loadImpl() override {} public: explicit LegacyPipelineStream(ResourceManager* resourceMgr); void setDocument(std::shared_ptr<LegacyPipelineDocument> document); std::shared_ptr<LegacyPipelineDocument> const& getDocument() const; }; }
