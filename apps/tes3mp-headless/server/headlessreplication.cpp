#include "headlessreplication.hpp"

#include "playeranchormanager.hpp"

#include <apps/openmw-mp/Cell.hpp>
#include <apps/openmw-mp/CellController.hpp>
#include <apps/openmw-mp/Networking.hpp>
#include <apps/openmw-mp/Player.hpp>
#include <apps/openmw-mp/Script/Script.hpp>
#include <apps/openmw-mp/Utils.hpp>
#include <apps/openmw/mwmechanics/creaturestats.hpp>
#include <apps/openmw/mwmechanics/movement.hpp>
#include <apps/openmw/mwmechanics/weapontype.hpp>
#include <apps/openmw/mwworld/cellstore.hpp>
#include <apps/openmw/mwworld/class.hpp>
#include <apps/openmw/mwworld/containerstore.hpp>
#include <apps/openmw/mwworld/inventorystore.hpp>
#include <apps/openmw/mwworld/scene.hpp>
#include <apps/openmw/mwworld/worldimp.hpp>
#include <components/debug/debuglog.hpp>
#include <components/esm/esmbridge.hpp>
#include <components/esm3/loadcont.hpp>
#include <components/esm3/loadweap.hpp>
#include <components/openmw-mp/Base/BaseActor.hpp>
#include <components/openmw-mp/Base/BaseObject.hpp>
#include <components/openmw-mp/NetworkMessages.hpp>
#include <components/openmw-mp/Packets/Actor/ActorPacket.hpp>
#include <components/openmw-mp/Packets/Object/ObjectPacket.hpp>
#include <RakPeerInterface.h>

#include <algorithm>
#include <cmath>
#include <functional>

namespace TES3MPHeadless
{
    namespace
    {
        ESM::Cell cellFromStore(const MWWorld::CellStore& store)
        {
            ESM::Cell out;
            out.blank();
            if (store.isExterior())
            {
                out.mData.mFlags &= ~ESM::Cell::Interior;
                out.mData.mX = store.getCell()->getGridX();
                out.mData.mY = store.getCell()->getGridY();
            }
            else
            {
                out.mData.mFlags |= ESM::Cell::Interior;
                out.mName = store.getCell()->getNameId();
            }
            return out;
        }

        unsigned int readMovementFlags(const MWMechanics::CreatureStats& stats)
        {
            unsigned int flags = 0;
            for (const auto bit : { MWMechanics::CreatureStats::Flag_ForceRun,
                     MWMechanics::CreatureStats::Flag_ForceSneak, MWMechanics::CreatureStats::Flag_Run,
                     MWMechanics::CreatureStats::Flag_Sneak, MWMechanics::CreatureStats::Flag_ForceJump,
                     MWMechanics::CreatureStats::Flag_ForceMoveJump })
            {
                if (stats.getMovementFlag(bit))
                    flags |= static_cast<unsigned int>(bit);
            }
            return flags;
        }

        mwmp::BaseActor buildBaseActor(const MWWorld::Ptr& ptr, const MWWorld::World& world, const ESM::Cell& cell,
            bool includePos, bool includeStats, bool includeDeath)
        {
            mwmp::BaseActor actor;
            actor.refId = ptr.getCellRef().getRefId().getRefIdString();
            actor.mpNum = ptr.getCellRef().getMpNum();
            actor.refNum = (actor.mpNum != 0) ? 0 : ptr.getCellRef().getRefNum().mIndex;
            actor.cell = cell;

            const ESM::Position& pos = ptr.getRefData().getPosition();
            actor.position = pos;
            const MWMechanics::Movement& movement = ptr.getClass().getMovementSettings(ptr);
            actor.direction.pos[0] = movement.mPosition[0];
            actor.direction.pos[1] = movement.mPosition[1];
            actor.direction.pos[2] = movement.mPosition[2];
            actor.direction.rot[0] = movement.mRotation[0];
            actor.direction.rot[1] = movement.mRotation[1];
            actor.direction.rot[2] = movement.mRotation[2];
            actor.hasPositionData = includePos;

            const MWMechanics::CreatureStats& stats = ptr.getClass().getCreatureStats(ptr);
            actor.movementFlags = readMovementFlags(stats);
            actor.drawState = static_cast<char>(stats.getDrawState());
            actor.isFlying = world.isFlying(ptr);

            stats.getDynamic(0).writeState(actor.creatureStats.mDynamic[0]);
            stats.getDynamic(1).writeState(actor.creatureStats.mDynamic[1]);
            stats.getDynamic(2).writeState(actor.creatureStats.mDynamic[2]);
            actor.creatureStats.mDead = stats.isDead();
            actor.creatureStats.mDeathAnimationFinished = stats.isDeathAnimationFinished();
            actor.hasStatsDynamicData = includeStats;

            if (includeDeath)
            {
                actor.deathState = stats.isDeathAnimationFinished() ? 2 : 1;
                actor.isInstantDeath = false;
                actor.killer.isPlayer = false;
                actor.killer.refNum = 0;
                actor.killer.mpNum = 0;
            }
            return actor;
        }

        std::uint64_t hashCombine(std::uint64_t seed, std::uint64_t value)
        {
            seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
            return seed;
        }

        std::uint64_t hashEquipmentSlot(std::uint64_t h, const std::string& refId, int count)
        {
            h = hashCombine(h, std::hash<std::string>{}(refId));
            return hashCombine(h, static_cast<std::uint64_t>(count));
        }

        std::uint64_t computeEquipmentHash(const MWWorld::Ptr& ptr)
        {
            std::uint64_t outHash = 1469598103934665603ULL;
            if (!ptr.getClass().hasInventoryStore(ptr))
                return outHash;

            MWWorld::InventoryStore& inv = const_cast<MWWorld::Ptr&>(ptr).getClass().getInventoryStore(ptr);
            for (int slot = 0; slot < MWWorld::InventoryStore::Slots; ++slot)
            {
                const MWWorld::ConstContainerStoreIterator it = inv.getSlot(slot);
                if (it != inv.end())
                    outHash = hashEquipmentSlot(outHash, it->getCellRef().getRefId().serializeText(), it->getCellRef().getCount());
                else
                    outHash = hashEquipmentSlot(outHash, std::string(), 0);
            }
            return outHash;
        }

        void populateActorEquipment(const MWWorld::Ptr& ptr, mwmp::BaseActor& actor, std::uint64_t& outHash)
        {
            outHash = 1469598103934665603ULL;
            if (!ptr.getClass().hasInventoryStore(ptr))
                return;

            MWWorld::InventoryStore& inv = const_cast<MWWorld::Ptr&>(ptr).getClass().getInventoryStore(ptr);
            for (int slot = 0; slot < MWWorld::InventoryStore::Slots; ++slot)
            {
                mwmp::Item& item = actor.equipmentItems[slot];
                item = {};
                const MWWorld::ConstContainerStoreIterator it = inv.getSlot(slot);
                if (it != inv.end())
                {
                    item.refId = it->getCellRef().getRefId().serializeText();
                    item.count = it->getCellRef().getCount();
                    item.charge = it->getCellRef().getCharge();
                    item.enchantmentCharge = it->getCellRef().getEnchantmentCharge();
                }
                outHash = hashEquipmentSlot(outHash, item.refId, item.count);
            }
        }

        std::uint64_t computeContainerHash(MWWorld::ContainerStore& store)
        {
            std::uint64_t h = 1469598103934665603ULL;
            for (MWWorld::ConstContainerStoreIterator it = store.cbegin(); it != store.cend(); ++it)
            {
                h = hashCombine(h, std::hash<std::string>{}(it->getCellRef().getRefId().serializeText()));
                h = hashCombine(h, static_cast<std::uint64_t>(it->getCellRef().getCount()));
            }
            return h;
        }

        mwmp::BaseObject buildContainerObject(const MWWorld::Ptr& ptr, MWWorld::ContainerStore& store)
        {
            mwmp::BaseObject obj;
            obj.refId = ptr.getCellRef().getRefId().serializeText();
            obj.refNum = ptr.getCellRef().getRefNum().mIndex;
            obj.mpNum = ptr.getCellRef().getMpNum();
            obj.position = ptr.getRefData().getPosition();
            for (MWWorld::ConstContainerStoreIterator it = store.cbegin(); it != store.cend(); ++it)
            {
                mwmp::ContainerItem ci;
                ci.refId = it->getCellRef().getRefId().serializeText();
                ci.count = it->getCellRef().getCount();
                ci.charge = it->getCellRef().getCharge();
                ci.enchantmentCharge = it->getCellRef().getEnchantmentCharge();
                ci.soul = it->getCellRef().getSoul().serializeText();
                ci.actionCount = ci.count;
                obj.containerItems.push_back(std::move(ci));
            }
            return obj;
        }
    }

    HeadlessReplicationSystem::HeadlessReplicationSystem(MWWorld::World& world)
        : mWorld(world)
    {
    }

    void HeadlessReplicationSystem::clear()
    {
        mActorSnapshots.clear();
        mContainerSnapshots.clear();
        mAuthoritativeDeltaCount = 0;
        mDeltaStats = {};
        mStallBroadcastCycles = 0;
        mContainerSweepCounter = 0;
    }

    AuthoritativeDeltaStats HeadlessReplicationSystem::consumeAuthoritativeDeltaStats(std::uint64_t& outTotalDeltas)
    {
        outTotalDeltas = mAuthoritativeDeltaCount;
        const AuthoritativeDeltaStats delta = mDeltaStats;
        mAuthoritativeDeltaCount = 0;
        mDeltaStats = {};
        return delta;
    }

    void HeadlessReplicationSystem::collectConnectedObservers(
        const std::unordered_set<unsigned short>& playersWithKnownCell, const HeadlessPlayerAnchorManager& anchors,
        std::vector<ConnectedObserver>& outObservers) const
    {
        outObservers.clear();
        TPlayers* players = Players::getPlayers();
        if (!players)
            return;

        for (const auto& [guid, player] : *players)
        {
            if (!player || player->getLoadState() != Player::POSTLOADED)
                continue;
            // Never treat the default (0, 0) cell of a freshly connected player as real until the client
            // confirms its cell via ID_PLAYER_CELL_CHANGE.
            if (playersWithKnownCell.find(player->getId()) == playersWithKnownCell.end())
                continue;

            ConnectedObserver obs;
            obs.mPlayer = player;
            obs.mPosition = player->position.asVec3();
            obs.mIsExterior = player->cell.isExterior();
            obs.mCell = HeadlessPlayerAnchorManager::resolveCellStore(mWorld, player->cell);
            if (obs.mIsExterior)
            {
                const ESM::ExteriorCellLocation posCell = ESM::positionToExteriorCellLocation(
                    obs.mPosition.x(), obs.mPosition.y(), ESM::Cell::sDefaultWorldspaceId);
                if (posCell.mX != player->cell.mData.mX || posCell.mY != player->cell.mData.mY)
                {
                    bool usedAnchor = false;
                    MWWorld::Ptr anchor = anchors.getAnchorPtr(player->getId());
                    if (!anchor.isEmpty() && anchor.isInCell() && anchor.getCell()->isExterior())
                    {
                        const osg::Vec3f anchorPos = anchor.getRefData().getPosition().asVec3();
                        const ESM::ExteriorCellLocation anchorCell = ESM::positionToExteriorCellLocation(
                            anchorPos.x(), anchorPos.y(), ESM::Cell::sDefaultWorldspaceId);
                        if (anchorCell.mX == player->cell.mData.mX && anchorCell.mY == player->cell.mData.mY)
                        {
                            obs.mPosition = anchorPos;
                            usedAnchor = true;
                        }
                    }
                    if (!usedAnchor)
                    {
                        const float cellSize = ESM::getCellSize(ESM::Cell::sDefaultWorldspaceId);
                        obs.mPosition.x() = (player->cell.mData.mX + 0.5f) * cellSize;
                        obs.mPosition.y() = (player->cell.mData.mY + 0.5f) * cellSize;
                    }
                }
            }
            outObservers.push_back(obs);
        }
    }

    void HeadlessReplicationSystem::broadcastAuthoritativeActorDeltas(
        const std::unordered_set<unsigned short>& playersWithKnownCell, const mwmp::ServerAuthorityConfig& authority,
        RakNet::RakPeerInterface* peer, mwmp::Networking* networking, const HeadlessPlayerAnchorManager& anchors)
    {
        if (!authority.authoritativeMode || !networking)
            return;

        // Pause actor delta broadcasting entirely while no client is connected.
        std::vector<ConnectedObserver> connected;
        collectConnectedObservers(playersWithKnownCell, anchors, connected);
        if (connected.empty())
            return;

        mwmp::ActorPacketController* actorPackets = networking->getActorPacketController();
        if (!actorPackets)
            return;

        const float posThreshSq = authority.actorPosDeltaSq;
        const float rotThresh = authority.actorRotDeltaRad;
        const float statThresh = authority.actorStatDelta;
        const float broadcastRadius = std::max(authority.actorSimulationRadius, authority.actorBroadcastRadius);
        const float broadcastRadiusSq = broadcastRadius * broadcastRadius;
        const std::size_t maxBatch = std::max<std::size_t>(1, authority.maxActorsPerBroadcast);
        const bool sweepContainersThisCycle = ((mContainerSweepCounter++ % 20) == 0);
        const std::uint64_t initialDeltaCount = mAuthoritativeDeltaCount;
        std::size_t evaluatedActors = 0;

        for (MWWorld::CellStore* cellStore : mWorld.getWorldScene().getActiveCells())
        {
            if (!cellStore)
                continue;

            ESM::Cell esmCell = cellFromStore(*cellStore);
            // Legacy CellController state is optional: it only exists if a client sent ID_PLAYER_CELL_STATE.
            Cell* serverCell = CellController::get()->findCell(&esmCell);

            const bool isExterior = cellStore->isExterior();

            // Candidate recipients for this cell, using live player positions.
            std::vector<const ConnectedObserver*> candidates;
            for (const ConnectedObserver& obs : connected)
            {
                if (isExterior ? obs.mIsExterior : (obs.mCell == cellStore))
                    candidates.push_back(&obs);
            }
            if (serverCell)
            {
                // Also include anyone the legacy CellController knows has this cell loaded.
                for (const ::Player* pl : serverCell->getPlayers())
                {
                    if (!pl)
                        continue;
                    for (const ConnectedObserver& obs : connected)
                    {
                        if (obs.mPlayer == pl
                            && std::find(candidates.begin(), candidates.end(), &obs) == candidates.end())
                            candidates.push_back(&obs);
                    }
                }
            }
            if (candidates.empty())
                continue;

            std::vector<bool> recipientInRange(candidates.size(), !isExterior);

            mwmp::BaseActorList posBatch;
            mwmp::BaseActorList animBatch;
            mwmp::BaseActorList statsBatch;
            mwmp::BaseActorList equipBatch;
            mwmp::BaseActorList attackBatch;
            mwmp::BaseActorList deathBatch;
            posBatch.cell = animBatch.cell = statsBatch.cell = equipBatch.cell = attackBatch.cell = deathBatch.cell
                = esmCell;

            mwmp::BaseObjectList containerBatch;
            containerBatch.cell = esmCell;
            containerBatch.action = mwmp::BaseObjectList::SET;
            containerBatch.containerSubAction = mwmp::BaseObjectList::NONE;
            containerBatch.packetOrigin = mwmp::SERVER_SCRIPT;

            cellStore->forEach([&](const MWWorld::Ptr& ptr) {
                if (!ptr.getRefData().isEnabled() || anchors.isPlayerAnchorPtr(ptr))
                    return true;

                const bool isActor = ptr.getClass().isActor();
                const bool isContainer = ptr.getType() == ESM::Container::sRecordId;
                if (!isActor && !isContainer)
                    return true;

                const ESM::Position& esmPos = ptr.getRefData().getPosition();
                const osg::Vec3f pos = esmPos.asVec3();
                if (isExterior)
                {
                    bool inVisibilityRadius = false;
                    for (std::size_t i = 0; i < candidates.size(); ++i)
                    {
                        if ((candidates[i]->mPosition - pos).length2() <= broadcastRadiusSq)
                        {
                            inVisibilityRadius = true;
                            recipientInRange[i] = true;
                        }
                    }
                    if (!inVisibilityRadius)
                        return true;
                }

                const MWWorld::LiveCellRefBase* key = ptr.getBase();

                if (isContainer)
                {
                    const bool isNewContainer = (mContainerSnapshots.find(key) == mContainerSnapshots.end());
                    if ((isNewContainer || sweepContainersThisCycle) && containerBatch.baseObjects.size() < maxBatch)
                    {
                        MWWorld::ContainerStore& store = const_cast<MWWorld::Ptr&>(ptr).getClass().getContainerStore(ptr);
                        if (!store.isResolved())
                            store.resolve();
                        const std::uint64_t cHash = computeContainerHash(store);
                        auto [cIt, cInserted] = mContainerSnapshots.try_emplace(key, cHash);
                        if (cInserted || cIt->second != cHash)
                        {
                            cIt->second = cHash;
                            containerBatch.baseObjects.push_back(buildContainerObject(ptr, store));
                            ++mAuthoritativeDeltaCount;
                        }
                    }
                    return true;
                }

                ++evaluatedActors;

                const osg::Vec3f rot = esmPos.asRotationVec3();
                const MWMechanics::CreatureStats& stats = ptr.getClass().getCreatureStats(ptr);
                const unsigned int moveFlags = readMovementFlags(stats);
                const char drawState = static_cast<char>(stats.getDrawState());
                const bool flying = mWorld.isFlying(ptr);
                const float health = stats.getHealth().getCurrent();
                const float magicka = stats.getMagicka().getCurrent();
                const float fatigue = stats.getFatigue().getCurrent();
                const bool dead = stats.isDead();

                auto [it, inserted] = mActorSnapshots.try_emplace(key);
                ActorSnapshot& snap = it->second;
                if (inserted || !snap.mInitialized)
                {
                    if (ptr.getClass().hasInventoryStore(ptr))
                    {
                        MWWorld::InventoryStore& inv = const_cast<MWWorld::Ptr&>(ptr).getClass().getInventoryStore(ptr);
                        inv.resolve();
                        if (!dead)
                            inv.autoEquip();
                    }
                    snap = ActorSnapshot{ pos, rot, moveFlags, drawState, flying, health, magicka, fatigue, dead, dead, 0,
                        false, false, false, true };
                    if (ptr.getClass().hasInventoryStore(ptr) && equipBatch.baseActors.size() < maxBatch)
                    {
                        mwmp::BaseActor equipActor = buildBaseActor(ptr, mWorld, esmCell, false, false, false);
                        populateActorEquipment(ptr, equipActor, snap.mEquipmentHash);
                        snap.mEquipmentBroadcasted = true;
                        equipBatch.baseActors.push_back(std::move(equipActor));
                        ++mAuthoritativeDeltaCount;
                        ++mDeltaStats.mEquip;
                    }
                    return true;
                }

                // Reset one-shot death flag only if the actor was explicitly resurrected.
                if (!dead && snap.mDeathBroadcasted)
                    snap.mDeathBroadcasted = false;

                // Resolve and broadcast corpse inventory once dead.
                if (dead && ptr.getClass().hasInventoryStore(ptr) && containerBatch.baseObjects.size() < maxBatch)
                {
                    MWWorld::ContainerStore& store = const_cast<MWWorld::Ptr&>(ptr).getClass().getContainerStore(ptr);
                    store.resolve();
                    const std::uint64_t cHash = computeContainerHash(store);
                    auto [cIt, cInserted] = mContainerSnapshots.try_emplace(key, cHash);
                    if (cInserted || cIt->second != cHash)
                    {
                        cIt->second = cHash;
                        containerBatch.baseObjects.push_back(buildContainerObject(ptr, store));
                        ++mAuthoritativeDeltaCount;
                    }
                }

                const bool batchCapacityRemaining = posBatch.baseActors.size() < maxBatch
                    && animBatch.baseActors.size() < maxBatch && statsBatch.baseActors.size() < maxBatch;

                const float dx = pos.x() - snap.mPos.x();
                const float dy = pos.y() - snap.mPos.y();
                const float horizLenSq = dx * dx + dy * dy;
                const bool isMovingNow = horizLenSq > 0.25f;
                const bool stoppedMoving = snap.mWasMoving && !isMovingNow;

                const bool posDirty = batchCapacityRemaining
                    && ((pos - snap.mPos).length2() >= posThreshSq || std::abs(rot.z() - snap.mRot.z()) >= rotThresh
                        || std::abs(rot.x() - snap.mRot.x()) >= rotThresh || stoppedMoving);
                const bool animDirty = batchCapacityRemaining
                    && (moveFlags != snap.mMovementFlags || drawState != snap.mDrawState || flying != snap.mIsFlying);
                const bool statsDirty = batchCapacityRemaining
                    && (std::abs(health - snap.mHealth) >= statThresh
                        || std::abs(magicka - snap.mMagicka) >= statThresh
                        || std::abs(fatigue - snap.mFatigue) >= statThresh || (dead != snap.mDead));
                const bool deathDirty = (dead && !snap.mDeathBroadcasted);

                const bool attacking = stats.getAttackingOrSpell();
                const bool attackDirty = batchCapacityRemaining && (attacking != snap.mAttacking)
                    && drawState == static_cast<char>(MWMechanics::DrawState::Weapon);
                if (attacking != snap.mAttacking && !attackDirty)
                    snap.mAttacking = attacking;

                bool equipDirty = false;
                std::uint64_t currentEquipHash = snap.mEquipmentHash;
                if (ptr.getClass().hasInventoryStore(ptr) && equipBatch.baseActors.size() < maxBatch)
                {
                    currentEquipHash = computeEquipmentHash(ptr);
                    equipDirty = (!snap.mEquipmentBroadcasted || currentEquipHash != snap.mEquipmentHash);
                }

                if (!posDirty && !animDirty && !statsDirty && !deathDirty && !equipDirty && !attackDirty)
                    return true;

                if (equipDirty)
                {
                    mwmp::BaseActor equipActor = buildBaseActor(ptr, mWorld, esmCell, false, false, false);
                    populateActorEquipment(ptr, equipActor, snap.mEquipmentHash);
                    equipBatch.baseActors.push_back(std::move(equipActor));
                    snap.mEquipmentBroadcasted = true;
                    ++mAuthoritativeDeltaCount;
                    ++mDeltaStats.mEquip;
                }

                if (!posDirty && !animDirty && !statsDirty && !deathDirty && !attackDirty)
                    return true;

                mwmp::BaseActor baseActor = buildBaseActor(ptr, mWorld, esmCell, posDirty, statsDirty, deathDirty);

                if (attackDirty)
                {
                    mwmp::BaseActor atkActor = baseActor;
                    bool isRanged = false;
                    if (ptr.getClass().hasInventoryStore(ptr))
                    {
                        const MWWorld::InventoryStore& inv = ptr.getClass().getInventoryStore(ptr);
                        auto weapIt = inv.getSlot(MWWorld::InventoryStore::Slot_CarriedRight);
                        if (weapIt != inv.end() && weapIt->getType() == ESM::Weapon::sRecordId)
                        {
                            const int weapClass
                                = MWMechanics::getWeaponType(weapIt->get<ESM::Weapon>()->mBase->mData.mType)
                                      ->mWeaponClass;
                            isRanged = (weapClass == ESM::WeaponType::Ranged || weapClass == ESM::WeaponType::Thrown);
                        }
                    }
                    atkActor.attack.type = isRanged ? mwmp::Attack::RANGED : mwmp::Attack::MELEE;
                    atkActor.attack.pressed = attacking;
                    atkActor.attack.attackAnimation = isRanged ? "shoot" : "slash";
                    atkActor.attack.isHit = false;
                    attackBatch.baseActors.push_back(std::move(atkActor));
                    snap.mAttacking = attacking;
                    ++mAuthoritativeDeltaCount;
                }

                if (posDirty)
                {
                    if (isMovingNow)
                    {
                        const float invLen = 1.0f / std::sqrt(horizLenSq);
                        const float nx = dx * invLen;
                        const float ny = dy * invLen;
                        const float sinZ = std::sin(rot.z());
                        const float cosZ = std::cos(rot.z());
                        baseActor.direction.pos[0] = nx * cosZ - ny * sinZ;
                        baseActor.direction.pos[1] = nx * sinZ + ny * cosZ;
                    }
                    else
                    {
                        baseActor.direction.pos[0] = 0.0f;
                        baseActor.direction.pos[1] = 0.0f;
                        baseActor.direction.pos[2] = 0.0f;
                    }
                    posBatch.baseActors.push_back(baseActor);
                    snap.mPos = pos;
                    snap.mRot = rot;
                    snap.mWasMoving = isMovingNow;
                    ++mAuthoritativeDeltaCount;
                    ++mDeltaStats.mPos;
                }
                if (animDirty)
                {
                    animBatch.baseActors.push_back(baseActor);
                    snap.mMovementFlags = moveFlags;
                    snap.mDrawState = drawState;
                    snap.mIsFlying = flying;
                    ++mAuthoritativeDeltaCount;
                    ++mDeltaStats.mAnim;
                }
                if (statsDirty)
                {
                    statsBatch.baseActors.push_back(baseActor);
                    snap.mHealth = health;
                    snap.mMagicka = magicka;
                    snap.mFatigue = fatigue;
                    snap.mDead = dead;
                    ++mAuthoritativeDeltaCount;
                    ++mDeltaStats.mStats;
                }
                if (deathDirty)
                {
                    deathBatch.baseActors.push_back(baseActor);
                    snap.mDead = true;
                    snap.mDeathBroadcasted = true;
                    ++mAuthoritativeDeltaCount;
                }
                return true;
            });

            if (posBatch.baseActors.empty() && animBatch.baseActors.empty() && statsBatch.baseActors.empty()
                && equipBatch.baseActors.empty() && attackBatch.baseActors.empty() && deathBatch.baseActors.empty()
                && containerBatch.baseObjects.empty())
            {
                continue;
            }

            const RakNet::RakNetGUID serverGuid = peer ? peer->GetMyGUID() : RakNet::RakNetGUID();

            std::vector<::Player*> recipients;
            for (std::size_t i = 0; i < candidates.size(); ++i)
            {
                ::Player* pl = candidates[i]->mPlayer;
                if (recipientInRange[i] && pl && !pl->npc.mName.empty())
                    recipients.push_back(pl);
            }

            auto sendToRecipients = [&](unsigned char packetId, mwmp::BaseActorList& batch) {
                batch.guid = serverGuid;
                mwmp::ActorPacket* pkt = actorPackets->GetPacket(packetId);
                for (::Player* pl : recipients)
                {
                    pkt->setActorList(&batch);
                    pkt->Send(pl->guid);
                }
            };

            auto dispatchActorBatch = [&](unsigned char packetId, mwmp::BaseActorList& batch, bool updateCellStore) {
                if (batch.baseActors.empty())
                    return;
                batch.count = static_cast<unsigned int>(batch.baseActors.size());
                if (serverCell && updateCellStore)
                    serverCell->readActorList(packetId, &batch);
                sendToRecipients(packetId, batch);
            };

            dispatchActorBatch(ID_ACTOR_POSITION, posBatch, true);
            dispatchActorBatch(ID_ACTOR_ANIM_FLAGS, animBatch, false);
            dispatchActorBatch(ID_ACTOR_STATS_DYNAMIC, statsBatch, true);
            dispatchActorBatch(ID_ACTOR_EQUIPMENT, equipBatch, true);
            dispatchActorBatch(ID_ACTOR_ATTACK, attackBatch, false);
            if (!containerBatch.baseObjects.empty())
            {
                if (mwmp::ObjectPacket* objPkt = networking->getObjectPacketController()->GetPacket(ID_CONTAINER))
                {
                    containerBatch.guid = serverGuid;
                    for (::Player* pl : recipients)
                    {
                        objPkt->setObjectList(&containerBatch);
                        objPkt->Send(pl->guid);
                    }
                }
            }
            if (!deathBatch.baseActors.empty())
            {
                deathBatch.count = static_cast<unsigned int>(deathBatch.baseActors.size());
                sendToRecipients(ID_ACTOR_DEATH, deathBatch);
                ::Player* observer = !recipients.empty() ? recipients.front() : candidates.front()->mPlayer;
                if (observer)
                {
                    Script::Call<Script::CallbackIdentity("OnActorDeath")>(
                        observer->getId(), Utils::getCellDescription(esmCell).c_str());
                }
            }
        }

        if (mAuthoritativeDeltaCount == initialDeltaCount)
        {
            if (++mStallBroadcastCycles >= 60)
            {
                mStallBroadcastCycles = 0;
                const ConnectedObserver& obs0 = connected.front();
                const std::string obsCell = obs0.mPlayer ? Utils::getCellDescription(obs0.mPlayer->cell) : "<none>";
                Log(Debug::Warning) << "Authoritative broadcast stall (3s, players=" << connected.size()
                                    << ", anchors=" << anchors.getAnchors().size()
                                    << ", anchor0=" << obsCell << " (" << obs0.mPosition.x() << ", "
                                    << obs0.mPosition.y() << ", " << obs0.mPosition.z() << ")"
                                    << ", activeCells=" << mWorld.getWorldScene().getActiveCells().size()
                                    << ", evaluatedActors=" << evaluatedActors << ")";
            }
        }
        else
        {
            mStallBroadcastCycles = 0;
        }
    }
}
