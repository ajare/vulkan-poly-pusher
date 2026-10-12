#pragma once
#include <memory>
#include "mpp/Config.h"
#include "mpp/Resource.h"
#include "mpp/SceneDocument.h"
namespace mpp { class _MPPAPI SceneTemplate:public Resource{std::shared_ptr<SceneDocument>mDocument;protected:void createImpl()override;void destroyImpl()override;void loadImpl()override{}void unloadImpl()override{}public:SceneTemplate(std::string const&,RenderSystem*,ResourceManager*,ResourceStreamPtr);std::shared_ptr<SceneDocument>const&getDocument()const;}; }
