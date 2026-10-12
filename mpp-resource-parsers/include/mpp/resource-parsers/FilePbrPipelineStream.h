#pragma once
#include <string>
#include "Config.h"
#include "mpp/PbrPipelineStream.h"
namespace mpp::resource_parsers { class _MPPRESOURCEPARSERSAPI FilePbrPipelineStream : public PbrPipelineStream { std::string mFilepath;std::shared_ptr<PbrPipelineDocument> mSuppliedDocument; void createChildResourceStreamsImpl() override; protected: void loadImpl() override; public: FilePbrPipelineStream(ResourceManager* manager,std::string filepath);FilePbrPipelineStream(ResourceManager* manager,std::shared_ptr<PbrPipelineDocument> document,std::string sourcePath); }; }
