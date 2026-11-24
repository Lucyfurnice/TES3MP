#include "headlesscombat.hpp"

#include "playeranchormanager.hpp"
#include "stubs/headlesswindowmanager.hpp"

#include <apps/openmw-mp/Networking.hpp>
#include <apps/openmw-mp/Player.hpp>
#include <apps/openmw-mp/Script/Script.hpp>
#include <apps/openmw/mwbase/environment.hpp>
#include <apps/openmw/mwbase/mechanicsmanager.hpp>
#include <apps/openmw/mwbase/windowmanager.hpp>
#include <apps/openmw/mwmechanics/aisequence.hpp>
#include <apps/openmw/mwmechanics/combat.hpp>
#include <apps/openmw/mwmechanics/creaturestats.hpp>
#include <apps/openmw/mwmechanics/movement.hpp>
#include <apps/openmw/mwphysics/physicssystem.hpp>
#include <apps/openmw/mwphysics/raycasting.hpp>
#include <apps/openmw/mwworld/cellstore.hpp>
#include <apps/openmw/mwworld/class.hpp>
#include <apps/openmw/mwworld/inventorystore.hpp>
#include <apps/openmw/mwworld/scene.hpp>
#include <apps/openmw/mwworld/worldimp.hpp>
#include <apps/openmw/mwworld/worldmodel.hpp>
#include <components/debug/debuglog.hpp>
#include <components/esm3/loadweap.hpp>
#include <components/esm3/refnum.hpp>
#include <components/misc/rng.hpp>
#include <components/misc/strings/algorithm.hpp>
#include <components/openmw-mp/NetworkMessages.hpp>
#include <components/openmw-mp/Packets/Player/PlayerPacket.hpp>

#include <algorithm>

namespace TES3MPHeadless
{
    HeadlessCombatController::HeadlessCombatController(MWWorld::World& world)
        : mWorld(world)
    {
    }

    void HeadlessCombatController::setPvpConfig(
        bool pvpEnabled, std::vector<std::string> pvpFreeCells, std::vector<std::string> pvpFreeRegions)
    {
        mPvpEnabled = pvpEnabled;
        mPvpFreeCells = std::move(pvpFreeCells);
        mPvpFreeRegions = std::move(pvpFreeRegions);
    }

    bool HeadlessCombatController::isPvpAllowed(const MWWorld::CellStore* cell, const osg::Vec3f& pos) const
    {
        (void)pos;
        if (!mPvpEnabled)
            return false;
        // An unresolved cell cannot be checked against the PvP-free lists, so treat it as not allowed.
        if (!cell || !cell->getCell())
            return false;

        std::string cellName(cell->getCell()->getNameId());
        if (const std::size_t instPos = cellName.find(" - Instance for "); instPos != std::string::npos)
            cellName.resize(instPos);
        else if (const std::size_t partyPos = cellName.find("#party_"); partyPos != std::string::npos)
            cellName.resize(partyPos);

        if (!cellName.empty())
        {
            for (const std::string& freeCell : mPvpFreeCells)
            {
                if (!freeCell.empty() && Misc::StringUtils::ciEqual(cellName, freeCell))
                    return false;
            }
        }

        const std::string regionName = cell->getCell()->getRegion().getRefIdString();
        if (!regionName.empty())
        {
            for (const std::string& freeRegion : mPvpFreeRegions)
            {
                if (!freeRegion.empty() && Misc::StringUtils::ciEqual(regionName, freeRegion))
                    return false;
            }
        }

        return true;
    }

    MWWorld::Ptr HeadlessCombatController::resolveTargetPtr(
        const mwmp::Target& target, const MWWorld::CellStore* hintCell, const HeadlessPlayerAnchorManager& anchors) const
    {
        if (target.isPlayer)
        {
            if (::Player* targetPlayer = Players::getPlayer(target.guid))
                return anchors.getAnchorPtr(targetPlayer->getId());
            return MWWorld::Ptr();
        }

        if (hintCell)
        {
            MWWorld::Ptr found
                = const_cast<MWWorld::CellStore*>(hintCell)->searchExact(target.refNum, target.mpNum, target.refId);
            if (!found.isEmpty())
                return found;
        }

        for (MWWorld::CellStore* activeCell : mWorld.getWorldScene().getActiveCells())
        {
            MWWorld::Ptr found = activeCell->searchExact(target.refNum, target.mpNum, target.refId);
            if (!found.isEmpty())
                return found;
        }
        return MWWorld::Ptr();
    }

    bool HeadlessCombatController::handlePlayerAttack(::Player& player, mwmp::Attack& attack,
        HeadlessPlayerAnchorManager& anchors, mwmp::Networking* networking, bool validateCombat)
    {
        if (!validateCombat)
            return true;

        MWWorld::Ptr attackerPtr = anchors.getAnchorPtr(player.getId());
        if (attackerPtr.isEmpty())
            return true;

        MWWorld::Ptr victimPtr
            = resolveTargetPtr(attack.target, attackerPtr.isInCell() ? attackerPtr.getCell() : nullptr, anchors);

        if (!attack.pressed && !attack.isHit)
            return true;

        if (victimPtr.isEmpty() || !victimPtr.getClass().isActor())
        {
            attack.isHit = false;
            attack.damage = 0.f;
            return true;
        }

        MWMechanics::CreatureStats& victimStats = victimPtr.getClass().getCreatureStats(victimPtr);
        if (victimStats.isDead())
        {
            attack.isHit = false;
            attack.damage = 0.f;
            return false;
        }

        std::optional<unsigned short> victimPid = anchors.resolvePidFromPtr(victimPtr);
        if (!victimPid.has_value() && attack.target.isPlayer)
        {
            if (::Player* vp = Players::getPlayer(attack.target.guid))
                victimPid = vp->getId();
        }
        const bool targetIsPlayer = attack.target.isPlayer || victimPid.has_value();

        if (targetIsPlayer)
        {
            const MWWorld::CellStore* targetCell
                = victimPtr.isInCell() ? victimPtr.getCell() : (attackerPtr.isInCell() ? attackerPtr.getCell() : nullptr);
            const osg::Vec3f targetPos = victimPtr.getRefData().getPosition().asVec3();
            if (!isPvpAllowed(targetCell, targetPos))
            {
                Log(Debug::Info) << "[INFO] Blocked PvP attack in PvP-free zone: " << player.npc.mName
                                 << " -> pid=" << (victimPid ? std::to_string(*victimPid) : std::string("unknown"))
                                 << " cell=" << (targetCell ? targetCell->getCell()->getDescription() : "<none>");
                attack.isHit = false;
                attack.success = false;
                attack.damage = 0.f;
                return false;
            }
        }

        MWWorld::InventoryStore& inv = attackerPtr.getClass().getInventoryStore(attackerPtr);
        MWWorld::ContainerStoreIterator weaponIt = inv.getSlot(MWWorld::InventoryStore::Slot_CarriedRight);
        MWWorld::Ptr weapon;
        if (weaponIt != inv.end() && weaponIt->getType() == ESM::Weapon::sRecordId)
            weapon = *weaponIt;

        if (attack.type == mwmp::Attack::MELEE)
        {
            const float reach = MWMechanics::getMeleeWeaponReach(attackerPtr, weapon);
            if (!MWMechanics::isInMeleeReach(attackerPtr, victimPtr, reach))
            {
                Log(Debug::Verbose) << "Authoritative combat rejected out-of-reach melee attack from "
                                    << player.npc.mName;
                attack.isHit = false;
                attack.damage = 0.f;
                return false;
            }
        }

        /*
            Start of tes3mp change

            A ranged attack has already had its full trajectory simulated by the client that fired
            it, which only reports a hit (with the struck target) once the arrow has physically
            reached the victim. Re-running the server's line-of-sight gate on top of that is both
            redundant and fragile: it is evaluated from the server's copy of the attacker anchor and
            victim transforms, which lag behind whenever the player stops feeding position packets
            (LocalPlayer::updatePosition only sends while moving/turning), so a perfectly valid
            arrow that lands while standing still gets rejected. Melee keeps the check because it
            meaningfully validates reach.
        */
        if (attack.type != mwmp::Attack::RANGED)
        {
            const MWPhysics::RayCastingInterface* rayCasting = mWorld.getRayCasting();
            if (rayCasting && !rayCasting->getLineOfSight(attackerPtr, victimPtr))
            {
                const osg::Vec3f from = attackerPtr.getRefData().getPosition().asVec3() + osg::Vec3f(0.f, 0.f, 96.f);
                const osg::Vec3f to = victimPtr.getRefData().getPosition().asVec3() + osg::Vec3f(0.f, 0.f, 64.f);
                const MWPhysics::RayCastingResult los = rayCasting->castRay(from, to, { attackerPtr }, {},
                    MWPhysics::CollisionType_World | MWPhysics::CollisionType_HeightMap);
                if (los.mHit && los.mHitObject != victimPtr)
                {
                    Log(Debug::Verbose) << "Authoritative combat blocked obstructed attack from " << player.npc.mName;
                    attack.isHit = false;
                    attack.damage = 0.f;
                    return false;
                }
            }
        }
        /*
            End of tes3mp change
        */

        const float strength = std::clamp(attack.attackStrength > 0.f ? attack.attackStrength : 0.75f, 0.f, 1.f);
        MWMechanics::applyFatigueLoss(attackerPtr, weapon, strength);

        const ESM::RefId skillId
            = !weapon.isEmpty() ? weapon.getClass().getEquipmentSkill(weapon) : ESM::Skill::HandToHand;
        const float skillValue = attackerPtr.getClass().getSkill(attackerPtr, skillId);
        const float hitChance = MWMechanics::getHitChance(attackerPtr, victimPtr, static_cast<int>(skillValue));
        const bool hitSuccess = Misc::Rng::roll0to99(mWorld.getPrng()) < hitChance;

        attack.success = hitSuccess;
        attack.isHit = hitSuccess;
        if (!hitSuccess)
        {
            attack.damage = 0.f;
            return true;
        }

        float damage = 0.f;
        bool healthDmg = true;
        if (attack.type == mwmp::Attack::RANGED && !weapon.isEmpty())
        {
            /*
                Start of tes3mp change

                Ranged damage is the sum of the bow/crossbow (chop rating of the carried-right weapon)
                and the ammunition (chop rating of the equipped arrow/bolt), matching
                MWMechanics::projectileHit. Previously only the bow was used, so every server-authoritative
                arrow was missing the arrow's contribution to damage.
            */
            const auto& bowData = weapon.get<ESM::Weapon>()->mBase->mData.mChop;
            damage = bowData[0] + (bowData[1] - bowData[0]) * strength;

            MWWorld::Ptr ammo;
            MWWorld::ContainerStoreIterator ammoIt = inv.getSlot(MWWorld::InventoryStore::Slot_Ammunition);
            if (ammoIt != inv.end() && ammoIt->getType() == ESM::Weapon::sRecordId)
            {
                ammo = *ammoIt;
                const auto& ammoData = ammo.get<ESM::Weapon>()->mBase->mData.mChop;
                damage += ammoData[0] + (ammoData[1] - ammoData[0]) * strength;
            }

            MWMechanics::adjustWeaponDamage(damage, weapon, attackerPtr);
            // Match projectileHit: normal-weapon resistance is evaluated against the projectile (arrow).
            MWMechanics::resistNormalWeapon(victimPtr, attackerPtr, ammo.isEmpty() ? weapon : ammo, damage);
            /*
                End of tes3mp change
            */
        }
        else if (!weapon.isEmpty())
        {
            const auto& weapData = weapon.get<ESM::Weapon>()->mBase->mData;
            const unsigned char* atkData = (attack.attackAnimation == "slash")
                ? weapData.mSlash.data()
                : (attack.attackAnimation == "thrust") ? weapData.mThrust.data() : weapData.mChop.data();
            damage = atkData[0] + (atkData[1] - atkData[0]) * strength;
            MWMechanics::adjustWeaponDamage(damage, weapon, attackerPtr);
            MWMechanics::resistNormalWeapon(victimPtr, attackerPtr, weapon, damage);
        }
        else
        {
            MWMechanics::getHandToHandDamage(attackerPtr, victimPtr, damage, healthDmg, strength);
        }

        const float armor = std::max(0.f, victimPtr.getClass().getArmorRating(victimPtr, false));
        if (armor > 0.f && damage > 0.f)
        {
            const float reduction = damage / (damage + armor);
            damage *= std::max(0.25f, reduction);
        }

        // Ranged attacks are not blockable in MWMechanics::projectileHit, so neither are they here.
        if (attack.type == mwmp::Attack::MELEE
            && MWMechanics::blockMeleeAttack(attackerPtr, victimPtr, weapon, damage, strength))
        {
            attack.block = true;
            damage = 0.f;
        }

        attack.damage = damage;
        const bool wasDead = victimStats.isDead();
        const float hpBefore = victimStats.getHealth().getCurrent();
        const ESM::RefId weaponId = !weapon.isEmpty() ? weapon.getCellRef().getRefId() : ESM::RefId();
        // Pass an empty attacker for PvP hits so Npc::onHit applies damage without NPC crime/bounty/arrest triggers.
        victimPtr.getClass().onHit(victimPtr, { { healthDmg ? "health" : "fatigue", damage } }, weaponId,
            targetIsPlayer ? MWWorld::Ptr() : attackerPtr, true,
            attack.type == mwmp::Attack::RANGED ? MWMechanics::DamageSourceType::Ranged
                                                : MWMechanics::DamageSourceType::Melee);
        if (targetIsPlayer)
        {
            /*
                Start of tes3mp change

                The attacker was deliberately suppressed above so a PvP hit never triggers NPC crime. That also
                skips Npc::onHit's own aggressor bookkeeping, so record it here (we already passed the PvP zone
                check) to let the attacker's summoned creatures join the fight through Actors::engageCombat().
            */
            attackerPtr.getClass().getCreatureStats(attackerPtr).setHitAttemptActor(victimPtr.getCellRef().getRefNum());
        }
        if (!targetIsPlayer)
        {
            const MWMechanics::CreatureStats& aStats = attackerPtr.getClass().getCreatureStats(attackerPtr);
            const bool hasPhys
                = mWorld.getPhysicsSystem() && mWorld.getPhysicsSystem()->getActor(attackerPtr) != nullptr;
            Log(Debug::Info) << "Authoritative melee hit: " << player.npc.mName << " -> "
                             << victimPtr.getCellRef().getRefId().getRefIdString()
                             << " dmg=" << damage << " hp=" << hpBefore << "->"
                             << victimStats.getHealth().getCurrent()
                             << " inCombat=" << (victimStats.getAiSequence().isInCombat() ? "true" : "false")
                             << " anchorDead=" << aStats.isDead() << " anchorHP=" << aStats.getHealth().getCurrent()
                             << " hasPhys=" << hasPhys;
        }
        else
        {
            Log(Debug::Info) << "[INFO] Authoritative PvP hit: " << player.npc.mName
                             << " -> pid=" << (victimPid ? std::to_string(*victimPid) : std::string("unknown"))
                             << " dmg=" << damage << " hp=" << hpBefore << "->"
                             << victimStats.getHealth().getCurrent();
        }

        if (targetIsPlayer && networking)
        {
            ::Player* victimPlayer = attack.target.isPlayer ? Players::getPlayer(attack.target.guid) : nullptr;
            if (!victimPlayer && victimPid.has_value())
                victimPlayer = Players::getPlayer(*victimPid);
            if (victimPlayer)
            {
                victimStats.getDynamic(0).writeState(victimPlayer->creatureStats.mDynamic[0]);
                victimStats.getDynamic(2).writeState(victimPlayer->creatureStats.mDynamic[2]);
                mwmp::PlayerPacket* statsPkt
                    = networking->getPlayerPacketController()->GetPacket(ID_PLAYER_STATS_DYNAMIC);
                statsPkt->setPlayer(victimPlayer);
                statsPkt->Send(victimPlayer->guid);
                statsPkt->Send(true);

                if (!wasDead && victimStats.isDead())
                {
                    victimPlayer->creatureStats.mDead = true;
                    victimPlayer->deathState = 1;
                    victimPlayer->killer.isPlayer = true;
                    victimPlayer->killer.guid = player.guid;
                    mwmp::PlayerPacket* deathPkt
                        = networking->getPlayerPacketController()->GetPacket(ID_PLAYER_DEATH);
                    deathPkt->setPlayer(victimPlayer);
                    deathPkt->Send(victimPlayer->guid);
                    deathPkt->Send(true);
                    Script::Call<Script::CallbackIdentity("OnPlayerDeath")>(victimPlayer->getId());
                }
            }
        }

        return true;
    }

    bool HeadlessCombatController::hasLastHealth(unsigned short pid) const
    {
        return mAnchorLastHealth.contains(pid);
    }

    float HeadlessCombatController::getLastHealth(unsigned short pid) const
    {
        auto it = mAnchorLastHealth.find(pid);
        return it != mAnchorLastHealth.end() ? it->second : 0.f;
    }

    void HeadlessCombatController::setLastHealth(unsigned short pid, float health)
    {
        mAnchorLastHealth[pid] = health;
    }

    void HeadlessCombatController::clearPlayerHealth(unsigned short pid)
    {
        mAnchorLastHealth.erase(pid);
    }

    void HeadlessCombatController::clear()
    {
        mAnchorLastHealth.clear();
    }

    bool HeadlessCombatController::dispatchAnchorHealthAndDeath(::Player& player, MWWorld::Ptr& anchor,
        MWMechanics::CreatureStats& anchorStats, float prevHealth, mwmp::Networking* networking,
        const HeadlessPlayerAnchorManager& anchors)
    {
        const unsigned short pid = player.getId();
        const float anchorHealth = anchorStats.getHealth().getCurrent();
        const bool wasDead = player.creatureStats.mDynamic[0].mCurrent <= 0.f;
        anchorStats.getDynamic(0).writeState(player.creatureStats.mDynamic[0]);
        if (networking)
        {
            if (mwmp::PlayerPacket* statsPkt
                = networking->getPlayerPacketController()->GetPacket(ID_PLAYER_STATS_DYNAMIC))
            {
                statsPkt->setPlayer(&player);
                statsPkt->Send(player.guid);
                statsPkt->Send(true);
                Log(Debug::Info) << "[HeadlessServer] Authoritative hit landed on anchor pid=" << pid
                                 << " health=" << anchorHealth << " hpDelta=" << (anchorHealth - prevHealth)
                                 << "; sent ID_PLAYER_STATS_DYNAMIC";
            }
        }
        if (!wasDead && anchorHealth <= 0.f)
        {
            player.creatureStats.mDead = true;
            player.deathState = 1;
            player.killer = {};

            /*
                Start of tes3mp change

                Attribute the kill to the actor that last damaged this player anchor. Previously the killer
                was always left empty, so ID_PLAYER_DEATH (and the server scripts handling it) reported an
                NPC/creature kill as a suicide. The anchor keeps its aggressor in
                CreatureStats::mHitAttemptActor (preserved by Actors::update while the anchor is dead) until
                the kill is consumed here.
            */
            const ESM::RefNum killerNum = anchorStats.getHitAttemptActor();
            if (killerNum.isSet())
            {
                const MWWorld::Ptr killer = MWBase::Environment::get().getWorldModel()->getPtr(killerNum);
                if (!killer.isEmpty() && killer.getClass().isActor() && killer != anchor)
                {
                    if (const std::optional<unsigned short> killerPid = anchors.resolvePidFromPtr(killer))
                    {
                        if (::Player* killerPlayer = Players::getPlayer(*killerPid))
                        {
                            player.killer.isPlayer = true;
                            player.killer.guid = killerPlayer->guid;
                        }
                    }
                    else
                    {
                        player.killer.isPlayer = false;
                        player.killer.refId = killer.getCellRef().getRefId().serializeText();
                        player.killer.refNum = killer.getCellRef().getRefNum().mIndex;
                        player.killer.mpNum = killer.getCellRef().getMpNum();
                        player.killer.name = killer.getClass().getName(killer);
                    }
                }
            }
            /*
                End of tes3mp change
            */

            if (auto* winMgr
                = dynamic_cast<HeadlessWindowManager*>(&*MWBase::Environment::get().getWindowManager()))
                winMgr->setPlayerDialogueOpen(pid, false);
            disengageActorsTargeting(anchor, false);
            if (networking)
            {
                if (mwmp::PlayerPacket* deathPkt
                    = networking->getPlayerPacketController()->GetPacket(ID_PLAYER_DEATH))
                {
                    deathPkt->setPlayer(&player);
                    deathPkt->Send(player.guid);
                    deathPkt->Send(true);
                }
            }
            Script::Call<Script::CallbackIdentity("OnPlayerDeath")>(pid);
            // Only drop the kill attribution once the death report for this kill has been emitted, so a
            // late duplicate dispatch can never report the kill as a suicide.
            anchorStats.setHitAttemptActor({});
            return true;
        }
        return false;
    }

    void HeadlessCombatController::disengageActorsTargeting(const MWWorld::Ptr& anchor, bool guardsOnly)
    {
        if (anchor.isEmpty())
            return;

        int count = 0;
        for (MWWorld::CellStore* cellStore : mWorld.getWorldScene().getActiveCells())
        {
            if (!cellStore)
                continue;
            cellStore->forEach([&](const MWWorld::Ptr& ptr) {
                if (ptr.isEmpty() || !ptr.getClass().isActor() || ptr == anchor)
                    return true;
                if (guardsOnly && !ptr.getClass().isClass(ptr, "guard"))
                    return true;
                MWMechanics::CreatureStats& stats = ptr.getClass().getCreatureStats(ptr);
                MWMechanics::AiSequence& seq = stats.getAiSequence();
                bool disengaged = false;
                if (seq.isInPursuit())
                {
                    seq.stopPursuit();
                    disengaged = true;
                }
                while (seq.isInCombat(anchor))
                {
                    seq.stopCombat();
                    MWBase::Environment::get().getMechanicsManager()->stopCombat(ptr);
                    disengaged = true;
                }
                if (disengaged)
                {
                    stats.setDrawState(MWMechanics::DrawState::Nothing);
                    stats.setAttackingOrSpell(false);
                    auto& move = ptr.getClass().getMovementSettings(ptr);
                    move.mPosition[0] = 0.f;
                    move.mPosition[1] = 0.f;
                    ++count;
                }
                return true;
            });
        }
        if (count > 0)
        {
            Log(Debug::Info) << "[HeadlessServer] Disengaged " << count << " actor(s) targeting anchor (guardsOnly="
                             << guardsOnly << ")";
        }
    }

    void HeadlessCombatController::calmGuardsForAnchor(const MWWorld::Ptr& anchor)
    {
        disengageActorsTargeting(anchor, true);
    }
}
