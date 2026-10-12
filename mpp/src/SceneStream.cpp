#include "mpp/MppException.h"
#include "mpp/SceneStream.h"
namespace mpp { SceneStream::SceneStream(ResourceManager*m):ResourceStream(m,"SceneTemplate"){}void SceneStream::setDocument(std::shared_ptr<SceneDocument>d){if(!d)THROW_MPP("SceneStream requires a document.",__LINE__,__FILE__,__func__);mDocument=std::move(d);}std::shared_ptr<SceneDocument>const&SceneStream::getDocument()const{return mDocument;} }
