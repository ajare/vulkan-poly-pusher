#include "mpp/MppException.h"
#include "mpp/LegacyPipelineStream.h"
namespace mpp { LegacyPipelineStream::LegacyPipelineStream(ResourceManager*r):ResourceStream(r,"LegacyPipeline"){} void LegacyPipelineStream::setDocument(std::shared_ptr<LegacyPipelineDocument>d){if(!d)THROW_MPP("LegacyPipeline stream requires a document.",__LINE__,__FILE__,__func__);mDocument=std::move(d);} std::shared_ptr<LegacyPipelineDocument> const& LegacyPipelineStream::getDocument()const{return mDocument;} }
