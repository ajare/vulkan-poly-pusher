#include "mpp/MppException.h"
#include "mpp/PbrPipelineStream.h"
namespace mpp { PbrPipelineStream::PbrPipelineStream(ResourceManager*r):ResourceStream(r,"PbrPipeline"){} void PbrPipelineStream::setDocument(std::shared_ptr<PbrPipelineDocument>d){if(!d)THROW_MPP("PbrPipeline stream requires a document.",__LINE__,__FILE__,__func__);mDocument=std::move(d);} std::shared_ptr<PbrPipelineDocument> const& PbrPipelineStream::getDocument()const{return mDocument;} }
