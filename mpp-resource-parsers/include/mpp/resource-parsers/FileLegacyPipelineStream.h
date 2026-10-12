#pragma once
#include <string>
#include "Config.h"
#include "mpp/LegacyPipelineStream.h"
namespace mpp::resource_parsers { class _MPPRESOURCEPARSERSAPI FileLegacyPipelineStream : public LegacyPipelineStream { std::string mFilepath;std::shared_ptr<LegacyPipelineDocument> mSuppliedDocument; void createChildResourceStreamsImpl() override; protected: void loadImpl() override; public: FileLegacyPipelineStream(ResourceManager* manager,std::string filepath);FileLegacyPipelineStream(ResourceManager* manager,std::shared_ptr<LegacyPipelineDocument> document,std::string sourcePath); }; }
