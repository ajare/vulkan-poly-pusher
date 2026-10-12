#pragma once
#include <memory>
#include "mpp/Config.h"
#include "mpp/ResourceStream.h"
#include "mpp/SceneDocument.h"
namespace mpp { class _MPPAPI SceneStream : public ResourceStream { std::shared_ptr<SceneDocument> mDocument; protected:void loadImpl()override{} public:explicit SceneStream(ResourceManager*);void setDocument(std::shared_ptr<SceneDocument>);std::shared_ptr<SceneDocument>const&getDocument()const;}; }
