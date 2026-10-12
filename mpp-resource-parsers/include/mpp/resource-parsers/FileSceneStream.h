#pragma once
#include <string>
#include "Config.h"
#include "mpp/SceneStream.h"
namespace mpp::resource_parsers { class _MPPRESOURCEPARSERSAPI FileSceneStream:public SceneStream{std::string mFilepath;protected:void loadImpl()override;public:FileSceneStream(ResourceManager*,std::string);}; }
