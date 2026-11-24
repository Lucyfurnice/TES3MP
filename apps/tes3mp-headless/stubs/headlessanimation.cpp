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
#include <components/esm3/loadnpc.hpp>
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
#include <components/settings/values.hpp>
#include <components/vfs/manager.hpp>

#include <apps/openmw/mwmechanics/character.hpp>

#include "../rangeddebug.hpp"

#include <osg/MatrixTransform>
#include <osg/Vec3f>

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>

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

    namespace
    {
        // Mirrors OpenMW's text-key matching helper: verifies that a key starts
        // with "<s1><s2><s3>" (s2 is normally ": ").
        bool equalsParts(std::string_view value, std::string_view s1, std::string_view s2, std::string_view s3 = {})
        {
            if (value.starts_with(s1))
            {
                value = value.substr(s1.size());
                if (value.starts_with(s2))
                    return value.substr(s2.size()) == s3;
            }
            return false;
        }

        // Replicates MWRender::calcAnimVelocity() from apps/openmw/mwrender/animation.cpp.
        float calcAnimVelocity(const SceneUtil::TextKeyMap& keys, const SceneUtil::KeyframeController* accumctrl,
            const osg::Vec3f& accum, std::string_view groupname)
        {
            float starttime = std::numeric_limits<float>::max();
            float stoptime = 0.0f;

            // Pick the last Loop Start key and the last Loop Stop key. This is required because of broken text
            // keys in AshVampire.nif which has *two* WalkForward: Loop Stop keys.
            auto keyiter = keys.rbegin();
            while (keyiter != keys.rend())
            {
                if (equalsParts(keyiter->second, groupname, ": start")
                    || equalsParts(keyiter->second, groupname, ": loop start"))
                {
                    starttime = keyiter->first;
                    break;
                }
                ++keyiter;
            }
            keyiter = keys.rbegin();
            while (keyiter != keys.rend())
            {
                if (equalsParts(keyiter->second, groupname, ": stop"))
                    stoptime = keyiter->first;
                else if (equalsParts(keyiter->second, groupname, ": loop stop"))
                {
                    stoptime = keyiter->first;
                    break;
                }
                ++keyiter;
            }

            if (accumctrl && stoptime > starttime)
            {
                osg::Vec3f startpos = osg::componentMultiply(accumctrl->getTranslation(starttime), accum);
                osg::Vec3f endpos = osg::componentMultiply(accumctrl->getTranslation(stoptime), accum);

                return (startpos - endpos).length() / (stoptime - starttime);
            }

            return 0.0f;
        }

        // Looks up the transform controller of the movement accumulation bone directly from the loaded keyframes.
        // There is no scene graph in headless mode, so this mirrors MWRender::Animation::addSingleAnimSource(),
        // which prefers "bip01" and falls back to "root bone".
        const SceneUtil::KeyframeController* findAccumController(const SceneUtil::KeyframeHolder* keyframes)
        {
            if (!keyframes)
                return nullptr;
            const SceneUtil::KeyframeHolder::KeyframeControllerMap& controllers = keyframes->mKeyframeControllers;
            for (const std::string_view name : { std::string_view("bip01"), std::string_view("root bone") })
            {
                for (const auto& [controllerName, controller] : controllers)
                {
                    if (Misc::StringUtils::ciEqual(controllerName, name))
                        return controller.get();
                }
            }
            return nullptr;
        }

        // Replicates MWRender::Animation::updatePosition(): accumulates the root-motion displacement of the
        // movement bone between two animation times.
        void accumulateRootMotion(const SceneUtil::KeyframeController* accumctrl, const osg::Vec3f& accum,
            float oldtime, float newtime, osg::Vec3f& movement)
        {
            const osg::Vec3f off = osg::componentMultiply(accumctrl->getTranslation(newtime), accum);
            movement += off - osg::componentMultiply(accumctrl->getTranslation(oldtime), accum);
        }
    }

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

    bool Animation::isLoopingAnimation(std::string_view group) const
    {
        // In Morrowind, some animation groups are always considered looping, regardless of loop start/stop keys.
        // To match vanilla behavior we probably only need to check this list, but we don't want to prevent modded
        // animations with custom group names from looping either.
        static const std::unordered_set<std::string_view> loopingAnimations = { "walkforward", "walkback", "walkleft",
            "walkright", "swimwalkforward", "swimwalkback", "swimwalkleft", "swimwalkright", "runforward", "runback",
            "runleft", "runright", "swimrunforward", "swimrunback", "swimrunleft", "swimrunright", "sneakforward",
            "sneakback", "sneakleft", "sneakright", "turnleft", "turnright", "swimturnleft", "swimturnright",
            "spellturnleft", "spellturnright", "torch", "idle", "idle2", "idle3", "idle4", "idle5", "idle6", "idle7",
            "idle8", "idle9", "idlesneak", "idlestorm", "idleswim", "jump", "inventoryhandtohand",
            "inventoryweapononehand", "inventoryweapontwohand", "inventoryweapontwowide" };
        static const std::vector<std::string_view> shortGroups = MWMechanics::getAllWeaponTypeShortGroups();

        if (getTextKeyTime(std::string(group) + ": loop start") >= 0)
            return true;

        // Most looping animations have variants for each weapon type shortgroup.
        // Just remove the shortgroup instead of enumerating all of the possible animation groupnames.
        // Make sure we pick the longest shortgroup so e.g. "bow" doesn't get picked over "crossbow".
        std::size_t suffixLength = 0;
        for (std::string_view suffix : shortGroups)
        {
            if (suffix.length() > suffixLength && group.ends_with(suffix))
                suffixLength = suffix.length();
        }
        group.remove_suffix(suffixLength);

        return loopingAnimations.count(group) > 0;
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
        mAnimVelocities.clear();
    }

    void Animation::addControllers() {}
    void Animation::removeFromScene() {}

    bool Animation::reset(AnimState& state, const SceneUtil::TextKeyMap& keys, std::string_view groupname,
        std::string_view start, std::string_view stop, float startpoint, bool loopfallback)
    {
        // Look for text keys in reverse. This matters because some models (e.g. undeadwolf_2.nif) have two focus
        // keys for a group, and the last one is supposed to be used.
        auto groupend = keys.rbegin();
        for (; groupend != keys.rend(); ++groupend)
        {
            if (groupend->second.starts_with(groupname) && groupend->second.compare(groupname.size(), 2, ": ") == 0)
                break;
        }

        auto startkey = groupend;
        while (startkey != keys.rend() && !equalsParts(startkey->second, groupname, ": ", start))
            ++startkey;
        if (startkey == keys.rend() && start == "loop start")
        {
            startkey = groupend;
            while (startkey != keys.rend() && !equalsParts(startkey->second, groupname, ": start"))
                ++startkey;
        }
        if (startkey == keys.rend())
            return false;

        auto stopkey = groupend;
        std::size_t checkLength = groupname.size() + 2 + stop.size();
        while (stopkey != keys.rend()
            // We have to ignore extra garbage at the end.  The Scrib's idle3 animation has "Idle3: Stop."
            // instead of "Idle3: Stop".
            && !equalsParts(std::string_view{ stopkey->second }.substr(0, checkLength), groupname, ": ", stop))
            ++stopkey;
        if (stopkey == keys.rend())
            return false;

        if (startkey->first > stopkey->first)
            return false;

        state.mStartTime = startkey->first;
        state.mStopTime = stopkey->first;
        if (loopfallback)
        {
            state.mLoopStartTime = startkey->first;
            state.mLoopStopTime = stopkey->first;
        }
        else
        {
            state.mLoopStartTime = startkey->first;
            state.mLoopStopTime = std::numeric_limits<float>::max();
        }

        state.setTime(state.mStartTime + ((state.mStopTime - state.mStartTime) * startpoint));

        // mLoopStartTime and mLoopStopTime normally get assigned when encountering these keys while playing the
        // animation (see handleTextKey). But if startpoint is already past these keys, or start time is == stop time,
        // we need to assign them now.
        auto key = groupend;
        for (; key != startkey && key != keys.rend(); ++key)
        {
            if (key->first > state.getTime())
                continue;

            if (equalsParts(key->second, groupname, ": loop start"))
                state.mLoopStartTime = key->first;
            else if (equalsParts(key->second, groupname, ": loop stop"))
                state.mLoopStopTime = key->first;
        }

        return true;
    }

    void Animation::play(std::string_view groupname, const AnimPriority& priority, int blendMask, bool autodisable,
        float speedmult, std::string_view start, std::string_view stop, float startpoint, uint32_t loops,
        bool loopfallback)
    {
        if (groupname.empty())
            return;

        // Any existing state for this group only updates its priority; the currently playing
        // animation is allowed to finish (matching MWRender::Animation::play).
        AnimStateMap::iterator foundstateiter = mStates.find(groupname);
        if (foundstateiter != mStates.end())
            foundstateiter->second.mPriority = priority;

        // Remove all other animations at this priority, they can not play together.
        auto stateiter = mStates.begin();
        while (stateiter != mStates.end())
        {
            if (stateiter->second.mPriority == priority && stateiter->first != groupname)
                stateiter = mStates.erase(stateiter);
            else
                ++stateiter;
        }

        if (foundstateiter != mStates.end())
            return;

        /* Look in reverse; last-inserted source has priority. */
        AnimState state;
        bool foundKeys = false;
        for (auto iter = mAnimSources.rbegin(); iter != mAnimSources.rend(); ++iter)
        {
            if (!*iter || !(*iter)->mKeyframes)
                continue;

            const SceneUtil::TextKeyMap& textkeys = (*iter)->mKeyframes->mTextKeys;
            if (reset(state, textkeys, groupname, start, stop, startpoint, loopfallback))
            {
                state.mSource = *iter;
                state.mSpeedMult = speedmult;
                state.mLoopCount = loops;
                state.mPlaying = (state.getTime() < state.mStopTime);
                state.mPriority = priority;
                state.mBlendMask = blendMask;
                state.mAutoDisable = autodisable;
                state.mGroupname = groupname;
                state.mStartKey = start;
                state.mStopKey = stop;
                mStates[std::string{ groupname }] = state;

                // If we start later than the start key, dispense the text keys we have already passed.
                if (state.mPlaying)
                {
                    auto textkey = textkeys.lowerBound(state.getTime());
                    while (textkey != textkeys.end() && textkey->first <= state.getTime())
                    {
                        handleTextKey(state, groupname, textkey, textkeys);
                        ++textkey;
                    }
                }

                if (state.getTime() >= state.mLoopStopTime && state.mLoopCount > 0)
                {
                    state.mLoopCount--;
                    state.setTime(state.mLoopStartTime);
                    state.mPlaying = true;
                    if (state.getTime() >= state.mLoopStopTime)
                        break;

                    auto textkey = textkeys.lowerBound(state.getTime());
                    while (textkey != textkeys.end() && textkey->first <= state.getTime())
                    {
                        handleTextKey(state, groupname, textkey, textkeys);
                        ++textkey;
                    }
                }

                foundKeys = true;
                break;
            }
        }

        if (!foundKeys)
        {
            // Headless fallback: provide sane nominal timing when a group exists but its start/stop keys are
            // unavailable (e.g. a model with no loaded .kf keyframe file). Without this the creature/NPC would
            // never leave its current animation state.
            AnimState& fallback = mStates[std::string{ groupname }];
            fallback.mSource = nullptr;
            fallback.mPriority = priority;
            fallback.mBlendMask = blendMask;
            fallback.mAutoDisable = autodisable;
            fallback.mSpeedMult = speedmult;
            fallback.mLoopCount = loops;
            fallback.mLoopingEnabled = loops > 0;
            fallback.mGroupname = groupname;
            fallback.mStartKey = start;
            fallback.mStopKey = stop;
            fallback.mStartTime = 0.f;
            fallback.mLoopStartTime = 0.f;
            fallback.mLoopStopTime = 0.4f;
            fallback.mStopTime = 0.4f;
            fallback.setTime(0.4f * std::clamp(startpoint, 0.f, 1.f));
            fallback.mPlaying = true;
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
        {
            if (complete)
                *complete = 0.0f;
            if (speedmult)
                *speedmult = 0.0f;
            if (loopcount)
                *loopcount = 0;
            return false;
        }
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
        for (auto it = mAnimSources.rbegin(); it != mAnimSources.rend(); ++it)
        {
            if (!*it || !(*it)->mKeyframes)
                continue;
            const auto found = (*it)->mKeyframes->mTextKeys.findGroupStart(groupname);
            if (found != (*it)->mKeyframes->mTextKeys.end())
                return found->first;
        }
        return -1.f;
    }

    float Animation::getTextKeyTime(std::string_view textKey) const
    {
        for (auto it = mAnimSources.rbegin(); it != mAnimSources.rend(); ++it)
        {
            if (!*it || !(*it)->mKeyframes)
                continue;
            for (const auto& [time, key] : (*it)->mKeyframes->mTextKeys)
            {
                if (key.starts_with(textKey))
                    return time;
            }
        }
        return -1.f;
    }

    float Animation::getCurrentTime(std::string_view groupname) const
    {
        auto it = mStates.find(groupname);
        if (it == mStates.end())
            return -1.f;
        return it->second.getTime();
    }

    void Animation::disable(std::string_view groupname)
    {
        auto it = mStates.find(groupname);
        if (it != mStates.end())
            mStates.erase(it);
    }

    float Animation::getVelocity(std::string_view groupname) const
    {
        auto found = mAnimVelocities.find(groupname);
        if (found != mAnimVelocities.end())
            return found->second;

        // Look in reverse; last-inserted source has priority.
        auto animsrc = mAnimSources.rbegin();
        for (; animsrc != mAnimSources.rend(); ++animsrc)
        {
            if (*animsrc && (*animsrc)->mKeyframes && (*animsrc)->mKeyframes->mTextKeys.hasGroupStart(groupname))
                break;
        }
        if (animsrc == mAnimSources.rend())
            return 0.0f;

        float velocity = 0.0f;
        if (const SceneUtil::KeyframeController* controller = findAccumController((*animsrc)->mKeyframes.get()))
            velocity = calcAnimVelocity((*animsrc)->mKeyframes->mTextKeys, controller, mAccumulate, groupname);

        // If there's no usable velocity in this source, keep looking in the lower-priority sources.
        if (!(velocity > 1.0f))
        {
            auto animiter = mAnimSources.rbegin();
            while (animiter != mAnimSources.rend() && *animiter != *animsrc)
                ++animiter;

            while (!(velocity > 1.0f) && ++animiter != mAnimSources.rend())
            {
                if (!*animiter || !(*animiter)->mKeyframes)
                    continue;
                if (const SceneUtil::KeyframeController* controller = findAccumController((*animiter)->mKeyframes.get()))
                    velocity = calcAnimVelocity((*animiter)->mKeyframes->mTextKeys, controller, mAccumulate, groupname);
            }
        }

        mAnimVelocities.emplace(std::string(groupname), velocity);

        return velocity;
    }

    void Animation::handleTextKey(AnimState& state, std::string_view groupname,
        SceneUtil::TextKeyMap::ConstIterator key, const SceneUtil::TextKeyMap& map)
    {
        std::string_view evt = key->second;

        if (evt.starts_with(groupname) && evt.substr(groupname.size()).starts_with(": "))
        {
            size_t off = groupname.size() + 2;
            if (evt.substr(off) == "loop start")
                state.mLoopStartTime = key->first;
            else if (evt.substr(off) == "loop stop")
                state.mLoopStopTime = key->first;
        }

        if (mTextKeyListener)
            mTextKeyListener->handleTextKey(groupname, key, map);
    }

    osg::Vec3f Animation::runAnimation(float duration)
    {
        static const SceneUtil::TextKeyMap sEmptyTextKeys;

        // Determine which animation state drives root motion: the highest-priority state affecting blend mask 0,
        // mirroring MWRender::Animation::resetActiveGroups(). Without a scene graph we look up its accumulation-root
        // transform controller directly from the loaded keyframes. The shared_ptr is captured by value so it stays
        // valid even if the state is erased during the loop below.
        const AnimState* movementState = nullptr;
        for (const auto& stateEntry : mStates)
        {
            const AnimState& state = stateEntry.second;
            if (!state.blendMaskContains(0))
                continue;
            if (movementState == nullptr
                || movementState->mPriority[static_cast<BoneGroup>(0)]
                    < state.mPriority[static_cast<BoneGroup>(0)])
                movementState = &state;
        }
        std::shared_ptr<float> movementTimePtr = movementState ? movementState->mTime : nullptr;
        const SceneUtil::KeyframeController* accumCtrl = (movementState && movementState->mSource)
            ? findAccumController(movementState->mSource->mKeyframes.get())
            : nullptr;

        osg::Vec3f movement(0.f, 0.f, 0.f);

        AnimStateMap::iterator stateiter = mStates.begin();
        while (stateiter != mStates.end())
        {
            AnimState& state = stateiter->second;
            if (mPlayScriptedOnly && !state.mPriority.contains(MWMechanics::Priority_Scripted))
            {
                ++stateiter;
                continue;
            }

            const SceneUtil::TextKeyMap& textkeys
                = state.mSource && state.mSource->mKeyframes ? state.mSource->mKeyframes->mTextKeys : sEmptyTextKeys;
            auto textkey = textkeys.upperBound(state.getTime());

            float timepassed = duration * state.mSpeedMult;
            while (state.mPlaying)
            {
                if (!state.shouldLoop())
                {
                    float targetTime = state.getTime() + timepassed;
                    if (textkey == textkeys.end() || textkey->first > targetTime)
                    {
                        if (accumCtrl && state.mTime == movementTimePtr)
                            accumulateRootMotion(accumCtrl, mAccumulate, state.getTime(), targetTime, movement);
                        state.setTime(std::min(targetTime, state.mStopTime));
                    }
                    else
                    {
                        if (accumCtrl && state.mTime == movementTimePtr)
                            accumulateRootMotion(accumCtrl, mAccumulate, state.getTime(), textkey->first, movement);
                        state.setTime(textkey->first);
                    }

                    state.mPlaying = (state.getTime() < state.mStopTime);
                    timepassed = targetTime - state.getTime();

                    while (textkey != textkeys.end() && textkey->first <= state.getTime())
                    {
                        handleTextKey(state, stateiter->first, textkey, textkeys);
                        ++textkey;
                    }
                }
                if (state.shouldLoop())
                {
                    state.mLoopCount--;
                    state.setTime(state.mLoopStartTime);
                    state.mPlaying = true;

                    textkey = textkeys.lowerBound(state.getTime());
                    while (textkey != textkeys.end() && textkey->first <= state.getTime())
                    {
                        handleTextKey(state, stateiter->first, textkey, textkeys);
                        ++textkey;
                    }

                    if (state.getTime() >= state.mLoopStopTime)
                        break;
                }

                if (timepassed <= 0.0f)
                    break;
            }

            if (!state.mPlaying && state.mAutoDisable)
                stateiter = mStates.erase(stateiter);
            else
                ++stateiter;
        }

        updateEffects();

        // When the legs are yawed (side movement / turning), the accumulated movement is rotated with them.
        // This mirrors MWRender::Animation::runAnimation().
        const float epsilon = 0.001f;
        if (std::abs(mLegsYawRadians) > epsilon)
            movement = osg::Quat(mLegsYawRadians, osg::Vec3f(0, 0, 1)) * movement;

        return movement;
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

        if (!ptr.isEmpty() && ptr.getType() == ESM::NPC::sRecordId)
            mNpc = ptr.get<ESM::NPC>()->mBase;

        // Select the same third-person skeleton as the real renderer (MWRender::NpcAnimation::updateNpcBase),
        // so the correct set of text keys and transform controllers is loaded for headless simulation.
        std::string defaultSkeleton = Settings::models().mBaseanim.get().value();
        bool isBeast = false;
        if (mNpc)
        {
            if (const ESM::Race* race
                = MWBase::Environment::get().getESMStore()->get<ESM::Race>().search(mNpc->mRace))
                isBeast = (race->mData.mFlags & ESM::Race::Beast) != 0;

            if (isBeast)
                defaultSkeleton = Settings::models().mBaseanimkna.get().value();
            else if (!mNpc->isMale())
                defaultSkeleton = Settings::models().mBaseanimfemale.get().value();
        }

        const std::string base = Settings::models().mXbaseanim.get().value();
        if (!base.empty())
            addAnimSource(base, base);
        if (defaultSkeleton != base)
            addAnimSource(defaultSkeleton, defaultSkeleton);

        // Argonian beast races use an additional swimming skeleton.
        if (mNpc && isBeast && mNpc->mRace.contains("argonian"))
            addAnimSource(Settings::models().mXargonianswimkna.get().value(), defaultSkeleton);
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
        {
            TES3MP_RANGED_LOG << "[RangedDebug] releaseArrow skipped: actor empty or no inventory (empty="
                              << (mPtr.isEmpty() ? "true" : "false") << ")";
            return;
        }

        MWWorld::InventoryStore& inv = mPtr.getClass().getInventoryStore(mPtr);
        MWWorld::ContainerStoreIterator weapon = inv.getSlot(MWWorld::InventoryStore::Slot_CarriedRight);
        if (weapon == inv.end() || weapon->getType() != ESM::Weapon::sRecordId)
        {
            TES3MP_RANGED_LOG << "[RangedDebug] releaseArrow skipped for " << mPtr.getCellRef().getRefId()
                              << ": no weapon in CarriedRight slot";
            return;
        }
        TES3MP_RANGED_LOG << "[RangedDebug] releaseArrow for " << mPtr.getCellRef().getRefId() << " weapon="
                          << weapon->getCellRef().getRefId() << " attackStrength=" << attackStrength;

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
            {
                TES3MP_RANGED_LOG << "[RangedDebug] releaseArrow for " << mPtr.getCellRef().getRefId()
                                  << ": no ammunition in Ammunition slot (weapon=" << weapon->getCellRef().getRefId()
                                  << ")";
                return;
            }
            const float fMin = gmst.find("fProjectileMinSpeed")->mValue.getFloat();
            const float fMax = gmst.find("fProjectileMaxSpeed")->mValue.getFloat();
            const float speed = fMin + (fMax - fMin) * attackStrength;
            MWWorld::Ptr weaponPtr = *weapon;
            MWWorld::Ptr ammoPtr = *ammo;
            TES3MP_RANGED_LOG << "[RangedDebug] launchProjectile caster=" << mPtr.getCellRef().getRefId()
                              << " ammo=" << ammoPtr.getCellRef().getRefId() << " speed=" << speed << " launchPos=("
                              << launchPos.x() << ", " << launchPos.y() << ", " << launchPos.z() << ")";
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
