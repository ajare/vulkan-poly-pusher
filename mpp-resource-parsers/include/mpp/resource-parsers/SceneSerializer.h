#pragma once
#include <string>
#include "Config.h"
#include "mpp/SceneDocument.h"
namespace mpp::resource_parsers { class _MPPRESOURCEPARSERSAPI SceneSerializer { public: static void toFile(SceneDocument const&,std::string const&); }; }
