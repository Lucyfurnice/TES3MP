#include <apps/openmw/mwrender/animation.hpp>
#include <apps/openmw/mwrender/bulletdebugdraw.hpp>
#include <apps/openmw/mwrender/camera.hpp>
#include <apps/openmw/mwrender/landmanager.hpp>
#include <apps/openmw/mwrender/npcanimation.hpp>
#include <apps/openmw/mwrender/objects.hpp>
#include <apps/openmw/mwrender/postprocessor.hpp>
#include <apps/openmw/mwrender/renderingmanager.hpp>
#include <apps/openmw/mwrender/sky.hpp>
#include <apps/openmw/mwrender/util.hpp>

#include <apps/openmw/mwbase/environment.hpp>
#include <apps/openmw/mwbase/world.hpp>
#include <apps/openmw/mwworld/cellstore.hpp>
#include <apps/openmw/mwworld/class.hpp>
#include <apps/openmw/mwworld/esmstore.hpp>

#include <components/debug/debugdraw.hpp>
#include <components/esmterrain/storage.hpp>
#include <components/misc/resourcehelpers.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/sceneutil/lightmanager.hpp>
#include <components/sceneutil/positionattitudetransform.hpp>
#include <components/sceneutil/rtt.hpp>
#include <components/sceneutil/shadow.hpp>
#include <components/sceneutil/statesetupdater.hpp>
#include <components/sceneutil/stateupdater.hpp>
#include <components/sceneutil/unrefqueue.hpp>
#include <components/sceneutil/workqueue.hpp>
#include <components/terrain/world.hpp>

#include <osg/Geometry>
#include <osg/Group>
#include <osg/LightModel>
#include <osg/OcclusionQueryNode>
#include <osg/PositionAttitudeTransform>
#include <osgParticle/BoxPlacer>
#include <osgParticle/ParticleSystem>

#include <map>

// Complete stub types for forward-declared members owned by MWRender::RenderingManager
// and PostProcessor so destructors compile without viewer/GL TUs.
namespace MWRender
{
    class NavMesh
    {
    };
    class ActorsPaths
    {
    };
    class RecastMesh
    {
    };
    class Pathgrid
    {
    };
    class Water
    {
    };
    class TerrainStorage
    {
    };
    class FogManager
    {
    };
    class ScreenshotManager
    {
    };
    class EffectManager
    {
    };
    class ObjectPaging
    {
    };
    class Groundcover
    {
    };
    class IntersectionVisitorWithIgnoreList : public osg::Referenced
    {
    };

    class PingPongCull : public osg::NodeCallback
    {
    };
    class DistortionCallback : public osg::Referenced
    {
    };

    struct MoonUpdater : public osg::Referenced
    {
    };
    class SunUpdater : public osg::Referenced
    {
    };
    class SunFlashCallback : public osg::Referenced
    {
    };
    class SunGlareCallback : public osg::Referenced
    {
    };

    Sun::~Sun() = default;
    Moon::~Moon() = default;
    void Sun::adjustTransparency(float /*ratio*/) {}
    void Moon::adjustTransparency(float /*ratio*/) {}

    // ---- MWRender::Util ----

    void overrideFirstRootTexture(
        VFS::Path::NormalizedView /*texture*/, Resource::ResourceSystem* /*resourceSystem*/, osg::Node& /*node*/)
    {
    }

    void overrideTexture(
        VFS::Path::NormalizedView /*texture*/, Resource::ResourceSystem* /*resourceSystem*/, osg::Node& /*node*/)
    {
    }

    bool shouldAddMSAAIntermediateTarget()
    {
        return false;
    }

    const osg::ref_ptr<osg::LightModel>& getVFXLightModelInstance()
    {
        static const osg::ref_ptr<osg::LightModel> instance = new osg::LightModel;
        return instance;
    }

    // ---- MWRender::DebugDrawer ----

    DebugDrawer::DebugDrawer(osg::ref_ptr<osg::Group> parentNode, btCollisionWorld* world, int /*debugMode*/)
        : mParentNode(std::move(parentNode))
        , mWorld(world)
        , mDebugOn(false)
    {
    }

    DebugDrawer::~DebugDrawer() = default;

    void DebugDrawer::step() {}
    void DebugDrawer::drawLine(const btVector3& /*from*/, const btVector3& /*to*/, const btVector3& /*color*/) {}
    void DebugDrawer::drawTriangle(const btVector3& /*v0*/, const btVector3& /*v1*/, const btVector3& /*v2*/,
        const btVector3& /*color*/, btScalar)
    {
    }
    void DebugDrawer::addCollision(const btVector3& /*orig*/, const btVector3& /*normal*/) {}
    void DebugDrawer::showCollisions() {}
    void DebugDrawer::drawSphere(btScalar /*radius*/, const btTransform& /*transform*/, const btVector3& /*color*/) {}
    void DebugDrawer::reportErrorWarning(const char* /*warningString*/) {}
    void DebugDrawer::setDebugMode(int isOn)
    {
        mDebugOn = (isOn != 0);
    }
    int DebugDrawer::getDebugMode() const
    {
        return mDebugOn ? 1 : 0;
    }

    // ---- MWRender::PostProcessor ----

    PostProcessor::PostProcessor(
        RenderingManager& rendering, osgViewer::Viewer* viewer, osg::Group* rootNode, const VFS::Manager* vfs)
        : mRootNode(rootNode)
        , mRendering(rendering)
        , mViewer(viewer)
        , mVFS(vfs)
        , mGLSLVersion(0)
        , mWidth(0)
        , mHeight(0)
        , mSamples(0)
    {
    }

    PostProcessor::~PostProcessor() = default;

    void PostProcessor::traverse(osg::NodeVisitor& /*nv*/) {}
    void PostProcessor::resize() {}

    PostProcessor::Status PostProcessor::enableTechnique(
        std::shared_ptr<Fx::Technique> /*technique*/, std::optional<int> /*location*/)
    {
        return Status_Unchanged;
    }

    PostProcessor::Status PostProcessor::disableTechnique(std::shared_ptr<Fx::Technique> /*technique*/, bool /*dirty*/)
    {
        return Status_Unchanged;
    }

    bool PostProcessor::isTechniqueEnabled(const std::shared_ptr<Fx::Technique>& /*technique*/) const
    {
        return false;
    }

    void PostProcessor::toggleMode() {}

    std::shared_ptr<Fx::Technique> PostProcessor::loadTechnique(
        VFS::Path::NormalizedView /*path*/, bool /*loadNextFrame*/)
    {
        return nullptr;
    }

    std::shared_ptr<Fx::Technique> PostProcessor::loadTechnique(std::string_view /*name*/, bool /*loadNextFrame*/)
    {
        return nullptr;
    }

    PostProcessor::TechniqueList PostProcessor::getChain()
    {
        return {};
    }

    void PostProcessor::disable() {}
    void PostProcessor::enable() {}
    void PostProcessor::disableDynamicShaders() {}
    int PostProcessor::renderWidth() const
    {
        return mWidth;
    }
    int PostProcessor::renderHeight() const
    {
        return mHeight;
    }
    void PostProcessor::triggerShaderReload() {}
    void PostProcessor::loadChain() {}
    void PostProcessor::saveChain() {}

    // ---- MWRender::Camera ----

    Camera::Camera(osg::Camera* camera)
        : mHeightScale(1.f)
        , mCollisionType(0)
        , mCamera(camera)
        , mAnimation(nullptr)
        , mFirstPersonView(true)
        , mMode(Mode::FirstPerson)
        , mVanityAllowed(false)
        , mDeferredRotationAllowed(false)
        , mProcessViewChange(false)
        , mHeight(0.f)
        , mPitch(0.f)
        , mYaw(0.f)
        , mRoll(0.f)
        , mCameraDistance(0.f)
        , mPreferredCameraDistance(0.f)
        , mFocalPointTransitionSpeedCoef(1.f)
        , mSkipFocalPointTransition(false)
        , mPreviousTransitionInfluence(0.f)
        , mShowCrosshair(false)
        , mDeferredRotationDisabled(false)
    {
    }

    Camera::~Camera() = default;

    void Camera::setFocalPointTargetOffset(const osg::Vec2d& v)
    {
        mFocalPointTargetOffset = v;
    }
    void Camera::instantTransition() {}
    void Camera::updateCamera(osg::Camera* /*cam*/) {}
    void Camera::rotateCameraToTrackingPtr() {}
    void Camera::setPitch(float angle, bool /*force*/)
    {
        mPitch = angle;
    }
    void Camera::setYaw(float angle, bool /*force*/)
    {
        mYaw = angle;
    }
    osg::Quat Camera::getOrient() const
    {
        return osg::Quat();
    }
    void Camera::toggleViewMode(bool /*force*/)
    {
        mFirstPersonView = !mFirstPersonView;
        mMode = mFirstPersonView ? Mode::FirstPerson : Mode::ThirdPerson;
    }
    bool Camera::toggleVanityMode(bool enable)
    {
        if (enable)
            mMode = Mode::Vanity;
        else
            mMode = mFirstPersonView ? Mode::FirstPerson : Mode::ThirdPerson;
        return true;
    }
    void Camera::applyDeferredPreviewRotationToPlayer(float /*dt*/) {}
    void Camera::setSneakOffset(float /*offset*/) {}
    void Camera::processViewChange() {}
    void Camera::update(float /*duration*/, bool /*paused*/) {}
    void Camera::setAnimation(NpcAnimation* anim)
    {
        mAnimation = anim;
    }
    void Camera::setStaticPosition(const osg::Vec3d& pos)
    {
        mPosition = pos;
    }
    void Camera::setMode(Mode mode, bool /*force*/)
    {
        mMode = mode;
        if (mode == Mode::FirstPerson)
            mFirstPersonView = true;
        else if (mode == Mode::ThirdPerson)
            mFirstPersonView = false;
    }
    void Camera::calculateDeferredRotation() {}

    // ---- MWRender::SkyManager ----

    SkyManager::SkyManager(osg::Group* /*parentNode*/, osg::Group* /*rootNode*/, osg::Camera* camera,
        Resource::SceneManager* sceneManager, bool /*enableSkyRTT*/)
        : mSceneManager(sceneManager)
        , mCamera(camera)
        , mAtmosphereNightRoll(0.f)
        , mCreated(false)
        , mIsStorm(false)
        , mTimescaleClouds(false)
        , mCloudAnimationTimer(0.f)
        , mCloudBlendFactor(0.f)
        , mCloudSpeed(0.f)
        , mStarsOpacity(0.f)
        , mRainSpeed(0.f)
        , mRainDiameter(0.f)
        , mRainMinHeight(0.f)
        , mRainMaxHeight(0.f)
        , mRainEntranceSpeed(0.f)
        , mRainMaxRaindrops(0)
        , mRainRipplesEnabled(false)
        , mSnowRipplesEnabled(false)
        , mWindSpeed(0.f)
        , mBaseWindSpeed(0.f)
        , mEnabled(false)
        , mSunglareEnabled(false)
        , mPrecipitationAlpha(0.f)
        , mDirtyParticlesEffect(false)
    {
    }

    SkyManager::~SkyManager() = default;

    void SkyManager::update(float /*duration*/) {}
    void SkyManager::setEnabled(bool enabled)
    {
        mEnabled = enabled;
    }
    int SkyManager::getMasserPhase() const
    {
        return 0;
    }
    int SkyManager::getSecundaPhase() const
    {
        return 0;
    }
    void SkyManager::setMoonColour(bool /*red*/) {}
    void SkyManager::setWeather(const WeatherResult& weather)
    {
        mWindSpeed = weather.mCurrentWindSpeed;
        mBaseWindSpeed = weather.mBaseWindSpeed;
    }
    void SkyManager::sunEnable() {}
    void SkyManager::sunDisable() {}
    bool SkyManager::isEnabled()
    {
        return mEnabled;
    }
    bool SkyManager::hasRain() const
    {
        return false;
    }
    bool SkyManager::getRainRipplesEnabled() const
    {
        return false;
    }
    float SkyManager::getPrecipitationAlpha() const
    {
        return 0.f;
    }
    void SkyManager::setStormParticleDirection(const osg::Vec3f& /*direction*/) {}
    void SkyManager::setSunDirection(const osg::Vec3f& /*direction*/) {}
    void SkyManager::setMasserState(const MoonState& /*state*/) {}
    void SkyManager::setSecundaState(const MoonState& /*state*/) {}
    void SkyManager::setGlareTimeOfDayFade(float /*val*/) {}
    void SkyManager::setWaterEnabled(bool /*enabled*/) {}
    void SkyManager::setWaterHeight(float /*height*/) {}
    void SkyManager::listAssetsToPreload(
        std::vector<VFS::Path::Normalized>& /*models*/, std::vector<VFS::Path::Normalized>& /*textures*/)
    {
    }
    float SkyManager::getBaseWindSpeed() const
    {
        return mBaseWindSpeed;
    }
    void SkyManager::setSunglare(bool enabled)
    {
        mSunglareEnabled = enabled;
    }

    // ---- MWRender::Objects ----

    Objects::Objects(
        Resource::ResourceSystem* resourceSystem, const osg::ref_ptr<osg::Group>& rootNode, SceneUtil::UnrefQueue& unrefQueue)
        : mRootNode(rootNode)
        , mResourceSystem(resourceSystem)
        , mUnrefQueue(unrefQueue)
    {
    }

    Objects::~Objects() = default;

    void Objects::insertBegin(const MWWorld::Ptr& ptr)
    {
        if (ptr.getRefData().getBaseNode())
            return;

        osg::ref_ptr<SceneUtil::PositionAttitudeTransform> node = new SceneUtil::PositionAttitudeTransform;
        node->setPosition(ptr.getRefData().getPosition().asVec3());
        ptr.getRefData().setBaseNode(node);
    }

    void Objects::insertModel(const MWWorld::Ptr& ptr, const std::string& model, bool /*allowLight*/)
    {
        insertBegin(ptr);
        const MWWorld::CellStore* cell = ptr.isInCell() ? ptr.getCell() : nullptr;
        osg::ref_ptr<osg::Group> cellNode = mRootNode;
        if (cell)
        {
            auto& entry = mCellSceneNodes[cell];
            if (!entry)
                entry = new osg::Group;
            cellNode = entry;
        }
        mObjects[ptr.getBase()] = new ObjectAnimation(ptr, model, mResourceSystem, true, false);
        (void)cellNode;
    }

    void Objects::insertNPC(const MWWorld::Ptr& ptr)
    {
        insertBegin(ptr);
        mObjects[ptr.getBase()] = new NpcAnimation(ptr, mRootNode, mResourceSystem, true, NpcAnimation::VM_Normal);
    }

    void Objects::insertCreature(const MWWorld::Ptr& ptr, const std::string& model, bool /*weaponsShields*/)
    {
        insertBegin(ptr);
        mObjects[ptr.getBase()] = new ObjectAnimation(ptr, model, mResourceSystem, true, false);
    }

    Animation* Objects::getAnimation(const MWWorld::Ptr& ptr)
    {
        auto it = mObjects.find(ptr.getBase());
        return it == mObjects.end() ? nullptr : it->second.get();
    }

    const Animation* Objects::getAnimation(const MWWorld::ConstPtr& ptr) const
    {
        auto it = mObjects.find(ptr.getBase());
        return it == mObjects.end() ? nullptr : it->second.get();
    }

    bool Objects::removeObject(const MWWorld::Ptr& ptr)
    {
        ptr.getRefData().setBaseNode(nullptr);
        return mObjects.erase(ptr.getBase()) > 0;
    }

    void Objects::removeCell(const MWWorld::CellStore* store)
    {
        mCellSceneNodes.erase(store);
    }

    void Objects::updatePtr(const MWWorld::Ptr& old, const MWWorld::Ptr& cur)
    {
        auto it = mObjects.find(old.getBase());
        if (it != mObjects.end())
        {
            osg::ref_ptr<Animation> anim = std::move(it->second);
            mObjects.erase(it);
            if (anim)
                anim->updatePtr(cur);
            mObjects[cur.getBase()] = std::move(anim);
        }
    }

    // ---- MWRender::RenderingManager ----

    RenderingManager::RenderingManager(osgViewer::Viewer* viewer, osg::ref_ptr<osg::Group> rootNode,
        Resource::ResourceSystem* resourceSystem, SceneUtil::WorkQueue* workQueue, DetourNavigator::Navigator& navigator,
        const MWWorld::GroundcoverStore& groundcoverStore, SceneUtil::UnrefQueue& unrefQueue)
        : mSkyBlending(false)
        , mViewer(viewer)
        , mRootNode(rootNode ? std::move(rootNode) : osg::ref_ptr<osg::Group>(new osg::Group))
        , mResourceSystem(resourceSystem)
        , mWorkQueue(workQueue)
        , mSunLight(new osg::Light)
        , mNavigator(navigator)
        , mObjects(std::make_unique<Objects>(resourceSystem, mRootNode, unrefQueue))
        , mTerrain(nullptr)
        , mObjectPaging(nullptr)
        , mGroundcover(nullptr)
        , mSky(std::make_unique<SkyManager>(mRootNode, mRootNode, nullptr,
              resourceSystem ? resourceSystem->getSceneManager() : nullptr, false))
        , mPostProcessor(new PostProcessor(*this, viewer, mRootNode, nullptr))
        , mCamera(std::make_unique<Camera>(nullptr))
        , mDebugDraw(new Debug::DebugDrawer())
        , mNightEyeFactor(0.f)
        , mNearClip(1.f)
        , mViewDistance(8192.f)
        , mFieldOfViewOverridden(false)
        , mFieldOfViewOverride(55.f)
        , mFieldOfView(55.f)
        , mFirstPersonFieldOfView(55.f)
        , mGroundCoverStore(groundcoverStore)
    {
    }

    RenderingManager::~RenderingManager() = default;

    osgUtil::IncrementalCompileOperation* RenderingManager::getIncrementalCompileOperation()
    {
        return nullptr;
    }

    MWRender::Objects& RenderingManager::getObjects()
    {
        return *mObjects;
    }

    Resource::ResourceSystem* RenderingManager::getResourceSystem()
    {
        return mResourceSystem;
    }

    SceneUtil::WorkQueue* RenderingManager::getWorkQueue()
    {
        return mWorkQueue;
    }

    Terrain::World* RenderingManager::getTerrain()
    {
        return mTerrain;
    }

    void RenderingManager::preloadCommonAssets() {}

    double RenderingManager::getReferenceTime() const
    {
        return 0.0;
    }

    SceneUtil::LightManager* RenderingManager::getLightRoot()
    {
        return mSceneRoot.get();
    }

    void RenderingManager::setNightEyeFactor(float factor)
    {
        mNightEyeFactor = factor;
    }

    void RenderingManager::setAmbientColour(const osg::Vec4f& colour)
    {
        mAmbientColor = colour;
    }

    int RenderingManager::skyGetMasserPhase() const
    {
        return mSky->getMasserPhase();
    }

    int RenderingManager::skyGetSecundaPhase() const
    {
        return mSky->getSecundaPhase();
    }

    void RenderingManager::skySetMoonColour(bool red)
    {
        mSky->setMoonColour(red);
    }

    void RenderingManager::setSunDirection(const osg::Vec3f& /*direction*/) {}
    void RenderingManager::setSunColour(const osg::Vec4f& /*diffuse*/, const osg::Vec4f& /*specular*/, float /*sunVis*/) {}
    void RenderingManager::configureAmbient(const MWWorld::Cell& /*cell*/) {}
    void RenderingManager::configureFog(const MWWorld::Cell& /*cell*/) {}
    void RenderingManager::configureFog(
        float /*fogDepth*/, float /*underwaterFog*/, float /*dlFactor*/, float /*dlOffset*/, const osg::Vec4f& /*colour*/)
    {
    }

    void RenderingManager::addCell(const MWWorld::CellStore* /*store*/) {}

    void RenderingManager::removeCell(const MWWorld::CellStore* store)
    {
        mObjects->removeCell(store);
    }

    void RenderingManager::enableTerrain(bool /*enable*/, ESM::RefId /*worldspace*/) {}

    void RenderingManager::updatePtr(const MWWorld::Ptr& old, const MWWorld::Ptr& updated)
    {
        mObjects->updatePtr(old, updated);
    }

    void RenderingManager::rotateObject(const MWWorld::Ptr& ptr, const osg::Quat& rot)
    {
        if (ptr == mCamera->getTrackingPtr() && mPlayerNode)
            mPlayerNode->setAttitude(rot);
        if (auto* node = ptr.getRefData().getBaseNode())
            node->setAttitude(rot);
    }

    void RenderingManager::moveObject(const MWWorld::Ptr& ptr, const osg::Vec3f& pos)
    {
        if (ptr == mCamera->getTrackingPtr() && mPlayerNode)
            mPlayerNode->setPosition(pos);
        if (auto* node = ptr.getRefData().getBaseNode())
            node->setPosition(pos);
    }

    void RenderingManager::scaleObject(const MWWorld::Ptr& ptr, const osg::Vec3f& scale)
    {
        if (auto* node = ptr.getRefData().getBaseNode())
            node->setScale(scale);
    }

    void RenderingManager::removeObject(const MWWorld::Ptr& ptr)
    {
        mObjects->removeObject(ptr);
    }

    void RenderingManager::setWaterEnabled(bool /*enabled*/) {}
    void RenderingManager::setWaterHeight(float /*level*/) {}
    void RenderingManager::screenshot(osg::Image* /*image*/, int /*w*/, int /*h*/) {}

    RenderingManager::RayResult RenderingManager::castRay(const osg::Vec3f& /*origin*/, const osg::Vec3f& /*dest*/,
        bool /*ignorePlayer*/, bool /*ignoreActors*/, std::span<const MWWorld::Ptr> /*ignoreList*/)
    {
        return RayResult{ false, {}, {}, MWWorld::Ptr(), {}, 1.f };
    }

    RenderingManager::RayResult RenderingManager::castCameraToViewportRay(
        const float /*nX*/, const float /*nY*/, float /*maxDistance*/, bool /*ignorePlayer*/, bool /*ignoreActors*/)
    {
        return RayResult{ false, {}, {}, MWWorld::Ptr(), {}, 1.f };
    }

    osg::Vec2f RenderingManager::getScreenCoords(const osg::BoundingBox& /*bb*/)
    {
        return osg::Vec2f(0.f, 0.f);
    }

    void RenderingManager::setSkyEnabled(bool enabled)
    {
        mSky->setEnabled(enabled);
    }

    bool RenderingManager::toggleRenderMode(RenderMode /*mode*/)
    {
        return false;
    }

    SkyManager* RenderingManager::getSkyManager()
    {
        return mSky.get();
    }

    void RenderingManager::spawnEffect(VFS::Path::NormalizedView /*model*/, std::string_view /*texture*/,
        const osg::Vec3f& /*worldPosition*/, float /*scale*/, bool /*isMagicVFX*/, bool /*useAmbientLight*/,
        std::string_view /*effectId*/, bool /*loop*/)
    {
    }

    void RenderingManager::removeEffect(std::string_view /*effectId*/) {}

    void RenderingManager::clear()
    {
        if (mPlayerAnimation)
            mPlayerAnimation = nullptr;
    }

    void RenderingManager::notifyWorldSpaceChanged() {}

    void RenderingManager::update(float dt, bool paused)
    {
        if (!paused)
            mSky->update(dt);
    }

    Animation* RenderingManager::getAnimation(const MWWorld::Ptr& ptr)
    {
        if (ptr == mCamera->getTrackingPtr() && mPlayerAnimation)
            return mPlayerAnimation.get();
        return mObjects->getAnimation(ptr);
    }

    const Animation* RenderingManager::getAnimation(const MWWorld::ConstPtr& ptr) const
    {
        if (ptr == mCamera->getTrackingPtr() && mPlayerAnimation)
            return mPlayerAnimation.get();
        return mObjects->getAnimation(ptr);
    }

    PostProcessor* RenderingManager::getPostProcessor()
    {
        return mPostProcessor.get();
    }

    void RenderingManager::addWaterRippleEmitter(const MWWorld::Ptr& /*ptr*/) {}
    void RenderingManager::removeWaterRippleEmitter(const MWWorld::Ptr& /*ptr*/) {}
    void RenderingManager::emitWaterRipple(const osg::Vec3f& /*pos*/) {}

    void RenderingManager::updatePlayerPtr(const MWWorld::Ptr& ptr)
    {
        if (mPlayerAnimation)
            mPlayerAnimation->updatePtr(ptr);
        mCamera->attachTo(ptr);
    }

    void RenderingManager::removePlayer(const MWWorld::Ptr& player)
    {
        player.getRefData().setBaseNode(nullptr);
        mPlayerNode = nullptr;
        mPlayerAnimation = nullptr;
    }

    void RenderingManager::setupPlayer(const MWWorld::Ptr& player)
    {
        if (!mPlayerNode)
            mPlayerNode = new SceneUtil::PositionAttitudeTransform;
        mPlayerNode->setPosition(player.getRefData().getPosition().asVec3());
        player.getRefData().setBaseNode(mPlayerNode);
        mCamera->attachTo(player);
    }

    void RenderingManager::renderPlayer(const MWWorld::Ptr& player)
    {
        if (!mPlayerAnimation)
        {
            mPlayerAnimation = new NpcAnimation(player, mPlayerNode, mResourceSystem, true, NpcAnimation::VM_Normal);
            mCamera->setAnimation(mPlayerAnimation.get());
        }
    }

    void RenderingManager::rebuildPtr(const MWWorld::Ptr& /*ptr*/) {}
    void RenderingManager::processChangedSettings(const Settings::CategorySettingVector& /*settings*/) {}

    void RenderingManager::setViewDistance(float distance, bool /*delay*/)
    {
        mViewDistance = distance;
    }

    float RenderingManager::getTerrainHeightAt(const osg::Vec3f& pos, ESM::RefId worldspace)
    {
        // Compute directly from ESM::Land so exterior spawn/ground snapping works without Terrain::World.
        ESM::ExteriorCellLocation cellPos = ESM::positionToExteriorCellLocation(pos.x(), pos.y(), worldspace);
        const MWBase::World* world = MWBase::Environment::get().getWorld();
        if (!world)
            return 0.f;
        const ESM::Land* land = world->getStore().get<ESM::Land>().search(cellPos.mX, cellPos.mY);
        if (!land)
            return 0.f;
        const ESM::Land::LandData* data = land->getLandData(ESM::Land::DATA_VHGT);
        if (!data)
            return 0.f;
        constexpr int landSize = ESM::Land::LAND_SIZE;
        constexpr float cellSize = ESM::Land::REAL_SIZE;
        float relX = (pos.x() - cellPos.mX * cellSize) / cellSize * (landSize - 1);
        float relY = (pos.y() - cellPos.mY * cellSize) / cellSize * (landSize - 1);
        int x = std::clamp(static_cast<int>(relX), 0, landSize - 1);
        int y = std::clamp(static_cast<int>(relY), 0, landSize - 1);
        return data->mHeights[y * landSize + x];
    }

    void RenderingManager::overrideFieldOfView(float val)
    {
        mFieldOfViewOverridden = true;
        mFieldOfViewOverride = val;
    }

    void RenderingManager::setFieldOfView(float val)
    {
        mFieldOfView = val;
    }

    float RenderingManager::getFieldOfView() const
    {
        return mFieldOfViewOverridden ? mFieldOfViewOverride : mFieldOfView;
    }

    void RenderingManager::resetFieldOfView()
    {
        mFieldOfViewOverridden = false;
    }

    osg::Vec3f RenderingManager::getHalfExtents(const MWWorld::ConstPtr& /*object*/) const
    {
        return osg::Vec3f(32.f, 32.f, 64.f);
    }

    osg::BoundingBox RenderingManager::getCullSafeBoundingBox(const MWWorld::Ptr& /*ptr*/) const
    {
        return osg::BoundingBox(-32.f, -32.f, 0.f, 32.f, 32.f, 128.f);
    }

    void RenderingManager::exportSceneGraph(
        const MWWorld::Ptr& /*ptr*/, const std::filesystem::path& /*filename*/, const std::string& /*format*/)
    {
    }

    LandManager* RenderingManager::getLandManager() const
    {
        static LandManager headlessLandManager(ESM::Land::DATA_VHGT | ESM::Land::DATA_VNML);
        return &headlessLandManager;
    }

    bool RenderingManager::toggleBorders()
    {
        return false;
    }

    void RenderingManager::updateActorPath(const MWWorld::ConstPtr& /*actor*/, const std::deque<osg::Vec3f>& /*path*/,
        const DetourNavigator::AgentBounds& /*agentBounds*/, const osg::Vec3f& /*start*/, const osg::Vec3f& /*end*/) const
    {
    }

    void RenderingManager::removeActorPath(const MWWorld::ConstPtr& /*actor*/) const {}
    void RenderingManager::setNavMeshNumber(const std::size_t value)
    {
        mNavMeshNumber = value;
    }
    void RenderingManager::setActiveGrid(const osg::Vec4i& /*grid*/) {}
    bool RenderingManager::pagingEnableObject(int /*type*/, const MWWorld::ConstPtr& /*ptr*/, bool /*enabled*/)
    {
        return false;
    }
    void RenderingManager::pagingBlacklistObject(int /*type*/, const MWWorld::ConstPtr& /*ptr*/) {}
    bool RenderingManager::pagingUnlockCache()
    {
        return false;
    }
    void RenderingManager::getPagedRefnums(const osg::Vec4i& /*activeGrid*/, std::vector<ESM::RefNum>& /*out*/) {}
    void RenderingManager::updateProjectionMatrix() {}
    void RenderingManager::setScreenRes(int /*width*/, int /*height*/) {}
    void RenderingManager::setNavMeshMode(Settings::NavMeshRenderMode /*value*/) {}
}
