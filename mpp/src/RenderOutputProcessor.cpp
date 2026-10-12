#include "mpp/Config.h"
#if defined(_WIN32)
#include <Windows.h>
#endif
#include <GL/glew.h>

#include "mpp/RenderOutputProcessor.h"

#include <algorithm>
#include <utility>

#include "mpp/GpuDebugScope.h"
#include "mpp/MppException.h"
#include "mpp/RenderGraphTargets.h"
#include "mpp/RenderSystem.h"
#include "mpp/RenderTexture.h"

using namespace std;

namespace mpp
{
	glm::vec2 taaHaltonJitter(uint32_t index)
	{
		auto halton=[](uint32_t value,uint32_t base){float result=0.0f,fraction=1.0f/(float)base;while(value){result+=fraction*(float)(value%base);value/=base;fraction/=(float)base;}return result;};uint32_t sample=index%8+1;return {halton(sample,2)-0.5f,halton(sample,3)-0.5f};
	}

	namespace
	{
		GraphImageInfo findImage(RenderGraph const& graph, string const& name)
		{
			for(uint32_t id=0;id<graph.getImageCount();++id){auto info=graph.getImageInfo({id,0});if(info.name==name)return info;}
			THROW_MPP("Named output references unknown graph image '"+name+"'.",__LINE__,__FILE__,__func__);
		}

		bool samePlans(vector<RenderPipelineOutputPlan> const& left,vector<RenderPipelineOutputPlan> const& right)
		{
			if(left.size()!=right.size())return false;
			for(size_t index=0;index<left.size();++index)
			{
				auto const& a=left[index];auto const& b=right[index];
				if(a.name!=b.name||a.image!=b.image||a.taaDepth!=b.taaDepth||a.logicalSize!=b.logicalSize||a.rasterSize!=b.rasterSize||a.rasterSamples!=b.rasterSamples||a.antiAliasing.msaa!=b.antiAliasing.msaa||a.antiAliasing.ssaa!=b.antiAliasing.ssaa||a.antiAliasing.taa!=b.antiAliasing.taa||a.antiAliasing.fxaa!=b.antiAliasing.fxaa||a.physicalImages.size()!=b.physicalImages.size())return false;
				for(size_t image=0;image<a.physicalImages.size();++image){auto const& x=a.physicalImages[image];auto const& y=b.physicalImages[image];if(x.role!=y.role||x.size!=y.size||x.format!=y.format||x.samples!=y.samples)return false;}
			}
			return true;
		}
	}

	RenderOutputProcessor::RenderOutputProcessor(RenderSystem* renderSystem,string pipelineName):mRenderSystem(renderSystem),mPipelineName(std::move(pipelineName))
	{
		if(!mRenderSystem)THROW_MPP("RenderOutputProcessor requires a RenderSystem.",__LINE__,__FILE__,__func__);
	}

	void RenderOutputProcessor::rebuild(vector<RenderPipelineOutput> const& outputs,RenderGraph const& graph,map<string,RenderTargetPtr> const& destinations,AntiAliasingDefaults const& defaults)
	{
		vector<RenderPipelineOutputPlan> plans;plans.reserve(outputs.size());
		for(auto const& output:outputs)
		{
			auto destination=destinations.find(output.name);if(destination==destinations.end()||!destination->second)THROW_MPP("Named output '"+output.name+"' has no destination render target.",__LINE__,__FILE__,__func__);
			auto colour=findImage(graph,output.image);auto effective=resolveAntiAliasing(defaults,output.antiAliasing);
			// An offscreen output's destination IS the graph's own image, which the
			// graph already rendered at raster size. There is nothing left to
			// downsample from, and no logical-size target to put the result in, so
			// SSAA has no meaning here -- unlike an external output, whose destination
			// is the screen at logical size. Forcing it off keeps rasterSize equal to
			// size, which is what the rest of the chain's work targets assume.
			if(!colour.desc.external)effective.ssaa=AntiAliasingSamples::Off;
			glm::uvec2 size((uint32_t)destination->second->getWidth(),(uint32_t)destination->second->getHeight());if(size.x==0||size.y==0)THROW_MPP("Named output '"+output.name+"' has zero-sized destination.",__LINE__,__FILE__,__func__);
			glm::uvec2 rasterSize(ssaaDimension(size.x,effective.ssaa),ssaaDimension(size.y,effective.ssaa));RenderPipelineOutputPlan plan;plan.name=output.name;plan.image=output.image;plan.taaDepth=output.taaDepth;plan.antiAliasing=effective;plan.logicalSize=size;plan.rasterSize=rasterSize;plan.rasterSamples=antiAliasingSampleCount(effective.msaa);plan.physicalImages.push_back({PhysicalOutputImageRole::Input,rasterSize,colour.desc.format,1});
			if(effective.ssaa!=AntiAliasingSamples::Off){plan.physicalImages.push_back({PhysicalOutputImageRole::Work,{size.x,rasterSize.y},colour.desc.format,1});plan.physicalImages.push_back({PhysicalOutputImageRole::Work,size,colour.desc.format,1});}if(effective.fxaa)plan.physicalImages.push_back({PhysicalOutputImageRole::Work,size,colour.desc.format,1});
			if(effective.taa){plan.physicalImages.push_back({PhysicalOutputImageRole::TaaColourHistory,rasterSize,colour.desc.format,1});plan.physicalImages.push_back({PhysicalOutputImageRole::TaaColourHistory,rasterSize,colour.desc.format,1});auto depthInfo=output.taaDepth.empty()?GraphImageInfo{}:findImage(graph,output.taaDepth);auto depthFormat=output.taaDepth.empty()?GraphImageFormat::Depth24:depthInfo.desc.format;if(!output.taaDepth.empty()){glm::uvec2 depthSize(depthInfo.desc.absoluteSize.x?depthInfo.desc.absoluteSize.x:std::max(1u,(uint32_t)(rasterSize.x*depthInfo.desc.relativeSize.x)),depthInfo.desc.absoluteSize.y?depthInfo.desc.absoluteSize.y:std::max(1u,(uint32_t)(rasterSize.y*depthInfo.desc.relativeSize.y)));if(depthSize!=rasterSize)THROW_MPP("TAA depth source '"+output.taaDepth+"' dimensions do not match output '"+output.name+"'.",__LINE__,__FILE__,__func__);}plan.physicalImages.push_back({PhysicalOutputImageRole::TaaDepthHistory,rasterSize,depthFormat,1});}
			plans.push_back(std::move(plan));
		}
		if(samePlans(plans,mPlans))return;

		map<string,OutputState> candidate;uint64_t generation=mGeneration+1;
		for(auto const& plan:plans)
		{
			OutputState state;state.plan=plan;
			for(size_t index=0;index<plan.physicalImages.size();++index)
			{
				auto const& physical=plan.physicalImages[index];GraphImageDesc desc;desc.format=physical.format;desc.params.minFilter=GL_LINEAR;desc.params.magFilter=GL_LINEAR;desc.params.wrap=GL_CLAMP_TO_EDGE;auto target=mRenderSystem->createRenderTexture(mPipelineName+".Output."+plan.name+".g"+to_string(generation)+"."+to_string(index),physical.size.x,physical.size.y,makeGraphRenderTextureOptions(desc));
				switch(physical.role){case PhysicalOutputImageRole::Input:state.input=target;break;case PhysicalOutputImageRole::Work:state.work.push_back(target);break;case PhysicalOutputImageRole::TaaColourHistory:state.colourHistory.push_back(target);break;case PhysicalOutputImageRole::TaaDepthHistory:state.depthHistory=target;break;}
			}
			candidate.emplace(plan.name,std::move(state));
		}
		mOutputs.swap(candidate);mPlans.swap(plans);mGeneration=generation;
	}

	void RenderOutputProcessor::clear(){mOutputs.clear();mPlans.clear();++mGeneration;}
	RenderTargetPtr RenderOutputProcessor::getInput(string const& outputName) const{auto found=mOutputs.find(outputName);return found==mOutputs.end()?nullptr:found->second.input;}
	void RenderOutputProcessor::present(string const& outputName,RenderTargetPtr const& destination,RenderTargetPtr const& source,RenderTargetPtr const& depthSource,TaaFrameContext const* taaFrame)
	{
		auto found=mOutputs.find(outputName);auto input=source?source:getInput(outputName);if(found==mOutputs.end()||!input||!destination)THROW_MPP("Cannot present an unconfigured named output.",__LINE__,__FILE__,__func__);auto& state=found->second;bool const flow=mRenderSystem->isRenderFlowCaptureActive();auto colourFormat=state.plan.physicalImages.front().format;auto resource=[&](string name,RenderTargetPtr const& target,GraphImageFormat format){return RenderFlowResourceDesc{std::move(name),{(uint32_t)target->getWidth(),(uint32_t)target->getHeight()},format,1};};auto disabled=[&](RenderFlowEventKind kind,char const* stage){if(flow)try{mRenderSystem->recordRenderFlowEvent(kind,outputName+" / "+stage,{},false,"Disabled by effective output setting",outputName);}catch(...){mRenderSystem->failRenderFlowCapture();}};if(state.plan.antiAliasing.msaa==AntiAliasingSamples::Off)disabled(RenderFlowEventKind::MsaaResolve,"MSAA");// Skip only when the chain would genuinely do nothing. This used to skip
			// whenever the input was already the destination, which is exactly the case
			// for every offscreen output -- so FXAA and TAA were silently dropped there
			// even though the authoring was accepted and their work targets allocated.
			// With at least one stage enabled the intermediate targets are distinct, so
			// the final blit never reads and writes the same texture.
			bool const chainDoesNothing=!state.plan.antiAliasing.taa&&state.plan.antiAliasing.ssaa==AntiAliasingSamples::Off&&!state.plan.antiAliasing.fxaa;
			if(input==destination&&chainDoesNothing){auto bypass=[&](RenderFlowEventKind kind,char const* stage){if(flow)try{mRenderSystem->recordRenderFlowEvent(kind,outputName+" / "+stage,{},false,"Input is already the destination",outputName);}catch(...){mRenderSystem->failRenderFlowCapture();}};bypass(RenderFlowEventKind::Taa,"TAA");bypass(RenderFlowEventKind::SsaaHorizontal,"SSAA");bypass(RenderFlowEventKind::Fxaa,"FXAA");bypass(RenderFlowEventKind::Presentation,"Presentation");return;}auto texture=dynamic_cast<RenderTexture*>(input.get());if(!texture)THROW_MPP("Named output input is not a render texture.",__LINE__,__FILE__,__func__);string textureName;if(flow)try{textureName=outputName+".Input";}catch(...){mRenderSystem->failRenderFlowCapture();}RenderTargetPtr currentTarget=input;
		if(state.plan.antiAliasing.taa)
		{
			GpuDebugScope outputScope(renderFlowOutputRenderDocLabel(outputName,RenderFlowEventKind::Taa));
			auto depth=dynamic_cast<RenderTexture*>(depthSource.get());auto depthHistory=dynamic_cast<RenderTexture*>(state.depthHistory.get());if(!taaFrame||!depth||!depthHistory||state.colourHistory.size()!=2||depth->getWidth()!=texture->getWidth()||depth->getHeight()!=texture->getHeight())THROW_MPP("TAA output is missing a matching resolved depth source or frame context.",__LINE__,__FILE__,__func__);bool valid=state.historyValid&&!taaFrame->resetHistory&&state.lastFrameSerial+1==taaFrame->frameSerial;if(!valid)++state.historyResetCount;uint32_t next=valid?1-state.historyIndex:0;auto nextHistory=dynamic_cast<RenderTexture*>(state.colourHistory[next].get());auto depthFormat=state.plan.physicalImages.back().format;if(flow)try{vector<RenderFlowResourceDesc> inputs{resource(textureName,input,colourFormat),resource(outputName+".CurrentDepth",depthSource,depthFormat)};if(valid){inputs.push_back(resource(outputName+".TaaHistory"+to_string(state.historyIndex),state.colourHistory[state.historyIndex],colourFormat));inputs.push_back(resource(outputName+".TaaDepthHistory",state.depthHistory,depthFormat));}vector<RenderFlowResourceDesc> outputs{resource(outputName+".TaaHistory"+to_string(next),state.colourHistory[next],colourFormat),resource(outputName+".TaaDepthHistory",state.depthHistory,depthFormat)};mRenderSystem->recordRenderFlowEvent(RenderFlowEventKind::Taa,outputName+" / TAA",{},true,{},outputName,false,std::move(inputs),std::move(outputs));}catch(...){mRenderSystem->failRenderFlowCapture();}if(valid)mRenderSystem->renderTaa(texture,depth,dynamic_cast<RenderTexture*>(state.colourHistory[state.historyIndex].get()),depthHistory,state.colourHistory[next],taaFrame->inverseCurrentViewProjection,state.previousViewProjection);else{mRenderSystem->setProjection2dOrthographic();mRenderSystem->resetTransform();mRenderSystem->scaleTransform2d(glm::vec2((float)nextHistory->getWidth()/mRenderSystem->getWindowWidth(),(float)nextHistory->getHeight()/mRenderSystem->getWindowHeight()));mRenderSystem->setRenderTarget(state.colourHistory[next]);mRenderSystem->setViewport(0,0,nextHistory->getWidth(),nextHistory->getHeight());mRenderSystem->renderFullscreenQuad(texture,BlendMode::One,BlendMode::Zero);}depth->copyDepthTo(depthHistory);state.historyIndex=next;state.historyValid=true;state.lastFrameSerial=taaFrame->frameSerial;state.previousViewProjection=taaFrame->currentViewProjection;texture=nextHistory;currentTarget=state.colourHistory[next];if(flow)try{textureName=outputName+".TaaHistory"+to_string(next);}catch(...){mRenderSystem->failRenderFlowCapture();}
		}
		else disabled(RenderFlowEventKind::Taa,"TAA");
		size_t workIndex=0;if(state.plan.antiAliasing.ssaa!=AntiAliasingSamples::Off){if(state.work.size()<2)THROW_MPP("SSAA output is missing Lanczos work targets.",__LINE__,__FILE__,__func__);if(flow)try{mRenderSystem->recordRenderFlowEvent(RenderFlowEventKind::SsaaHorizontal,outputName+" / SSAA Horizontal",{},true,{},outputName,false,{resource(textureName,currentTarget,colourFormat)},{resource(outputName+".Work0",state.work[workIndex],colourFormat)});}catch(...){mRenderSystem->failRenderFlowCapture();}{GpuDebugScope outputScope(renderFlowOutputRenderDocLabel(outputName,RenderFlowEventKind::SsaaHorizontal));mRenderSystem->renderSsaaLanczos(texture,state.work[workIndex],glm::vec2(1,0));}texture=dynamic_cast<RenderTexture*>(state.work[workIndex].get());currentTarget=state.work[workIndex++];if(flow)try{textureName=outputName+".Work0";}catch(...){mRenderSystem->failRenderFlowCapture();}if(flow)try{mRenderSystem->recordRenderFlowEvent(RenderFlowEventKind::SsaaVertical,outputName+" / SSAA Vertical",{},true,{},outputName,false,{resource(textureName,currentTarget,colourFormat)},{resource(outputName+".Work1",state.work[workIndex],colourFormat)});}catch(...){mRenderSystem->failRenderFlowCapture();}{GpuDebugScope outputScope(renderFlowOutputRenderDocLabel(outputName,RenderFlowEventKind::SsaaVertical));mRenderSystem->renderSsaaLanczos(texture,state.work[workIndex],glm::vec2(0,1));}texture=dynamic_cast<RenderTexture*>(state.work[workIndex].get());currentTarget=state.work[workIndex++];if(flow)try{textureName=outputName+".Work1";}catch(...){mRenderSystem->failRenderFlowCapture();}}else disabled(RenderFlowEventKind::SsaaHorizontal,"SSAA");if(state.plan.antiAliasing.fxaa){if(workIndex>=state.work.size())THROW_MPP("FXAA output is missing its LDR work target.",__LINE__,__FILE__,__func__);if(flow)try{mRenderSystem->recordRenderFlowEvent(RenderFlowEventKind::Fxaa,outputName+" / FXAA",{},true,{},outputName,false,{resource(textureName,currentTarget,colourFormat)},{resource(outputName+".Work"+to_string(workIndex),state.work[workIndex],colourFormat)});}catch(...){mRenderSystem->failRenderFlowCapture();}{GpuDebugScope outputScope(renderFlowOutputRenderDocLabel(outputName,RenderFlowEventKind::Fxaa));mRenderSystem->renderFxaa(texture,state.work[workIndex]);}texture=dynamic_cast<RenderTexture*>(state.work[workIndex].get());currentTarget=state.work[workIndex];if(flow)try{textureName=outputName+".Work"+to_string(workIndex);}catch(...){mRenderSystem->failRenderFlowCapture();}}else disabled(RenderFlowEventKind::Fxaa,"FXAA");if(flow)try{mRenderSystem->recordRenderFlowEvent(RenderFlowEventKind::Presentation,outputName+" / Presentation",{},true,{},outputName,false,{resource(textureName,currentTarget,colourFormat)},{resource(outputName+".Destination",destination,colourFormat)});}catch(...){mRenderSystem->failRenderFlowCapture();}GpuDebugScope presentationScope(renderFlowOutputRenderDocLabel(outputName,RenderFlowEventKind::Presentation));mRenderSystem->setProjection2dOrthographic();mRenderSystem->resetTransform();mRenderSystem->scaleTransform2d(glm::vec2((float)destination->getWidth()/mRenderSystem->getWindowWidth(),(float)destination->getHeight()/mRenderSystem->getWindowHeight()));mRenderSystem->setRenderTarget(destination);mRenderSystem->setViewport(0,0,destination->getWidth(),destination->getHeight());mRenderSystem->renderFullscreenQuad(texture,BlendMode::One,BlendMode::Zero);
	}
	uint64_t RenderOutputProcessor::getGeneration() const{return mGeneration;}
	vector<RenderPipelineOutputPlan> const& RenderOutputProcessor::getPlans() const{return mPlans;}
	bool RenderOutputProcessor::hasValidTaaHistory(string const& outputName) const{auto found=mOutputs.find(outputName);return found!=mOutputs.end()&&found->second.historyValid;}
	uint64_t RenderOutputProcessor::getTaaHistoryResetCount(string const& outputName) const{auto found=mOutputs.find(outputName);return found==mOutputs.end()?0:found->second.historyResetCount;}
}
