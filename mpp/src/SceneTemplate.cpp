#include "mpp/MppException.h"
#include "mpp/SceneStream.h"
#include "mpp/SceneTemplate.h"
namespace mpp { SceneTemplate::SceneTemplate(std::string const&n,RenderSystem*r,ResourceManager*m,ResourceStreamPtr s):Resource(n,"SceneTemplate",r,m,s){}void SceneTemplate::createImpl(){auto stream=dynamic_cast<SceneStream*>(getResourceStream().get());if(!stream||!stream->getDocument())THROW_MPP("SceneTemplate requires a populated SceneStream.",__LINE__,__FILE__,__func__);if(stream->getDocument()->validate().hasErrors())THROW_MPP("Cannot create invalid SceneTemplate.",__LINE__,__FILE__,__func__);mDocument=stream->getDocument();}void SceneTemplate::destroyImpl(){mDocument.reset();}std::shared_ptr<SceneDocument>const&SceneTemplate::getDocument()const{return mDocument;} }
