#include <components/misc/resourcehelpers.hpp>
#include <components/openmw-mp/TimedLog.hpp>
#include <components/settings/values.hpp>
#include <components/vfs/pathutil.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/soundmanager.hpp"
#include "../mwbase/windowmanager.hpp"

#include "../mwdialogue/dialoguemanagerimp.hpp"

#include "../mwmechanics/aiactivate.hpp"
#include "../mwmechanics/aicombat.hpp"
#include "../mwmechanics/aiescort.hpp"
#include "../mwmechanics/aifollow.hpp"
#include "../mwmechanics/aitravel.hpp"
#include "../mwmechanics/aiwander.hpp"

#include "../mwmechanics/aisetting.hpp"
#include "../mwmechanics/creaturestats.hpp"
#include "../mwmechanics/drawstate.hpp"
#include "../mwmechanics/mechanicsmanagerimp.hpp"
#include "../mwmechanics/movement.hpp"

#include "../mwrender/animation.hpp"

#include "../mwworld/action.hpp"
#include "../mwworld/cellstore.hpp"
#include "../mwworld/class.hpp"
#include "../mwworld/inventorystore.hpp"
#include "../mwworld/worldimp.hpp"

#include "DedicatedActor.hpp"
#include "Main.hpp"
#include "CellController.hpp"
#include "MechanicsHelper.hpp"

using namespace mwmp;

DedicatedActor::DedicatedActor()
{
    drawState = static_cast<char>(MWMechanics::DrawState::Nothing);
    movementFlags = 0;
    animation.groupname = "";
    sound = "";

    hasPositionData = false;
    hasStatsDynamicData = false;
    hasReceivedInitialEquipment = false;
    hasChangedCell = true;
    isAuthoritativeMoving = false;
    authoritativeMoveTimer = 0.0f;

    attack.pressed = false;
    cast.pressed = false;
}

DedicatedActor::~DedicatedActor()
{

}

void DedicatedActor::update(float dt)
{
    if (Main::get().isAuthoritativeServer() && !ptr.isEmpty())
    {
        MWMechanics::AiSequence& seq = ptr.getClass().getCreatureStats(ptr).getAiSequence();
        if (!seq.isEmpty())
            seq.clear();
    }

    // Only move and set anim flags if the framerate isn't too low
    if (dt < 0.1)
    {
        move(dt);
        setAnimFlags();
    }

    setStatsDynamic();
}

void DedicatedActor::setCell(MWWorld::CellStore *cellStore)
{
    MWBase::World *world = MWBase::Environment::get().getWorld();

    ptr = world->moveObject(ptr, cellStore, position.asVec3());
    setMovementSettings();

    hasChangedCell = true;
}

void DedicatedActor::move(float dt)
{
    if (!hasPositionData)
        return;

    ESM::Position refPos = ptr.getRefData().getPosition();
    MWBase::World *world = MWBase::Environment::get().getWorld();
    const float maxInterpolationDistance = 256.0f;

    // Apply interpolation only if the position hasn't changed too much from last time
    bool shouldInterpolate = std::abs(position.pos[0] - refPos.pos[0]) < maxInterpolationDistance
        && std::abs(position.pos[1] - refPos.pos[1]) < maxInterpolationDistance
        && std::abs(position.pos[2] - refPos.pos[2]) < maxInterpolationDistance;

    // Don't apply linear interpolation if the DedicatedActor has just gone through a cell change, because
    // the interpolated position will be invalid, causing a slight hopping glitch
    if (shouldInterpolate && !hasChangedCell)
    {
        static const float timeMultiplier = 15.0f;
        const float alpha = std::clamp(dt * timeMultiplier, 0.0f, 1.0f);
        osg::Vec3f lerp = MechanicsHelper::getLinearInterpolation(refPos.asVec3(), position.asVec3(), alpha);
        MWWorld::Ptr moved = world->moveObject(ptr, lerp);
        if (!moved.isEmpty())
            ptr = moved;

        constexpr float twoPi = 6.28318530718f;
        constexpr float pi = 3.14159265359f;
        float yawDelta = std::fmod(position.rot[2] - refPos.rot[2], twoPi);
        if (yawDelta > pi)
            yawDelta -= twoPi;
        else if (yawDelta < -pi)
            yawDelta += twoPi;
        const float lerpedYaw = refPos.rot[2] + yawDelta * alpha;
        world->rotateObject(ptr, osg::Vec3f(position.rot[0], position.rot[1], lerpedYaw));
    }
    else
    {
        setPosition();
        hasChangedCell = false;
        world->rotateObject(ptr, osg::Vec3f(position.rot[0], position.rot[1], position.rot[2]));
    }

    if (Main::get().isAuthoritativeServer())
    {
        if (isAuthoritativeMoving)
        {
            authoritativeMoveTimer += dt;
            if (authoritativeMoveTimer >= 0.35f)
            {
                isAuthoritativeMoving = false;
                authoritativeMoveTimer = 0.0f;
                direction.pos[0] = 0.0f;
                direction.pos[1] = 0.0f;
                direction.pos[2] = 0.0f;
            }
        }
        else
        {
            direction.pos[0] = 0.0f;
            direction.pos[1] = 0.0f;
            direction.pos[2] = 0.0f;
        }
    }
    else if ((position.asVec3() - ptr.getRefData().getPosition().asVec3()).length2() < 1.0f)
    {
        direction.pos[0] = 0.0f;
        direction.pos[1] = 0.0f;
        direction.pos[2] = 0.0f;
    }

    setMovementSettings();
}

void DedicatedActor::onPositionPacket(const ESM::Position& newPosition, const ESM::Position& newDirection)
{
    const float dirHorizSq = newDirection.pos[0] * newDirection.pos[0] + newDirection.pos[1] * newDirection.pos[1];
    const float targetDeltaSq = hasPositionData ? (newPosition.asVec3() - position.asVec3()).length2() : 0.0f;

    position = newPosition;
    direction = newDirection;

    if (dirHorizSq > 0.001f || (hasPositionData && targetDeltaSq > 0.25f))
    {
        if (dirHorizSq <= 0.001f)
        {
            // Synthesize forward movement if position advanced without an explicit direction vector.
            direction.pos[0] = 0.0f;
            direction.pos[1] = 1.0f;
        }
        isAuthoritativeMoving = true;
        authoritativeMoveTimer = 0.0f;
    }
    else
    {
        isAuthoritativeMoving = false;
        authoritativeMoveTimer = 0.0f;
    }
}

void DedicatedActor::setMovementSettings()
{
    MWMechanics::Movement *move = &ptr.getClass().getMovementSettings(ptr);
    move->mPosition[0] = direction.pos[0];
    move->mPosition[1] = direction.pos[1];
    move->mPosition[2] = direction.pos[2];

    // Make sure the values are valid, or we'll get an infinite error loop
    if (!isnan(direction.rot[0]) && !isnan(direction.rot[1]) && !isnan(direction.rot[2]))
    {
        move->mRotation[0] = direction.rot[0];
        move->mRotation[1] = direction.rot[1];
        move->mRotation[2] = direction.rot[2];
    }
}

void DedicatedActor::setPosition()
{
    MWBase::World *world = MWBase::Environment::get().getWorld();
    MWWorld::Ptr moved = world->moveObject(ptr, position.asVec3());
    if (!moved.isEmpty())
        ptr = moved;
}

void DedicatedActor::setAnimFlags()
{
    using namespace MWMechanics;

    MWMechanics::CreatureStats *ptrCreatureStats = &ptr.getClass().getCreatureStats(ptr);

    ptrCreatureStats->setDrawState(static_cast<MWMechanics::DrawState>(drawState));

    ptrCreatureStats->setMovementFlag(CreatureStats::Flag_Run, (movementFlags & CreatureStats::Flag_Run) != 0);
    ptrCreatureStats->setMovementFlag(CreatureStats::Flag_Sneak, (movementFlags & CreatureStats::Flag_Sneak) != 0);
    ptrCreatureStats->setMovementFlag(CreatureStats::Flag_ForceJump, (movementFlags & CreatureStats::Flag_ForceJump) != 0);
    ptrCreatureStats->setMovementFlag(CreatureStats::Flag_ForceMoveJump, (movementFlags & CreatureStats::Flag_ForceMoveJump) != 0);
}

void DedicatedActor::setStatsDynamic()
{
    // Only set dynamic stats if we have received at least one packet about them
    if (!hasStatsDynamicData) return;

    MWMechanics::CreatureStats *ptrCreatureStats = &ptr.getClass().getCreatureStats(ptr);
    MWMechanics::DynamicStat<float> value;

    // Resurrect this Actor if it's not supposed to be dead according to its authority
    if (creatureStats.mDynamic[0].mCurrent > 0)
        MWBase::Environment::get().getMechanicsManager()->resurrect(ptr);

    for (int i = 0; i < 3; ++i)
    {
        value.readState(creatureStats.mDynamic[i]);
        ptrCreatureStats->setDynamic(i, value);
    }
}

void DedicatedActor::setEquipment()
{
    if (!ptr.getClass().hasInventoryStore(ptr))
        return;

    MWWorld::InventoryStore& invStore = ptr.getClass().getInventoryStore(ptr);

    for (int slot = 0; slot < MWWorld::InventoryStore::Slots; ++slot)
    {
        int count = equipmentItems[slot].count;

        // If we've somehow received a corrupted item with a count lower than 0, ignore it
        if (count < 0) continue;

        MWWorld::ContainerStoreIterator it = invStore.getSlot(slot);

        const std::string &packetRefId = equipmentItems[slot].refId;
        int packetCharge = equipmentItems[slot].charge;
        std::string storeRefId = "";
        bool equal = false;

        if (it != invStore.end())
        {
            storeRefId = it->getCellRef().getRefId().serializeText();

            if (!Misc::StringUtils::ciEqual(storeRefId, packetRefId)) // if other item equiped
                invStore.unequipSlot(slot);
            else
                equal = true;
        }

        if (packetRefId.empty() || equal)
            continue;

        if (!hasItem(packetRefId, packetCharge))
        {
            ptr.getClass().getContainerStore(ptr).add(ESM::RefId::deserializeText(packetRefId), count, true);
        }

        // Equip items silently if this is the first time equipment is being set for this character
        equipItem(packetRefId, packetCharge, !hasReceivedInitialEquipment);
    }

    hasReceivedInitialEquipment = true;
}

void DedicatedActor::setAi()
{
    MWMechanics::CreatureStats *ptrCreatureStats = &ptr.getClass().getCreatureStats(ptr);
    ptrCreatureStats->setAiSetting(MWMechanics::AiSetting::Fight, 0);

    LOG_APPEND(TimedLog::LOG_VERBOSE, "- actor cellRef: %s %i-%i",
        ptr.getCellRef().getRefId().toDebugString().c_str(), ptr.getCellRef().getRefNum().mIndex, ptr.getCellRef().getMpNum());

    if (aiAction == mwmp::BaseActorList::CANCEL)
    {
        LOG_APPEND(TimedLog::LOG_VERBOSE, "-- Cancelling AI sequence");

        ptrCreatureStats->getAiSequence().clear();
    }
    else if (aiAction == mwmp::BaseActorList::TRAVEL)
    {
        LOG_APPEND(TimedLog::LOG_VERBOSE, "-- Travelling to %f, %f, %f",
            aiCoordinates.pos[0], aiCoordinates.pos[1], aiCoordinates.pos[2]);

        MWMechanics::AiTravel package(aiCoordinates.pos[0], aiCoordinates.pos[1], aiCoordinates.pos[2], false);
        ptrCreatureStats->getAiSequence().stack(package, ptr, true);
    }
    else if (aiAction == mwmp::BaseActorList::WANDER)
    {
        LOG_APPEND(TimedLog::LOG_VERBOSE, "-- Wandering for distance %i and duration %i, repetition is %s",
            aiDistance, aiDuration, aiShouldRepeat ? "true" : "false");

        std::vector<unsigned char> idleList;

        MWMechanics::AiWander package(aiDistance, aiDuration, -1, idleList, aiShouldRepeat);
        ptrCreatureStats->getAiSequence().stack(package, ptr, true);
    }
    else if (aiAction == mwmp::BaseActorList::ARREST)
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "DedicatedActor::setAi ARREST opening GM_Dialogue for %s",
            ptr.getCellRef().getRefId().toDebugString().c_str());

        if (!MWBase::Environment::get().getWindowManager()->containsMode(MWGui::GM_Dialogue)
            && !MWBase::Environment::get().getWindowManager()->containsMode(MWGui::GM_Jail))
        {
            MWBase::Environment::get().getWindowManager()->pushGuiMode(MWGui::GM_Dialogue, ptr);
        }
    }
    else if (hasAiTarget)
    {
        MWWorld::Ptr targetPtr;

        if (aiTarget.isPlayer)
        {
            targetPtr = MechanicsHelper::getPlayerPtr(aiTarget);

            LOG_APPEND(TimedLog::LOG_VERBOSE, "-- Has player target %s",
                std::string(targetPtr.getClass().getName(targetPtr)).c_str());
        }
        else
        {
            if (mwmp::Main::get().getCellController()->isLocalActor(aiTarget.refNum, aiTarget.mpNum))
                targetPtr = mwmp::Main::get().getCellController()->getLocalActor(aiTarget.refNum, aiTarget.mpNum)->getPtr();
            else if (mwmp::Main::get().getCellController()->isDedicatedActor(aiTarget.refNum, aiTarget.mpNum))
                targetPtr = mwmp::Main::get().getCellController()->getDedicatedActor(aiTarget.refNum, aiTarget.mpNum)->getPtr();
            else if (aiAction == mwmp::BaseActorList::ACTIVATE)
                targetPtr = MWBase::Environment::get().getWorld()->searchPtrViaUniqueIndex(aiTarget.refNum, aiTarget.mpNum);

            if (!targetPtr.isEmpty())
            {
                LOG_APPEND(TimedLog::LOG_VERBOSE, "-- Has actor target %s %i-%i",
                    targetPtr.getCellRef().getRefId().toDebugString().c_str(), aiTarget.refNum, aiTarget.mpNum);
            }
            else
            {
                LOG_APPEND(TimedLog::LOG_VERBOSE, "-- Has invalid actor target %i-%i",
                    aiTarget.refNum, aiTarget.mpNum);
            }

        }

        if (!targetPtr.isEmpty())
        {
            if (aiAction == mwmp::BaseActorList::ACTIVATE)
            {
                LOG_APPEND(TimedLog::LOG_VERBOSE, "-- Activating target");

                MWMechanics::AiActivate package(targetPtr.getCellRef().getRefId(), false);
                ptrCreatureStats->getAiSequence().stack(package, ptr, true);
            }

            if (aiAction == mwmp::BaseActorList::COMBAT)
            {
                LOG_APPEND(TimedLog::LOG_VERBOSE, "-- Starting combat with target");

                MWMechanics::AiCombat package(targetPtr);
                ptrCreatureStats->getAiSequence().stack(package, ptr, true);
            }
            else if (aiAction == mwmp::BaseActorList::ESCORT)
            {
                LOG_APPEND(TimedLog::LOG_VERBOSE, "-- Being escorted by target, for duration %i, to coordinates %f, %f, %f",
                    aiDuration, aiCoordinates.pos[0], aiCoordinates.pos[1], aiCoordinates.pos[2]);

                MWMechanics::AiEscort package(targetPtr.getCellRef().getRefId(), {}, aiDuration,
                    aiCoordinates.pos[0], aiCoordinates.pos[1], aiCoordinates.pos[2], false);
                ptrCreatureStats->getAiSequence().stack(package, ptr, true);
            }
            else if (aiAction == mwmp::BaseActorList::FOLLOW)
            {
                LOG_APPEND(TimedLog::LOG_VERBOSE, "-- Following target");

                MWMechanics::AiFollow package(targetPtr);
                package.allowAnyDistance(true);
                ptrCreatureStats->getAiSequence().stack(package, ptr, true);
            }
        }
    }
}

void DedicatedActor::playAnimation()
{
    if (!animation.groupname.empty())
    {
        MWBase::Environment::get().getMechanicsManager()->playAnimationGroup(ptr,
            animation.groupname, animation.mode, animation.count, animation.persist);

        animation.groupname.clear();
    }
}

void DedicatedActor::playSound()
{
    if (!sound.empty())
    {
        MWBase::Environment::get().getSoundManager()->say(
            ptr, Misc::ResourceHelpers::correctSoundPath(VFS::Path::Normalized(sound)));

        MWBase::WindowManager *winMgr = MWBase::Environment::get().getWindowManager();
        if (Settings::gui().mSubtitles)
            winMgr->messageBox(MWBase::Environment::get().getDialogueManager()->getVoiceCaption(sound), MWGui::ShowInDialogueMode_Never);

        sound.clear();
    }
}

bool DedicatedActor::hasItem(std::string itemId, int charge)
{
    for (const auto &itemPtr : ptr.getClass().getInventoryStore(ptr))
    {
        if (::Misc::StringUtils::ciEqual(itemPtr.getCellRef().getRefId().serializeText(), itemId) && itemPtr.getCellRef().getCharge() == charge)
            return true;
    }

    return false;
}

void DedicatedActor::equipItem(std::string itemId, int charge, bool noSound)
{
    for (const auto &itemPtr : ptr.getClass().getInventoryStore(ptr))
    {
        if (::Misc::StringUtils::ciEqual(itemPtr.getCellRef().getRefId().serializeText(), itemId) && itemPtr.getCellRef().getCharge() == charge)
        {
            std::shared_ptr<MWWorld::Action> action = itemPtr.getClass().use(itemPtr);
            action->execute(ptr, noSound);
            break;
        }
    }
}

void DedicatedActor::addSpellsActive()
{
    MWMechanics::ActiveSpells& activeSpells = getPtr().getClass().getCreatureStats(getPtr()).getActiveSpells();

    for (const auto& activeSpell : spellsActiveChanges.activeSpells)
    {
        MWWorld::TimeStamp timestamp = MWWorld::TimeStamp(activeSpell.timestampHour, activeSpell.timestampDay);
        ESM::RefNum casterRefNum = MechanicsHelper::getActorId(activeSpell.caster);

        MechanicsHelper::createSpellGfx(getPtr(), activeSpell.params.mEffects);

        // Don't do a check for a spell's existence, because active effects from potions need to be applied here too
        activeSpells.addSpell(ESM::RefId::stringRefId(activeSpell.id), activeSpell.isStackingSpell, activeSpell.params.mEffects, activeSpell.params.mDisplayName, casterRefNum, timestamp);
    }
}

void DedicatedActor::removeSpellsActive()
{
    MWMechanics::ActiveSpells& activeSpells = getPtr().getClass().getCreatureStats(getPtr()).getActiveSpells();

    for (const auto& activeSpell : spellsActiveChanges.activeSpells)
    {
        // Remove stacking spells based on their timestamps
        if (activeSpell.isStackingSpell)
        {
            MWWorld::TimeStamp timestamp = MWWorld::TimeStamp(activeSpell.timestampHour, activeSpell.timestampDay);
            activeSpells.removeSpellByTimestamp(getPtr(), ESM::RefId::stringRefId(activeSpell.id), timestamp);
        }
        else
        {
            activeSpells.removeEffectsBySourceSpellId(getPtr(), ESM::RefId::stringRefId(activeSpell.id));
        }
    }
}

void DedicatedActor::setSpellsActive()
{
    MWMechanics::ActiveSpells& activeSpells = getPtr().getClass().getCreatureStats(getPtr()).getActiveSpells();
    activeSpells.clear(getPtr());

    // Proceed by adding spells active
    addSpellsActive();
}

MWWorld::Ptr DedicatedActor::getPtr()
{
    return ptr;
}

void DedicatedActor::setPtr(const MWWorld::Ptr& newPtr)
{
    ptr = newPtr;

    refId = ptr.getCellRef().getRefId().serializeText();
    refNum = ptr.getCellRef().getRefNum().mIndex;
    mpNum = ptr.getCellRef().getMpNum();

    position = ptr.getRefData().getPosition();
    drawState = static_cast<char>(ptr.getClass().getCreatureStats(ptr).getDrawState());
}

void DedicatedActor::reloadPtr()
{
    MWBase::World* world = MWBase::Environment::get().getWorld();
    world->disable(ptr);
    world->enable(ptr);
}
