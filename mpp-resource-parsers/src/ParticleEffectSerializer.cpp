#include <array>
#include <sstream>

#include "mpp/resource-parsers/DocumentWriter.h"
#include "mpp/resource-parsers/ParticleEffectSerializer.h"
#include "mpp/resource-parsers/StructuredDataWriteNode.h"

namespace mpp::resource_parsers
{
	namespace
	{
		template<typename Range> std::string values(Range const& range)
		{
			std::ostringstream output; bool first=true;for(auto const& item:range){if(!first)output<<' ';first=false;output<<item;}return output.str();
		}
		char const* shape(uint32_t value){static char const* names[]{"point","line","box","sphere","hemisphere","disc","cone"};return value<7?names[value]:"point";}
		char const* billboard(uint32_t value){static char const* names[]{"cameraFacing","screenAligned","cylindrical","axisLocked","velocityAligned","velocityStretched"};return value<6?names[value]:"cameraFacing";}
		char const* blendClass(uint32_t value){static char const* names[]{"additive","alpha","weightedOit"};return value<3?names[value]:"additive";}
		char const* collisionResponse(uint32_t value){static char const* names[]{"bounce","slide","stop","kill","spawnSecondaryEffect"};return value<5?names[value]:"bounce";}
		char const* eventTrigger(ParticleEventTrigger value){static char const* names[]{"spawn","death","collision","age"};auto index=uint32_t(value);return index<4?names[index]:"spawn";}
		char const* eventAction(ParticleEventAction value){static char const* names[]{"secondaryParticleBurst","decal","audio","light","gameplayCallback"};auto index=uint32_t(value);return index<5?names[index]:"secondaryParticleBurst";}
		std::string collisionSources(uint32_t mask)
		{
			std::string result;
			auto append=[&](char const* name){if(!result.empty())result+=',';result+=name;};
			if(mask&uint32_t(ParticleCollisionSource::ScreenSpace))append("screenSpace");
			if(mask&uint32_t(ParticleCollisionSource::Analytical))append("analytical");
			if(mask&uint32_t(ParticleCollisionSource::SignedDistanceField))append("signedDistanceField");
			return result;
		}
		void curve(StructuredDataWriteNode* parent,char const* name,ParticleCurve const& value)
		{
			auto node=parent->createChild(name);node->createChild("default")->setValue(value.defaultValue);auto keys=node->createChild("Keys");for(auto const& key:value.keys){auto item=keys->createChild("Key");item->createChild("time")->setValue(key.time);item->createChild("value")->setValue(key.value);}
		}
	}

	void ParticleEffectSerializer::toFile(ParticleEffectSpecification const& specification, std::string const& filepath)
	{
		StructuredDataWriteNode root("ParticleEffect");root.createChild("version")->setValue(specification.version);root.createChild("name")->setValue(specification.name);root.createChild("maximumParticleCount")->setValue(specification.maximumParticleCount);
		if (specification.bounds)
		{
			auto bounds = root.createChild("Bounds");
			bounds->createChild("center")->setValue(values(std::array<float, 3>{
				specification.bounds->center.x, specification.bounds->center.y, specification.bounds->center.z }));
			bounds->createChild("size")->setValue(values(std::array<float, 3>{
				specification.bounds->size.x, specification.bounds->size.y, specification.bounds->size.z }));
		}
		auto emitters=root.createChild("Emitters");
		for(auto const& authored:specification.emitterTemplates)
		{
			auto node=emitters->createChild("Emitter");auto const& emitter=authored.value;auto const& sim=emitter.simulation;auto const& appearance=emitter.appearance;
			node->createChild("name")->setValue(authored.name);node->createChild("maximumParticleCount")->setValue(sim.shapeSeedModulesBudget[3]);
			std::array<float,16> transform{};for(int column=0;column<4;++column)for(int row=0;row<4;++row)transform[size_t(column*4+row)]=emitter.localTransform[column][row];node->createChild("transform")->setValue(values(transform));
			auto spawn=node->createChild("Spawn");spawn->createChild("shape")->setValue(shape(sim.shapeSeedModulesBudget[0]));spawn->createChild("shapeParameters")->setValue(values(sim.shapeParameters));spawn->createChild("seed")->setValue(sim.shapeSeedModulesBudget[1]);spawn->createChild("mode")->setValue(sim.emissionState[0]?"burst":"continuous");spawn->createChild("enabled")->setValue(sim.emissionState[1]!=0);spawn->createChild("rate")->setValue(sim.emissionRateAndPadding[0]);spawn->createChild("burstCount")->setValue(sim.emissionState[2]);spawn->createChild("initialVelocityMin")->setValue(values(sim.initialVelocityMin));spawn->createChild("initialVelocityMax")->setValue(values(sim.initialVelocityMax));spawn->createChild("colourMin")->setValue(values(sim.colourMin));spawn->createChild("colourMax")->setValue(values(sim.colourMax));spawn->createChild("lifetime")->setValue(values(std::array<float,2>{sim.lifetimeSizeRanges[0],sim.lifetimeSizeRanges[1]}));spawn->createChild("size")->setValue(values(std::array<float,2>{sim.lifetimeSizeRanges[2],sim.lifetimeSizeRanges[3]}));spawn->createChild("rotation")->setValue(values(std::array<float,2>{sim.rotationRanges[0],sim.rotationRanges[1]}));spawn->createChild("angularVelocity")->setValue(values(std::array<float,2>{sim.rotationRanges[2],sim.rotationRanges[3]}));
			if(!authored.events.empty()){auto events=node->createChild("Events");for(auto const& event:authored.events){auto item=events->createChild("Event");item->createChild("trigger")->setValue(eventTrigger(event.trigger));item->createChild("action")->setValue(eventAction(event.action));if(event.action==ParticleEventAction::SecondaryParticleBurst){item->createChild("targetEmitter")->setValue(event.targetEmitter);item->createChild("count")->setValue(event.count);}if(event.trigger==ParticleEventTrigger::Age)item->createChild("age")->setValue(event.age);if(event.payload!=0u)item->createChild("payload")->setValue(event.payload);}}
			auto behaviours=node->createChild("Behaviours");auto modules=sim.shapeSeedModulesBudget[2];if(modules&uint32_t(ParticleBehaviourModule::Gravity)){auto block=behaviours->createChild("Gravity");block->createChild("acceleration")->setValue(values(std::array<float,3>{sim.gravityAndDrag[0],sim.gravityAndDrag[1],sim.gravityAndDrag[2]}));}if(modules&uint32_t(ParticleBehaviourModule::Drag)){auto block=behaviours->createChild("Drag");block->createChild("coefficient")->setValue(sim.gravityAndDrag[3]);}if(modules&uint32_t(ParticleBehaviourModule::Noise)){auto block=behaviours->createChild("Noise");block->createChild("frequency")->setValue(values(std::array<float,3>{sim.noiseFrequencyStrength[0],sim.noiseFrequencyStrength[1],sim.noiseFrequencyStrength[2]}));block->createChild("strength")->setValue(sim.noiseFrequencyStrength[3]);block->createChild("scroll")->setValue(values(std::array<float,3>{sim.noiseScrollAndTimeScale[0],sim.noiseScrollAndTimeScale[1],sim.noiseScrollAndTimeScale[2]}));block->createChild("timeScale")->setValue(sim.noiseScrollAndTimeScale[3]);}
			if(modules&uint32_t(ParticleBehaviourModule::CurlNoise)){auto block=behaviours->createChild("CurlNoise");block->createChild("frequency")->setValue(values(std::array<float,3>{sim.curlNoiseFrequencyStrength[0],sim.curlNoiseFrequencyStrength[1],sim.curlNoiseFrequencyStrength[2]}));block->createChild("strength")->setValue(sim.curlNoiseFrequencyStrength[3]);block->createChild("scroll")->setValue(values(std::array<float,3>{sim.curlNoiseScrollAndTimeScale[0],sim.curlNoiseScrollAndTimeScale[1],sim.curlNoiseScrollAndTimeScale[2]}));block->createChild("timeScale")->setValue(sim.curlNoiseScrollAndTimeScale[3]);}
			if(modules&uint32_t(ParticleBehaviourModule::Turbulence)){auto block=behaviours->createChild("Turbulence");block->createChild("frequency")->setValue(values(std::array<float,3>{sim.turbulenceFrequencyStrength[0],sim.turbulenceFrequencyStrength[1],sim.turbulenceFrequencyStrength[2]}));block->createChild("strength")->setValue(sim.turbulenceFrequencyStrength[3]);block->createChild("scroll")->setValue(values(std::array<float,3>{sim.turbulenceScrollAndTimeScale[0],sim.turbulenceScrollAndTimeScale[1],sim.turbulenceScrollAndTimeScale[2]}));block->createChild("timeScale")->setValue(sim.turbulenceScrollAndTimeScale[3]);block->createChild("octaves")->setValue(uint32_t(sim.turbulenceOctavesLacunarityGain[0]));block->createChild("lacunarity")->setValue(sim.turbulenceOctavesLacunarityGain[1]);block->createChild("gain")->setValue(sim.turbulenceOctavesLacunarityGain[2]);}
			if(modules&uint32_t(ParticleBehaviourModule::VectorField)){auto block=behaviours->createChild("VectorField");block->createChild("frequency")->setValue(values(std::array<float,3>{sim.vectorFieldFrequencyStrength[0],sim.vectorFieldFrequencyStrength[1],sim.vectorFieldFrequencyStrength[2]}));block->createChild("strength")->setValue(sim.vectorFieldFrequencyStrength[3]);block->createChild("scroll")->setValue(values(std::array<float,3>{sim.vectorFieldScrollAndTimeScale[0],sim.vectorFieldScrollAndTimeScale[1],sim.vectorFieldScrollAndTimeScale[2]}));block->createChild("timeScale")->setValue(sim.vectorFieldScrollAndTimeScale[3]);}
			if(modules&uint32_t(ParticleBehaviourModule::Collision)){auto block=behaviours->createChild("Collision");block->createChild("sources")->setValue(collisionSources(sim.collisionConfiguration[0]));block->createChild("response")->setValue(collisionResponse(sim.collisionConfiguration[1]));block->createChild("restitution")->setValue(sim.collisionParameters[0]);block->createChild("friction")->setValue(sim.collisionParameters[1]);block->createChild("radiusScale")->setValue(sim.collisionParameters[2]);block->createChild("screenSpaceThickness")->setValue(sim.collisionParameters[3]);}
			auto curves=node->createChild("Curves");curve(curves,"Size",emitter.curves[size_t(ParticleScalarCurve::Size)]);curve(curves,"Alpha",emitter.curves[size_t(ParticleScalarCurve::Alpha)]);curve(curves,"VelocityMultiplier",emitter.curves[size_t(ParticleScalarCurve::VelocityMultiplier)]);curve(curves,"Drag",emitter.curves[size_t(ParticleScalarCurve::Drag)]);curve(curves,"RotationSpeed",emitter.curves[size_t(ParticleScalarCurve::RotationSpeed)]);curve(curves,"EmissiveIntensity",emitter.curves[size_t(ParticleScalarCurve::EmissiveIntensity)]);auto colour=curves->createChild("Colour");colour->createChild("default")->setValue(values(emitter.colourGradient.defaultColour));auto colourKeys=colour->createChild("Keys");for(auto const& key:emitter.colourGradient.keys){auto item=colourKeys->createChild("Key");item->createChild("time")->setValue(key.time);item->createChild("colour")->setValue(values(key.colour));}
			if(!authored.meshModel.empty()){auto mesh=node->createChild("Mesh");mesh->createChild("model")->setValue(authored.meshModel);if(!authored.meshMaterial.empty())mesh->createChild("material")->setValue(authored.meshMaterial);}
			if(authored.value.lighting.flagsAndPadding[0]!=0u)
			{
				auto lighting=node->createChild("Lighting");auto const& value=authored.value.lighting;auto flags=value.flagsAndPadding[0];
				lighting->createChild("proxyLight")->setValue((flags&uint32_t(ParticleLightingFlag::ProxyLight))!=0u);
				lighting->createChild("lightInjection")->setValue((flags&uint32_t(ParticleLightingFlag::PbrLightInjection))!=0u);
				lighting->createChild("volumetricLighting")->setValue((flags&uint32_t(ParticleLightingFlag::VolumetricContribution))!=0u);
				lighting->createChild("colour")->setValue(values(std::array<float,3>{value.colourAndIntensity[0],value.colourAndIntensity[1],value.colourAndIntensity[2]}));
				lighting->createChild("intensity")->setValue(value.colourAndIntensity[3]);lighting->createChild("range")->setValue(value.rangeAndVolumetric[0]);lighting->createChild("volumetricIntensity")->setValue(value.rangeAndVolumetric[1]);
			}
			auto block=node->createChild("Appearance");if(!authored.albedoTexture.empty())block->createChild("texture")->setValue(authored.albedoTexture);block->createChild("tint")->setValue(values(std::array<float,3>{appearance.tintAndAlpha[0],appearance.tintAndAlpha[1],appearance.tintAndAlpha[2]}));block->createChild("alpha")->setValue(appearance.tintAndAlpha[3]);block->createChild("emissiveIntensity")->setValue(appearance.appearance[0]);block->createChild("softFadeDistance")->setValue(appearance.appearance[1]);block->createChild("maximumDrawDistance")->setValue(appearance.culling[0]);block->createChild("minimumProjectedSize")->setValue(appearance.culling[1]);block->createChild("atlasColumns")->setValue(appearance.textureAndAtlas[2]);block->createChild("atlasRows")->setValue(appearance.textureAndAtlas[3]);block->createChild("frameCount")->setValue(appearance.modes[0]);auto playback=appearance.modes[1]&ParticleTexturePlaybackMask;block->createChild("animation")->setValue(playback==uint32_t(ParticleTextureAnimation::FrameOverLife)?"frameOverLife":playback==uint32_t(ParticleTextureAnimation::FixedRate)?"fixedRate":"none");block->createChild("randomStart")->setValue((appearance.modes[1]&ParticleTextureRandomStartBit)!=0);block->createChild("animationRate")->setValue(appearance.appearance[2]);block->createChild("billboard")->setValue(billboard(appearance.modes[2]));block->createChild("blendClass")->setValue(blendClass(appearance.modes[3]));block->createChild("depthSort")->setValue(appearance.sorting[0]==uint32_t(ParticleSortMode::BackToFront));block->createChild("distortion")->setValue(appearance.sorting[2]!=0u);block->createChild("distortionStrength")->setValue(appearance.culling[3]);
		}
		if(!specification.childEffects.empty())
		{
			auto children=root.createChild("ChildEffects");
			for(auto const& child:specification.childEffects)
			{
				auto node=children->createChild("ChildEffect");node->createChild("effect")->setValue(child.effect);
				std::array<float,16> transform{};for(int column=0;column<4;++column)for(int row=0;row<4;++row)transform[size_t(column*4+row)]=child.transform[column][row];
				node->createChild("transform")->setValue(values(transform));node->createChild("seed")->setValue(child.seed);
			}
		}
		detail::writeDocument(root.toStructuredData(),filepath);
	}
}
