#include <GL/glew.h>
#include <algorithm>
#include <filesystem>
#include <glm/gtc/constants.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "mpp/BoxModelStream.h"
#include "mpp/CylinderModelStream.h"
#include "mpp/GridModelStream.h"
#include "mpp/Model.h"
#include "mpp/MppException.h"
#include "mpp/MppModelStream.h"
#include "mpp/PbrMaterial.h"
#include "mpp/ProgrammaticPbrMaterialStream.h"
#include "mpp/ResourceManager.h"
#include "mpp/RenderSystem.h"
#include "mpp/SceneRuntime.h"
#include "mpp/SphereModelStream.h"
#include "mpp/mesh/MeshSpecification.h"
#include "mpp/mesh/Vertex.h"

namespace mpp
{
	namespace
	{
		mesh::MeshSpecification previewMeshSpecification()
		{
			mesh::MeshSpecification result(mesh::Primitive::Type::Triangles);
			auto layout=result.createVertexBufferAttributeLayout(false);
			layout->createAttribute(mesh::Vertex::Component::Position3,mesh::Vertex::DataType::Float,false);
			layout->createAttribute(mesh::Vertex::Component::Normal3,mesh::Vertex::DataType::Float,false);
			layout->createAttribute(mesh::Vertex::Component::TexCoord2,mesh::Vertex::DataType::Float,false);
			layout->createAttribute(mesh::Vertex::Component::Colour4,mesh::Vertex::DataType::Float,true);
			layout->createAttribute(mesh::Vertex::Component::Tangent4,mesh::Vertex::DataType::Float,false);
			result.setStorageType(mesh::VertexBufferStorageType::Static);result.setIndexedVertices(true);return result;
		}
	}

	SceneRuntime::SceneRuntime(RenderSystem* renderSystem,ResourceManager* resourceManager):mRenderSystem(renderSystem),mResourceManager(resourceManager)
	{
		if(!mRenderSystem||!mResourceManager)THROW_MPP("SceneRuntime requires render and resource systems.",__LINE__,__FILE__,__func__);
	}
	SceneRuntime::~SceneRuntime(){clear();}

	void SceneRuntime::clearResources(ScenePtr& scene,std::vector<std::string>& names)
	{
		if(scene){scene->unload();scene.reset();}
		for(auto const& name:names)if(mResourceManager->getResource(name,true)){try{mResourceManager->deleteResourceTree(name);}catch(...){}}
		names.clear();
	}
	void SceneRuntime::clear()
	{
		auto& particles=mRenderSystem->getParticleSystem();for(auto const& [id,handle]:mParticleEffects)if(particles.isAlive(handle))particles.destroyEffect(handle);mParticleEffects.clear();
		mModelInstances.clear();clearResources(mScene,mResourceNames);mDiagnostics.clear();mModelTriangles.clear();mLights.clear();mEnvironmentBinding.clear();mUniqueTriangles=0;
	}

	bool SceneRuntime::rebuild(SceneDocument const& document,std::map<std::string,ResourcePtr> const& materialBindings,std::map<std::string,UniformCollection> const& instanceOverrides,std::string const& expectedEnvironmentBinding,std::string const& shadowDomain,std::map<std::string,ResourcePtr> const& particleEffectBindings)
	{
		mDiagnostics=document.validate();
		if(!expectedEnvironmentBinding.empty()&&document.environmentBinding!=expectedEnvironmentBinding)
			mDiagnostics.error("MPP-SCENE-RUNTIME-007","Scene environment binding '"+document.environmentBinding+"' does not match active pipeline binding '"+expectedEnvironmentBinding+"'.",{document.sourcePath},"environment");
		if(mDiagnostics.hasErrors())return false;
		ScenePtr candidate=std::make_shared<Scene>(mRenderSystem);std::vector<std::string> candidateNames;std::map<std::string,uint64_t> candidateTriangles;std::map<std::string,SceneModel3dPtr> candidateInstances;std::map<std::string,ParticleEffectHandle> candidateEffects;std::vector<PbrLight> candidateLights;uint64_t candidateTriangleTotal=0;auto generation=std::to_string(++mGeneration),prefix="SceneRuntime."+generation+".";
		try
		{
			candidate->load();
			auto declareOwned=[&](std::string const& name,ResourceStreamPtr const& stream){auto declared=mResourceManager->declareResource(name,stream);if(!declared.second)THROW_MPP("Scene candidate resource name already exists: "+name,__LINE__,__FILE__,__func__);candidateNames.push_back(name);return declared.first;};
			for(auto const& authored:document.lights)
			{
				PbrLight light;light.type=authored.type==SceneLightType::Point?PbrLightType::Point:PbrLightType::Directional;light.colour=authored.colour;light.intensity=authored.intensity;light.position=authored.position;light.range=authored.range;light.direction=authored.type==SceneLightType::Directional?glm::normalize(authored.direction):authored.direction;candidateLights.push_back(light);
			}
			candidate->setPbrLights(candidateLights);
			auto meshSpec=previewMeshSpecification();auto fallbackName=prefix+"FallbackPbr";auto materialStream=std::make_shared<ProgrammaticPbrMaterialStream>(mResourceManager);PbrMaterialSpecification::PbrSurface surface;surface.enabled=true;surface.metallicFactor=0.0f;surface.roughnessFactor=0.8f;materialStream->setMeshSpecification(meshSpec);materialStream->setSurface(surface);auto fallback=declareOwned(fallbackName,materialStream);fallback->load();if(mResourceManager->getResource(fallbackName+"/Program",true))candidateNames.push_back(fallbackName+"/Program");
			for(auto const& authored:document.models)
			{
				ResourcePtr material=fallback;auto mapped=materialBindings.find(authored.materialBinding);
				if(mapped!=materialBindings.end()&&mapped->second&&dynamic_cast<PbrMaterial*>(mapped->second.get()))material=mapped->second;
				else if(!authored.materialBinding.empty())mDiagnostics.warning("MPP-SCENE-RUNTIME-001","Material binding '"+authored.materialBinding+"' uses the neutral PBR placeholder.",{document.sourcePath},authored.id);
				auto makePrimitive=[&](std::string const& name,SceneModelSource source){ResourceStreamPtr stream;switch(source){case SceneModelSource::Box:stream=std::make_shared<BoxModelStream>(mResourceManager,meshSpec,material->getName(),authored.primitive.width,authored.primitive.height,authored.primitive.depth);break;case SceneModelSource::Sphere:stream=std::make_shared<SphereModelStream>(mResourceManager,meshSpec,material->getName(),authored.primitive.radius,(int)authored.primitive.resolution);break;case SceneModelSource::Cylinder:stream=std::make_shared<CylinderModelStream>(mResourceManager,meshSpec,material->getName(),authored.primitive.height,authored.primitive.radius,authored.primitive.topRadius,(int)authored.primitive.resolution);break;default:stream=std::make_shared<GridModelStream>(mResourceManager,meshSpec,material->getName(),authored.primitive.width,authored.primitive.depth,authored.primitive.segmentsX,authored.primitive.segmentsZ,authored.primitive.textureRepeatU,authored.primitive.textureRepeatV);break;}auto value=declareOwned(name,stream);value->load();return value;};
				ResourcePtr model;bool placeholder=false;auto modelName=prefix+authored.id;
				if(authored.source==SceneModelSource::MppModel)
				{
					auto path=std::filesystem::path(authored.file);if(!path.is_absolute())path=std::filesystem::path(document.sourcePath).parent_path()/path;
					if(std::filesystem::exists(path)){try{auto stream=std::make_shared<MppModelStream>(mResourceManager,path.string());model=declareOwned(modelName+".Source",stream);model->load();if(!dynamic_cast<Model*>(model.get())){mDiagnostics.warning("MPP-SCENE-RUNTIME-005","Loaded resource is not a Model; using placeholder.",{document.sourcePath},authored.id);model.reset();}}catch(std::exception const& error){mDiagnostics.warning("MPP-SCENE-RUNTIME-002","Model load failed; using placeholder: "+std::string(error.what()),{document.sourcePath},authored.id);model.reset();}}
					if(!model){mDiagnostics.warning("MPP-SCENE-RUNTIME-003","Missing model uses a placeholder box.",{document.sourcePath},authored.id);placeholder=true;model=makePrimitive(modelName+".Placeholder",SceneModelSource::Box);}
				}
				else model=makePrimitive(modelName,authored.source);
				auto modelValue=dynamic_cast<Model*>(model.get());if(!modelValue)THROW_MPP("Scene model resource has the wrong type.",__LINE__,__FILE__,__func__);
				auto triangles=placeholder?0u:(uint64_t)modelValue->getNumTriangles();candidateTriangles[authored.id]=triangles;if(authored.visible)candidateTriangleTotal+=triangles;
				auto instance=candidate->add3dModel(model);instance->setRenderLayers(authored.layers);instance->resetTransform();instance->translate(authored.translation);instance->rotateSelf(glm::radians(authored.rotationDegrees.x),glm::vec3(1,0,0));instance->rotateSelf(glm::radians(authored.rotationDegrees.y),glm::vec3(0,1,0));instance->rotateSelf(glm::radians(authored.rotationDegrees.z),glm::vec3(0,0,1));instance->scale(authored.scale);uint32_t flags=0;if(authored.visible)flags|=ModelRenderParams::Flag_Visible;if(authored.shadowCaster)flags|=ModelRenderParams::Flag_CastShadows;instance->getParams()->setModelFlags(flags);instance->getParams()->setModelMaterial(material);auto overrideValue=instanceOverrides.find(authored.id);if(overrideValue!=instanceOverrides.end())instance->getParams()->setModelUniforms(std::make_shared<UniformCollection>(overrideValue->second));candidateInstances[authored.id]=instance;
			}
			for(auto const& authored:document.particleEffects)
			{
				auto binding=particleEffectBindings.find(authored.effect);if(binding==particleEffectBindings.end()||!binding->second)THROW_MPP("Particle effect resource is unavailable: "+authored.effect,__LINE__,__FILE__,__func__);
				auto transform=glm::translate(glm::mat4(1.0f),authored.translation);transform=glm::rotate(transform,glm::radians(authored.rotationDegrees.x),glm::vec3(1,0,0));transform=glm::rotate(transform,glm::radians(authored.rotationDegrees.y),glm::vec3(0,1,0));transform=glm::rotate(transform,glm::radians(authored.rotationDegrees.z),glm::vec3(0,0,1));transform=glm::scale(transform,authored.scale);
				auto handle=mRenderSystem->getParticleSystem().createEffect(binding->second,transform);mRenderSystem->getParticleSystem().setEffectVisible(handle,authored.visible);candidateEffects.emplace(authored.id,handle);
			}
		}
		catch(std::exception const& error){auto& particles=mRenderSystem->getParticleSystem();for(auto const& [id,handle]:candidateEffects)if(particles.isAlive(handle))particles.destroyEffect(handle);mDiagnostics.error("MPP-SCENE-RUNTIME-004","Scene candidate creation failed: "+std::string(error.what()),{document.sourcePath},"scene");clearResources(candidate,candidateNames);return false;}
		if (!shadowDomain.empty())
		{
			try
			{
				auto const shadowLightIndex = document.getShadowLightIndex();
				if (!shadowLightIndex) THROW_MPP("Shadow domain requires a shadow-casting scene light.", __LINE__, __FILE__, __func__);
				auto const& authored = document.lights[*shadowLightIndex];
				ShadowOptions shadow;
				shadow.enabled = true;
				shadow.light.type = authored.type == SceneLightType::Point ? ShadowLightType::Point : ShadowLightType::Directional;
				shadow.light.lightIndex = static_cast<uint32_t>(*shadowLightIndex);
				if (authored.type == SceneLightType::Point)
				{
					shadow.light.position = authored.position;
					shadow.light.range = authored.range;
				}
				else
				{
					shadow.light.direction = glm::normalize(authored.direction);
					shadow.light.focusPoint = document.camera.target;
				}
				mRenderSystem->configureShadowDomain(shadowDomain, shadow);
			}
			catch (std::exception const& error)
			{
				mDiagnostics.error("MPP-SCENE-RUNTIME-008", "Scene shadow domain setup failed: " + std::string(error.what()), { document.sourcePath }, "lights");
				auto& particles=mRenderSystem->getParticleSystem();for(auto const& [id,handle]:candidateEffects)if(particles.isAlive(handle))particles.destroyEffect(handle);
				clearResources(candidate, candidateNames);
				return false;
			}
		}
		auto previous=mScene;auto previousNames=std::move(mResourceNames);auto previousEffects=std::move(mParticleEffects);mScene=std::move(candidate);mResourceNames=std::move(candidateNames);mModelTriangles=std::move(candidateTriangles);mModelInstances=std::move(candidateInstances);mParticleEffects=std::move(candidateEffects);mLights=std::move(candidateLights);mEnvironmentBinding=document.environmentBinding;mUniqueTriangles=candidateTriangleTotal;auto& particles=mRenderSystem->getParticleSystem();for(auto const& [id,handle]:previousEffects)if(particles.isAlive(handle))particles.destroyEffect(handle);clearResources(previous,previousNames);return true;
	}
	ScenePtr const& SceneRuntime::getScene()const{return mScene;}
	DiagnosticBag const& SceneRuntime::getDiagnostics()const{return mDiagnostics;}
	uint64_t SceneRuntime::getGeneration()const{return mGeneration;}
	uint64_t SceneRuntime::getUniqueTriangleCount()const{return mUniqueTriangles;}
	uint64_t SceneRuntime::getModelTriangleCount(std::string const& modelId)const{auto found=mModelTriangles.find(modelId);return found==mModelTriangles.end()?0:found->second;}
	SceneModel3dPtr SceneRuntime::getModelInstance(std::string const& modelId)const{auto found=mModelInstances.find(modelId);return found==mModelInstances.end()?SceneModel3dPtr{}:found->second;}
	ParticleEffectHandle SceneRuntime::getParticleEffect(std::string const& effectId)const{auto found=mParticleEffects.find(effectId);return found==mParticleEffects.end()?ParticleEffectHandle{}:found->second;}
	std::string SceneRuntime::getModelId(SceneModel3d const* instance)const{for(auto const& [id,value]:mModelInstances)if(value.get()==instance)return id;return {};}
	std::vector<PbrLight> const& SceneRuntime::getLights()const{return mLights;}
	std::string const& SceneRuntime::getEnvironmentBinding()const{return mEnvironmentBinding;}
}
