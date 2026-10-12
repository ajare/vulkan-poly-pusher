#include <algorithm>
#include <utility>
#include "mpp/RenderSystem.h"
#include "mpp/Scene.h"
#include "mpp/GLErrorCheck.h"

using namespace std;

namespace mpp
{
	Scene::Scene(RenderSystem* renderSystem)
		: mRenderSystem(renderSystem)
		, mClearColour(Colour::Black)
	{
		setViewport(0, 0, mRenderSystem->getWindowWidth(), mRenderSystem->getWindowHeight());
	}

	Scene::~Scene()
	{
		unload();
	}

	void Scene::load()
	{
		if (!mLoaded)
		{
			loadImpl();
			mLoaded = true;
		}
	}

	void Scene::unload()
	{
		if (mLoaded)
		{
			m2dModels.clear();
			m3dModels.clear();

			unloadImpl();
			mLoaded = false;
		}
	}

	void Scene::setViewport(int x, int y, size_t width, size_t height)
	{
		mViewport.x = x;
		mViewport.y = y;
		mViewport.width = (int)width;
		mViewport.height = (int)height;
	}

	ClipRectangle const& Scene::getViewport() const
	{
		return mViewport;
	}

	SceneModel3dPtr Scene::add3dModel(ResourcePtr model)
	{
		auto sm = make_shared<SceneModel3d>(model);
		m3dModels.push_back(sm);

		return sm;
	}

	SceneModel2dPtr Scene::add2dModel(ResourcePtr model, int order)
	{
		auto sm = make_shared<SceneModel2d>(model, mRenderSystem);
		m2dModels.push_back(make_pair(sm, order));

		return sm;
	}

	SceneModel2dPtr Scene::add2dBatch(BatchDataProviderPtr dataProvider, BatchRendererPtr renderer, int order)
	{
		auto sb = make_shared<SceneModel2d>(dataProvider, renderer);
		m2dModels.push_back(make_pair(sb, order));

		return sb;
	}

	void Scene::remove3dModel(SceneModel3dPtr model)
	{
		auto c = (uint32_t)m3dModels.size();

		for (uint32_t i = 0; i < c; ++i)
		{
			if (model == m3dModels[i])
			{
				while (i < (c - 1))
				{
					m3dModels[i] = m3dModels[i + 1];
					i++;
				}

				m3dModels.pop_back();
				break;
			}
		}
	}

	void Scene::remove2dModel(SceneModel2dPtr model)
	{
		auto c = (uint32_t)m2dModels.size();

		for (uint32_t i = 0; i < c; ++i)
		{
			if (model == m2dModels[i].first)
			{
				while (i < (c - 1))
				{
					m2dModels[i] = m2dModels[i + 1];
					i++;
				}

				m2dModels.pop_back();
				break;
			}
		}
	}

	void Scene::remove2dBatch(SceneModel2dPtr batch)
	{
		remove2dModel(batch);
	}

	vector<SceneModel3dPtr> Scene::get3dModelsInView(CameraPtr camera)
	{
		vector<SceneModel3dPtr> inView;

		std::copy_if(m3dModels.begin(), m3dModels.end(), std::back_inserter(inView), [camera](SceneModel3dPtr model) 
		{
			return true;
		});

		return inView;
	}

	vector<SceneModel3dPtr> Scene::get3dModelsInSphere(glm::vec3 const& centre, float radius)
	{
		vector<SceneModel3dPtr> inVolume;
		copy_if(m3dModels.begin(), m3dModels.end(), back_inserter(inVolume), [&](SceneModel3dPtr const& model)
		{
			return model && model->intersectsSphere(centre, radius);
		});
		return inVolume;
	}

	vector<SceneModel3dPtr> Scene::get3dModelsInLayers(CameraPtr camera, vector<string> const& layers)
	{
		auto models = get3dModelsInView(camera);
		if (layers.empty()) return models;
		models.erase(remove_if(models.begin(), models.end(), [&layers](SceneModel3dPtr const& model)
		{
			return none_of(layers.begin(), layers.end(), [&model](string const& layer) { return model->isInRenderLayer(layer); });
		}), models.end());
		return models;
	}

	vector<pair<SceneModel2dPtr, int>> Scene::get2dModelsInView()
	{
		vector<pair<SceneModel2dPtr, int>> inView;

		auto width = mRenderSystem->getWindowWidth();
		auto height = mRenderSystem->getWindowHeight();
		
		std::copy_if(m2dModels.begin(), m2dModels.end(), std::back_inserter(inView), [width, height](pair<SceneModel2dPtr, int> item)
		{
			glm::vec3 bMin, bMax;

			auto model = item.first;
			model->getBounds(bMin, bMax);

			if (bMin.x > width)
			{
				return false;
			}
			if (bMin.y > height)
			{
				return false;
			}
			if (bMax.x < 0)
			{
				return false;
			}
			if (bMax.y < 0)
			{
				return false;
			}

			return true;
		});
		
		return inView;
	}

	void Scene::setClearColour(Colour const& colour)
	{
		mClearColour = colour;
	}

	Colour Scene::getClearColour() const
	{
		return mClearColour;
	}

	void Scene::show2dModels(bool show)
	{
		mShow2dBatches = show;
	}

	bool Scene::show2dModels() const
	{
		return mShow2dBatches;
	}

	void Scene::show3dModels(bool show)
	{
		mShowModels = show;
	}

	bool Scene::show3dModels() const
	{
		return mShowModels;
	}

	void Scene::setPbrLights(vector<PbrLight> lights)
	{
		mPbrLights = std::move(lights);
		mOwnsPbrLights = true;
	}

	vector<PbrLight> const& Scene::getPbrLights() const
	{
		return mPbrLights;
	}

	bool Scene::ownsPbrLights() const
	{
		return mOwnsPbrLights;
	}

	void Scene::update(float frameTime)
	{
		for (auto item: m2dModels)
		{
			auto model = item.first;
			model->update(frameTime);
		}
	}

}