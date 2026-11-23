#include <apps/openmw/mwrender/animation.hpp>
#include <apps/openmw/mwrender/npcanimation.hpp>
#include <apps/openmw/mwrender/rotatecontroller.hpp>

#include <apps/openmw/mwbase/environment.hpp>
#include <apps/openmw/mwbase/world.hpp>
#include <apps/openmw/mwmechanics/combat.hpp>
#include <apps/openmw/mwmechanics/weapontype.hpp>
#include <apps/openmw/mwworld/class.hpp>
#include <apps/openmw/mwworld/esmstore.hpp>
#include <apps/openmw/mwworld/inventorystore.hpp>
#include <components/esm3/loadgmst.hpp>
#include <components/esm3/loadweap.hpp>
#include <components/esm3/loadrace.hpp>
#include <components/misc/resourcehelpers.hpp>
#include <components/misc/strings/algorithm.hpp>
#include <components/resource/keyframemanager.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/sceneutil/keyframe.hpp>
#include <components/sceneutil/lightcommon.hpp>
#include <components/sceneutil/lightmanager.hpp>
#include <components/sceneutil/skeleton.hpp>
#include <components/vfs/manager.hpp>

#include <osg/MatrixTransform>

#include <algorithm>
#include <limits>

namespace MWRender
{
    // Complete dummy definitions for forward-declared types owned by Animation/RotateController.
    class ResetAccumRootCallback : public osg::NodeCallback
    {
    };

    class TransparencyUpdater : public osg::Referenced
    {
    };

    class HeadAnimationTime : public SceneUtil::ControllerSource
    {
    public:
        float getValue(osg::NodeVisitor* /*nv*/) override { return 0.f; }
    };

    struct Animation::AnimSource
    {
        osg::ref_ptr<const SceneUtil::KeyframeHolder> mKeyframes;
    };

    // ---- Helpers ----

    float EffectAnimationTime::getValue(osg::NodeVisitor* /*nv*/)
    {
        return mTime;
    }

    void EffectAnimationTime::addTime(float duration)
    {
        mTime += duration;
    }

    void EffectAnimationTime::resetTime(float time)
    {
        mTime = time;
    }

    float EffectAnimationTime::getTime() const
    {
        return mTime;
    }

    PartHolder::PartHolder(osg::ref_ptr<osg::Node> node)
        : mNode(std::move(node))
    {
    }

    PartHolder::~PartHolder() = default;

    float Animation::AnimationTime::getValue(osg::NodeVisitor* /*nv*/)
    {
        return mTimePtr ? *mTimePtr : 0.f;
    }

    float Animation::AnimState::getCompletion() const
    {
        if (mStopTime <= mStartTime)
            return 1.f;
        return std::clamp((getTime() - mStartTime) / (mStopTime - mStartTime), 0.f, 1.f);
    }

    // ---- Animation ----

    Animation::Animation(
        const MWWorld::Ptr& ptr, osg::ref_ptr<osg::Group> parentNode, Resource::ResourceSystem* resourceSystem)
        : mInsert(std::move(parentNode))
        , mSkeleton(nullptr)
        , mNodeMapCreated(false)
        , mPtr(ptr)
        , mResourceSystem(resourceSystem)
        , mAccumulate(1.f, 1.f, 0.f)
        , mTextKeyListener(nullptr)
        , mHeadYawRadians(0.f)
        , mHeadPitchRadians(0.f)
        , mUpperBodyYawRadians(0.f)
        , mLegsYawRadians(0.f)
        , mBodyPitchRadians(0.f)
        , mHasMagicEffects(false)
        , mAlpha(1.f)
        , mPlayScriptedOnly(false)
        , mRequiresBoneMap(false)
    {
        for (size_t i = 0; i < sNumBlendMasks; ++i)
            mAnimationTimePtr[i] = std::make_shared<AnimationTime>();
    }

    Animation::~Animation() = default;

    void Animation::setTextKeyListener(TextKeyListener* listener)
    {
        mTextKeyListener = listener;
    }

    void Animation::setActive(int /*active*/) {}

    osg::Group* Animation::getOrCreateObjectRoot()
    {
        if (!mObjectRoot)
            mObjectRoot = new osg::Group;
        return mObjectRoot.get();
    }

    osg::Group* Animation::getObjectRoot()
    {
        return mObjectRoot.get();
    }

    void Animation::addEffect(std::string_view /*model*/, std::string_view /*effectId*/, bool /*loop*/,
        std::string_view /*bonename*/, std::string_view /*texture*/, bool /*useAmbientLight*/)
    {
        mHasMagicEffects = true;
    }

    void Animation::removeEffect(std::string_view /*effectId*/) {}
    void Animation::removeEffects()
    {
        mHasMagicEffects = false;
    }
    std::vector<std::string_view> Animation::getLoopingEffects() const
    {
        return {};
    }
    void Animation::addSpellCastGlow(const osg::Vec4f& /*color*/, float /*glowDuration*/) {}

    void Animation::updatePtr(const MWWorld::Ptr& ptr)
    {
        mPtr = ptr;
    }

    bool Animation::hasAnimation(std::string_view anim) const
    {
        for (const auto& source : mAnimSources)
        {
            if (source && source->mKeyframes && source->mKeyframes->mTextKeys.hasGroupStart(anim))
                return true;
        }
        return false;
    }

    bool Animation::isLoopingAnimation(std::string_view /*group*/) const
    {
        return false;
    }

    void Animation::setAccumulation(const osg::Vec3f& accum)
    {
        mAccumulate = accum;
    }

    void Animation::setObjectRoot(
        const std::string& model, bool /*forceskeleton*/, bool /*baseonly*/, bool /*isCreature*/)
    {
        clearAnimSources();
        if (!model.empty())
            addAnimSource(model, model);
    }

    void Animation::loadAdditionalAnimations(VFS::Path::NormalizedView /*model*/, const std::string& /*baseModel*/) {}

    void Animation::addAnimSource(std::string_view model, const std::string& baseModel)
    {
        if (!mResourceSystem || model.empty())
            return;
        std::string kfName(model);
        if (auto dot = kfName.rfind('.'); dot != std::string::npos)
            kfName.replace(dot, kfName.size() - dot, ".kf");
        addSingleAnimSource(kfName, baseModel);
    }

    std::shared_ptr<Animation::AnimSource> Animation::addSingleAnimSource(
        const std::string& model, const std::string& /*baseModel*/)
    {
        if (!mResourceSystem || model.empty())
            return nullptr;
        try
        {
            VFS::Path::Normalized path(Misc::ResourceHelpers::correctMeshPath(VFS::Path::Normalized(model)));
            if (!mResourceSystem->getVFS()->exists(path))
                return nullptr;
            auto keyframes = mResourceSystem->getKeyframeManager()->get(path);
            if (!keyframes)
                return nullptr;
            auto src = std::make_shared<AnimSource>();
            src->mKeyframes = std::move(keyframes);
            mAnimSources.push_back(src);
            return src;
        }
        catch (...)
        {
            return nullptr;
        }
    }

    void Animation::clearAnimSources()
    {
        mStates.clear();
        mAnimSources.clear();
    }

    void Animation::addControllers() {}
    void Animation::removeFromScene() {}

    bool Animation::reset(AnimState& state, const SceneUtil::TextKeyMap& keys, std::string_view groupname,
        std::string_view start, std::string_view stop, float startpoint, bool loopfallback)
    {
        std::string startKey = std::string(groupname) + ": " + std::string(start);
        std::string stopKey = std::string(groupname) + ": " + std::string(stop);

        auto startIt = keys.end();
        auto stopIt = keys.end();
        for (auto it = keys.begin(); it != keys.end(); ++it)
        {
            if (it->second == startKey && startIt == keys.end())
                startIt = it;
            if (it->second == stopKey)
                stopIt = it;
        }
        if (startIt == keys.end() || stopIt == keys.end() || startIt->first == stopIt->first)
            return false;

        state.mStartTime = startIt->first;
        state.mStopTime = stopIt->first;
        state.mLoopStartTime = state.mStartTime;
        state.mLoopStopTime = state.mStopTime;
        if (loopfallback)
        {
            std::string loopStart = std::string(groupname) + ": loop start";
            std::string loopStop = std::string(groupname) + ": loop stop";
            for (auto it = keys.begin(); it != keys.end(); ++it)
            {
                if (it->second == loopStart)
                    state.mLoopStartTime = it->first;
                else if (it->second == loopStop)
                    state.mLoopStopTime = it->first;
            }
        }
        state.setTime(state.mStartTime + (state.mStopTime - state.mStartTime) * std::clamp(startpoint, 0.f, 1.f));
        state.mGroupname = groupname;
        state.mStartKey = start;
        state.mStopKey = stop;
        return true;
    }

    void Animation::play(std::string_view groupname, const AnimPriority& priority, int blendMask, bool autodisable,
        float speedmult, std::string_view start, std::string_view stop, float startpoint, uint32_t loops,
        bool loopfallback)
    {
        if (groupname.empty())
            return;

        AnimState& state = mStates[std::string(groupname)];
        state.mPriority = priority;
        state.mBlendMask = blendMask;
        state.mAutoDisable = autodisable;
        state.mSpeedMult = speedmult;
        state.mLoopCount = loops;
        state.mLoopingEnabled = loops > 0;
        state.mPlaying = true;
        state.mGroupname = groupname;
        state.mStartKey = start;
        state.mStopKey = stop;

        bool foundKeys = false;
        for (auto it = mAnimSources.rbegin(); it != mAnimSources.rend(); ++it)
        {
            if (*it && (*it)->mKeyframes
                && reset(state, (*it)->mKeyframes->mTextKeys, groupname, start, stop, startpoint, loopfallback))
            {
                state.mSource = *it;
                foundKeys = true;
                break;
            }
        }

        if (!foundKeys)
        {
            // Provide sane nominal timing if a headless model has no loaded .kf keyframe file.
            state.mStartTime = 0.f;
            state.mLoopStartTime = 0.f;
            state.mLoopStopTime = 0.4f;
            state.mStopTime = 0.4f;
            state.setTime(0.4f * std::clamp(startpoint, 0.f, 1.f));
        }
    }

    void Animation::adjustSpeedMult(const std::string& groupname, float speedmult)
    {
        auto it = mStates.find(groupname);
        if (it != mStates.end())
            it->second.mSpeedMult = speedmult;
    }

    bool Animation::isPlaying(std::string_view groupname) const
    {
        auto it = mStates.find(groupname);
        return it != mStates.end() && it->second.mPlaying;
    }

    bool Animation::upperBodyReady() const
    {
        for (const auto& [name, state] : mStates)
        {
            if (state.mPlaying && (state.mBlendMask & BlendMask_UpperBody) && state.mPriority[BoneGroup_Torso] > 0)
                return false;
        }
        return true;
    }

    bool Animation::getInfo(
        std::string_view groupname, float* complete, float* speedmult, uint32_t* loopcount) const
    {
        auto it = mStates.find(groupname);
        if (it == mStates.end())
            return false;
        if (complete)
            *complete = it->second.getCompletion();
        if (speedmult)
            *speedmult = it->second.mSpeedMult;
        if (loopcount)
            *loopcount = it->second.mLoopCount;
        return true;
    }

    std::string_view Animation::getActiveGroup(BoneGroup /*boneGroup*/) const
    {
        for (const auto& [name, state] : mStates)
        {
            if (state.mPlaying)
                return name;
        }
        return {};
    }

    float Animation::getStartTime(const std::string& groupname) const
    {
        auto it = mStates.find(groupname);
        return it == mStates.end() ? 0.f : it->second.mStartTime;
    }

    float Animation::getTextKeyTime(std::string_view textKey) const
    {
        for (auto it = mAnimSources.rbegin(); it != mAnimSources.rend(); ++it)
        {
            if (!*it || !(*it)->mKeyframes)
                continue;
            for (const auto& [time, key] : (*it)->mKeyframes->mTextKeys)
            {
                if (key == textKey)
                    return time;
            }
        }
        return -1.f;
    }

    float Animation::getCurrentTime(std::string_view groupname) const
    {
        auto it = mStates.find(groupname);
        return it == mStates.end() ? 0.f : it->second.getTime();
    }

    void Animation::disable(std::string_view groupname)
    {
        auto it = mStates.find(groupname);
        if (it != mStates.end())
            mStates.erase(it);
    }

    float Animation::getVelocity(std::string_view /*groupname*/) const
    {
        return 0.f;
    }

    void Animation::handleTextKey(AnimState& /*state*/, std::string_view groupname,
        SceneUtil::TextKeyMap::ConstIterator key, const SceneUtil::TextKeyMap& map)
    {
        if (mTextKeyListener)
            mTextKeyListener->handleTextKey(groupname, key, map);
    }

    osg::Vec3f Animation::runAnimation(float duration)
    {
        std::vector<std::string> groupNames;
        groupNames.reserve(mStates.size());
        for (const auto& [name, state] : mStates)
        {
            if (state.mPlaying)
                groupNames.push_back(name);
        }

        for (const std::string& groupName : groupNames)
        {
            auto it = mStates.find(groupName);
            if (it == mStates.end() || !it->second.mPlaying)
                continue;

            const float oldTime = it->second.getTime();
            float newTime = oldTime + duration * it->second.mSpeedMult;
            if (!it->second.shouldLoop() && newTime > it->second.mStopTime)
                newTime = it->second.mStopTime;
            it->second.setTime(newTime);

            auto src = it->second.mSource;
            if (src && src->mKeyframes && mTextKeyListener)
            {
                const auto& keys = src->mKeyframes->mTextKeys;
                for (auto kit = keys.begin(); kit != keys.end(); ++kit)
                {
                    if (kit->first > oldTime && kit->first <= newTime && kit->second.starts_with(groupName))
                    {
                        mTextKeyListener->handleTextKey(groupName, kit, keys);
                    }
                }
            }

            it = mStates.find(groupName);
            if (it == mStates.end() || !it->second.mPlaying)
                continue;

            AnimState& state = it->second;
            if (state.shouldLoop())
            {
                --state.mLoopCount;
                state.setTime(state.mLoopStartTime);
            }
            else if (state.getTime() >= state.mStopTime)
            {
                state.mPlaying = false;
                if (state.mAutoDisable)
                    mStates.erase(it);
            }
        }
        return osg::Vec3f(0.f, 0.f, 0.f);
    }

    void Animation::setLoopingEnabled(std::string_view groupname, bool enabled)
    {
        auto it = mStates.find(groupname);
        if (it != mStates.end())
            it->second.mLoopingEnabled = enabled;
    }

    void Animation::updateEffects() {}

    const osg::Node* Animation::getNode(std::string_view /*name*/) const
    {
        return nullptr;
    }

    MWWorld::MovementDirectionFlags Animation::getSupportedMovementDirections(
        std::span<const std::string_view> prefixes) const
    {
        MWWorld::MovementDirectionFlags result = 0;
        for (const std::string_view prefix : prefixes)
        {
            if (prefix.empty())
            {
                for (const auto& source : mAnimSources)
                {
                    if (!source || !source->mKeyframes)
                        continue;
                    for (const auto& [time, key] : source->mKeyframes->mTextKeys)
                    {
                        (void)time;
                        const std::size_t colon = key.find(':');
                        const std::string_view group = colon == std::string_view::npos ? std::string_view(key)
                                                                                       : std::string_view(key).substr(0, colon);
                        if (group.ends_with("forward"))
                            result |= MWWorld::MovementDirectionFlag_Forward;
                        else if (group.ends_with("back"))
                            result |= MWWorld::MovementDirectionFlag_Back;
                        else if (group.ends_with("left"))
                            result |= MWWorld::MovementDirectionFlag_Left;
                        else if (group.ends_with("right"))
                            result |= MWWorld::MovementDirectionFlag_Right;
                    }
                }
                continue;
            }
            if (hasAnimation(std::string(prefix) + "forward"))
                result |= MWWorld::MovementDirectionFlag_Forward;
            if (hasAnimation(std::string(prefix) + "back"))
                result |= MWWorld::MovementDirectionFlag_Back;
            if (hasAnimation(std::string(prefix) + "left"))
                result |= MWWorld::MovementDirectionFlag_Left;
            if (hasAnimation(std::string(prefix) + "right"))
                result |= MWWorld::MovementDirectionFlag_Right;
        }
        if (result == 0)
        {
            result = MWWorld::MovementDirectionFlag_Forward | MWWorld::MovementDirectionFlag_Back
                | MWWorld::MovementDirectionFlag_Left | MWWorld::MovementDirectionFlag_Right;
        }
        return result;
    }

    void Animation::setAlpha(float alpha)
    {
        mAlpha = alpha;
    }
    void Animation::setLightEffect(float /*effect*/) {}
    void Animation::setHeadPitch(float pitchRadians)
    {
        mHeadPitchRadians = pitchRadians;
    }
    void Animation::setHeadYaw(float yawRadians)
    {
        mHeadYawRadians = yawRadians;
    }
    float Animation::getHeadPitch() const
    {
        return mHeadPitchRadians;
    }
    float Animation::getHeadYaw() const
    {
        return mHeadYawRadians;
    }

    ObjectAnimation::ObjectAnimation(const MWWorld::Ptr& ptr, const std::string& model,
        Resource::ResourceSystem* resourceSystem, bool /*animated*/, bool /*allowLight*/)
        : Animation(ptr, nullptr, resourceSystem)
    {
        setObjectRoot(model, false, false, false);
    }

    bool ObjectAnimation::canBeHarvested() const
    {
        return false;
    }
    void ObjectAnimation::harvest(const MWWorld::Ptr& /*ptr*/) {}

    // ---- ActorAnimation / WeaponAnimation ----

    ActorAnimation::ActorAnimation(
        const MWWorld::Ptr& ptr, osg::ref_ptr<osg::Group> parentNode, Resource::ResourceSystem* resourceSystem)
        : Animation(ptr, std::move(parentNode), resourceSystem)
    {
    }

    ActorAnimation::~ActorAnimation() = default;

    void ActorAnimation::itemAdded(const MWWorld::ConstPtr& /*item*/, int /*count*/) {}
    void ActorAnimation::itemRemoved(const MWWorld::ConstPtr& /*item*/, int /*count*/) {}
    bool ActorAnimation::useShieldAnimations() const
    {
        return false;
    }
    bool ActorAnimation::updateCarriedLeftVisible(const int /*weaptype*/) const
    {
        return false;
    }
    void ActorAnimation::removeFromScene() {}
    std::string ActorAnimation::getSheathedShieldMesh(const MWWorld::ConstPtr& /*shield*/) const
    {
        return {};
    }

    WeaponAnimation::WeaponAnimation() = default;
    WeaponAnimation::~WeaponAnimation() = default;

    float WeaponAnimationTime::getValue(osg::NodeVisitor* /*nv*/)
    {
        return 0.f;
    }

    // ---- NpcAnimation ----

    NpcAnimation::NpcAnimation(const MWWorld::Ptr& ptr, osg::ref_ptr<osg::Group> parentNode,
        Resource::ResourceSystem* resourceSystem, bool disableSounds, ViewMode viewMode, float firstPersonFieldOfView)
        : ActorAnimation(ptr, std::move(parentNode), resourceSystem)
        , mNpc(nullptr)
        , mViewMode(viewMode)
        , mShowWeapons(false)
        , mShowCarriedLeft(false)
        , mNpcType(Type_Normal)
        , mFirstPersonFieldOfView(firstPersonFieldOfView)
        , mSoundsDisabled(disableSounds)
        , mAccurateAiming(false)
        , mAimingFactor(0.f)
    {
        mSounds.fill(nullptr);
        addAnimSource("meshes\\base_anim.nif", "meshes\\base_anim.nif");
    }

    NpcAnimation::~NpcAnimation() = default;

    void NpcAnimation::equipmentChanged() {}
    void NpcAnimation::addControllers() {}
    bool NpcAnimation::isArrowAttached() const
    {
        return false;
    }
    std::string NpcAnimation::getSheathedShieldMesh(const MWWorld::ConstPtr& /*shield*/) const
    {
        return {};
    }
    void NpcAnimation::enableHeadAnimation(bool /*enable*/) {}
    void NpcAnimation::setAccurateAiming(bool enabled)
    {
        mAccurateAiming = enabled;
    }
    void NpcAnimation::setWeaponGroup(const std::string& /*group*/, bool /*relativeDuration*/) {}
    osg::Vec3f NpcAnimation::runAnimation(float timepassed)
    {
        return Animation::runAnimation(timepassed);
    }
    void NpcAnimation::showWeapons(bool showWeapon)
    {
        mShowWeapons = showWeapon;
    }
    bool NpcAnimation::updateCarriedLeftVisible(const int /*weaptype*/) const
    {
        return mShowCarriedLeft;
    }
    void NpcAnimation::showCarriedLeft(bool show)
    {
        mShowCarriedLeft = show;
    }
    void NpcAnimation::attachArrow() {}
    void NpcAnimation::detachArrow() {}
    void NpcAnimation::releaseArrow(float attackStrength)
    {
        if (mPtr.isEmpty() || !mPtr.getClass().hasInventoryStore(mPtr))
            return;

        MWWorld::InventoryStore& inv = mPtr.getClass().getInventoryStore(mPtr);
        MWWorld::ContainerStoreIterator weapon = inv.getSlot(MWWorld::InventoryStore::Slot_CarriedRight);
        if (weapon == inv.end() || weapon->getType() != ESM::Weapon::sRecordId)
            return;

        const osg::Quat orient = osg::Quat(mPtr.getRefData().getPosition().rot[0], osg::Vec3f(-1, 0, 0))
            * osg::Quat(mPtr.getRefData().getPosition().rot[2], osg::Vec3f(0, 0, -1));
        const osg::Vec3f launchPos = mPtr.getRefData().getPosition().asVec3() + osg::Vec3f(0.f, 0.f, 96.f)
            + orient * osg::Vec3f(0.f, 24.f, 0.f);

        const MWWorld::Store<ESM::GameSetting>& gmst
            = MWBase::Environment::get().getESMStore()->get<ESM::GameSetting>();
        MWMechanics::applyFatigueLoss(mPtr, *weapon, attackStrength);

        if (MWMechanics::getWeaponType(weapon->get<ESM::Weapon>()->mBase->mData.mType)->mWeaponClass
            == ESM::WeaponType::Thrown)
        {
            const float fMin = gmst.find("fThrownWeaponMinSpeed")->mValue.getFloat();
            const float fMax = gmst.find("fThrownWeaponMaxSpeed")->mValue.getFloat();
            const float speed = fMin + (fMax - fMin) * attackStrength;
            MWWorld::Ptr weaponPtr = *weapon;
            MWBase::Environment::get().getWorld()->launchProjectile(
                mPtr, weaponPtr, launchPos, orient, weaponPtr, speed, attackStrength);
            inv.remove(*weapon, 1);
        }
        else
        {
            MWWorld::ContainerStoreIterator ammo = inv.getSlot(MWWorld::InventoryStore::Slot_Ammunition);
            if (ammo == inv.end())
                return;
            const float fMin = gmst.find("fProjectileMinSpeed")->mValue.getFloat();
            const float fMax = gmst.find("fProjectileMaxSpeed")->mValue.getFloat();
            const float speed = fMin + (fMax - fMin) * attackStrength;
            MWWorld::Ptr weaponPtr = *weapon;
            MWWorld::Ptr ammoPtr = *ammo;
            MWBase::Environment::get().getWorld()->launchProjectile(
                mPtr, ammoPtr, launchPos, orient, weaponPtr, speed, attackStrength);
            inv.remove(ammoPtr, 1);
        }
    }
    osg::Group* NpcAnimation::getArrowBone()
    {
        return nullptr;
    }
    osg::Node* NpcAnimation::getWeaponNode()
    {
        return nullptr;
    }
    Resource::ResourceSystem* NpcAnimation::getResourceSystem()
    {
        return mResourceSystem;
    }
    void NpcAnimation::setViewMode(ViewMode viewMode)
    {
        mViewMode = viewMode;
    }
    void NpcAnimation::updateParts() {}
    void NpcAnimation::rebuild() {}
    int NpcAnimation::getSlot(const osg::NodePath& /*path*/) const
    {
        return -1;
    }
    void NpcAnimation::setVampire(bool /*vampire*/) {}
    void NpcAnimation::setFirstPersonOffset(const osg::Vec3f& offset)
    {
        mFirstPersonOffset = offset;
    }
    void NpcAnimation::updatePtr(const MWWorld::Ptr& updated)
    {
        Animation::updatePtr(updated);
    }

    const std::vector<const ESM::BodyPart*>& NpcAnimation::getBodyParts(
        const ESM::RefId& /*raceId*/, bool /*female*/, bool /*firstperson*/, bool /*werewolf*/)
    {
        static const std::vector<const ESM::BodyPart*> empty;
        return empty;
    }
}
