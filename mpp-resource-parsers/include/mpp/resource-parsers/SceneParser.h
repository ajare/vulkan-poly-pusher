#pragma once
#include <string>
#include "Config.h"
#include "mpp/SceneDocument.h"
namespace mpp::resource_parsers { class _MPPRESOURCEPARSERSAPI SceneParser { public: static SceneDocument fromFile(std::string const& filepath); }; }
