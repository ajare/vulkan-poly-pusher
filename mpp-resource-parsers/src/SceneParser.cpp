#include <memory>
#include <set>
#include <sstream>
#include "utils/StringUtils.h"
#include "mpp/resource-parsers/MppResourceParsersException.h"
#include "mpp/resource-parsers/SceneParser.h"
#include "StructuredDataAdapter.h"

using namespace std;

namespace mpp::resource_parsers
{
	namespace
	{
		void rejectUnknown(mpp::data::StructuredData const& data,set<string> const& allowed,string const& context)
		{
			for(auto const& entry:data)if(!allowed.contains(entry.first))THROW_MPP_RESOURCE_PARSERS("Unknown field '"+entry.first+"' in "+context+".",__LINE__,__FILE__,__func__);
		}
		glm::vec3 vec3(string const& value)
		{
			istringstream stream(value);glm::vec3 result;string extra;
			if(!(stream>>result.x>>result.y>>result.z)||stream>>extra)THROW_MPP_RESOURCE_PARSERS("Invalid scene vec3 '"+value+"'.",__LINE__,__FILE__,__func__);
			return result;
		}
		bool boolean(string value)
		{
			utils::StringUtils::toUpper(value);if(value=="TRUE"||value=="1"||value=="YES")return true;if(value=="FALSE"||value=="0"||value=="NO")return false;
			THROW_MPP_RESOURCE_PARSERS("Invalid scene boolean '"+value+"'.",__LINE__,__FILE__,__func__);
		}
		SceneModelSource source(string value)
		{
			utils::StringUtils::toUpper(value);if(value=="MPPMODEL")return SceneModelSource::MppModel;if(value=="BOX")return SceneModelSource::Box;if(value=="SPHERE")return SceneModelSource::Sphere;if(value=="CYLINDER")return SceneModelSource::Cylinder;if(value=="GRID")return SceneModelSource::Grid;
			THROW_MPP_RESOURCE_PARSERS("Unknown scene model source '"+value+"'.",__LINE__,__FILE__,__func__);
		}
	}

	SceneDocument SceneParser::fromFile(string const& filepath)
	{
		auto data=detail::readDocumentRoot(filepath);
		if(data.getName()!="Scene")THROW_MPP_RESOURCE_PARSERS("Scene root must be Scene: "+filepath,__LINE__,__FILE__,__func__);
		rejectUnknown(data,{"version","name","environmentBinding","Camera","Layers","Models","Lights","ParticleEffects"},"Scene");
		SceneDocument document;document.sourcePath=filepath;document.version=data.hasEntry("version")?utils::StringUtils::parseUInt(data.getEntry("version").getValue()):1;document.name=data.hasEntry("name")?data.getEntry("name").getValue():"";
		if(data.hasEntry("environmentBinding"))document.environmentBinding=data.getEntry("environmentBinding").getValue();
		if(data.hasEntry("Camera"))
		{
			auto const& camera=data.getEntry("Camera");rejectUnknown(camera,{"position","target","fov","near","far"},"Camera");
			if(camera.hasEntry("position"))document.camera.position=vec3(camera.getEntry("position").getValue());if(camera.hasEntry("target"))document.camera.target=vec3(camera.getEntry("target").getValue());if(camera.hasEntry("fov"))document.camera.fov=utils::StringUtils::parseFloat(camera.getEntry("fov").getValue());if(camera.hasEntry("near"))document.camera.nearPlane=utils::StringUtils::parseFloat(camera.getEntry("near").getValue());if(camera.hasEntry("far"))document.camera.farPlane=utils::StringUtils::parseFloat(camera.getEntry("far").getValue());
		}
		if(data.hasEntry("Layers"))for(auto const& layer:data.getEntry("Layers")){if(layer.first!="Layer")THROW_MPP_RESOURCE_PARSERS("Unknown field '"+layer.first+"' in Scene/Layers.",__LINE__,__FILE__,__func__);document.layers.push_back(layer.second.getValue());}
		if(data.hasEntry("Models"))for(auto const& entry:data.getEntry("Models"))
		{
			if(entry.first!="Model")THROW_MPP_RESOURCE_PARSERS("Unknown field '"+entry.first+"' in Models.",__LINE__,__FILE__,__func__);auto const& model=entry.second;rejectUnknown(model,{"id","source","file","Primitive","translation","rotation","scale","materialBinding","visible","shadowCaster","Layers"},"Models/Model");
			SceneModelDocument value;value.id=model.getEntry("id").getValue();value.source=source(model.getEntry("source").getValue());if(model.hasEntry("file"))value.file=model.getEntry("file").getValue();if(model.hasEntry("Primitive")){auto const& primitive=model.getEntry("Primitive");rejectUnknown(primitive,{"width","height","depth","radius","topRadius","resolution","segmentsX","segmentsZ","textureRepeatU","textureRepeatV"},"Models/Model/Primitive");auto number=[&](char const* name,float& target){if(primitive.hasEntry(name))target=utils::StringUtils::parseFloat(primitive.getEntry(name).getValue());};number("width",value.primitive.width);number("height",value.primitive.height);number("depth",value.primitive.depth);number("radius",value.primitive.radius);number("topRadius",value.primitive.topRadius);number("textureRepeatU",value.primitive.textureRepeatU);number("textureRepeatV",value.primitive.textureRepeatV);if(primitive.hasEntry("resolution"))value.primitive.resolution=utils::StringUtils::parseUInt(primitive.getEntry("resolution").getValue());if(primitive.hasEntry("segmentsX"))value.primitive.segmentsX=utils::StringUtils::parseUInt(primitive.getEntry("segmentsX").getValue());if(primitive.hasEntry("segmentsZ"))value.primitive.segmentsZ=utils::StringUtils::parseUInt(primitive.getEntry("segmentsZ").getValue());}if(model.hasEntry("translation"))value.translation=vec3(model.getEntry("translation").getValue());if(model.hasEntry("rotation"))value.rotationDegrees=vec3(model.getEntry("rotation").getValue());if(model.hasEntry("scale"))value.scale=vec3(model.getEntry("scale").getValue());if(model.hasEntry("materialBinding"))value.materialBinding=model.getEntry("materialBinding").getValue();if(model.hasEntry("visible"))value.visible=boolean(model.getEntry("visible").getValue());if(model.hasEntry("shadowCaster"))value.shadowCaster=boolean(model.getEntry("shadowCaster").getValue());
			if(model.hasEntry("Layers"))for(auto const& layer:model.getEntry("Layers")){if(layer.first!="Layer")THROW_MPP_RESOURCE_PARSERS("Unknown field '"+layer.first+"' in Layers.",__LINE__,__FILE__,__func__);value.layers.push_back(layer.second.getValue());}document.models.push_back(value);
		}
		if(!data.hasEntry("Layers")){set<string> inferred;for(auto const& model:document.models)for(auto const& layer:model.layers)if(!layer.empty()&&inferred.insert(layer).second)document.layers.push_back(layer);}
		if(data.hasEntry("Lights"))for(auto const& entry:data.getEntry("Lights"))
		{
			if(entry.first!="Light")THROW_MPP_RESOURCE_PARSERS("Unknown field '"+entry.first+"' in Lights.",__LINE__,__FILE__,__func__);auto const& light=entry.second;rejectUnknown(light,{"id","type","colour","intensity","position","direction","range","castsShadows"},"Lights/Light");SceneLightDocument value;value.id=light.getEntry("id").getValue();
			if(light.hasEntry("type")){auto type=light.getEntry("type").getValue();utils::StringUtils::toUpper(type);if(type=="POINT")value.type=SceneLightType::Point;else if(type=="DIRECTIONAL")value.type=SceneLightType::Directional;else THROW_MPP_RESOURCE_PARSERS("Unknown scene light type '"+type+"'.",__LINE__,__FILE__,__func__);}if(light.hasEntry("colour"))value.colour=vec3(light.getEntry("colour").getValue());if(light.hasEntry("intensity"))value.intensity=utils::StringUtils::parseFloat(light.getEntry("intensity").getValue());if(light.hasEntry("position"))value.position=vec3(light.getEntry("position").getValue());if(light.hasEntry("direction"))value.direction=vec3(light.getEntry("direction").getValue());if(light.hasEntry("range"))value.range=utils::StringUtils::parseFloat(light.getEntry("range").getValue());if(light.hasEntry("castsShadows"))value.castsShadows=boolean(light.getEntry("castsShadows").getValue());document.lights.push_back(value);
		}
		if(data.hasEntry("ParticleEffects"))for(auto const& entry:data.getEntry("ParticleEffects"))
		{
			if(entry.first!="ParticleEffect")THROW_MPP_RESOURCE_PARSERS("Unknown field '"+entry.first+"' in ParticleEffects.",__LINE__,__FILE__,__func__);auto const& effect=entry.second;rejectUnknown(effect,{"id","effect","translation","rotation","scale","visible"},"ParticleEffects/ParticleEffect");SceneParticleEffectDocument value;value.id=effect.getEntry("id").getValue();value.effect=effect.getEntry("effect").getValue();if(effect.hasEntry("translation"))value.translation=vec3(effect.getEntry("translation").getValue());if(effect.hasEntry("rotation"))value.rotationDegrees=vec3(effect.getEntry("rotation").getValue());if(effect.hasEntry("scale"))value.scale=vec3(effect.getEntry("scale").getValue());if(effect.hasEntry("visible"))value.visible=boolean(effect.getEntry("visible").getValue());document.particleEffects.push_back(value);
		}
		return document;
	}
}
