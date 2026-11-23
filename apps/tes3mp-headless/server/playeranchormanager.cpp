#include "playeranchormanager.hpp"

#include <apps/openmw/mwbase/environment.hpp>
#include <apps/openmw/mwbase/mechanicsmanager.hpp>
#include <apps/openmw/mwmechanics/aisequence.hpp>
#include <apps/openmw/mwmechanics/creaturestats.hpp>
#include <apps/openmw/mwphysics/physicssystem.hpp>
#include <apps/openmw/mwworld/cellstore.hpp>
#include <apps/openmw/mwworld/class.hpp>
#include <apps/openmw/mwworld/manualref.hpp>
#include <apps/openmw/mwworld/worldimp.hpp>
#include <apps/openmw/mwworld/worldmodel.hpp>
#include <components/debug/debuglog.hpp>
#include <components/esm/esmbridge.hpp>

namespace TES3MPHeadless
{
    HeadlessPlayerAnchorManager::HeadlessPlayerAnchorManager(MWWorld::World& world)
        : mWorld(world)
    {
    }

    MWWorld::CellStore* HeadlessPlayerAnchorManager::resolveCellStore(MWWorld::World& world, const ESM::Cell& cell)
    {
        MWWorld::WorldModel& worldModel = world.getWorldModel();
        if (cell.isExterior())
        {
            return &worldModel.getExterior(
                ESM::ExteriorCellLocation(cell.mData.mX, cell.mData.mY, ESM::Cell::sDefaultWorldspaceId), true);
        }
        return worldModel.findInterior(cell.mName, true);
    }

    MWWorld::Ptr HeadlessPlayerAnchorManager::getAnchorPtr(unsigned short pid) const
    {
        auto it = mPlayerAnchors.find(pid);
        return it != mPlayerAnchors.end() ? it->second : MWWorld::Ptr();
    }

    std::optional<unsigned short> HeadlessPlayerAnchorManager::resolvePidFromPtr(const MWWorld::Ptr& ptr) const
    {
        if (ptr.isEmpty())
            return std::nullopt;
        for (const auto& [pid, anchor] : mPlayerAnchors)
        {
            if (anchor == ptr)
                return pid;
        }
        return std::nullopt;
    }

    bool HeadlessPlayerAnchorManager::isPlayerAnchorPtr(const MWWorld::Ptr& ptr) const
    {
        if (ptr == mWorld.getPlayerPtr())
            return true;
        return resolvePidFromPtr(ptr).has_value();
    }

    void HeadlessPlayerAnchorManager::ensurePhysicsActor(const MWWorld::Ptr& anchor)
    {
        if (anchor.isEmpty())
            return;
        if (MWPhysics::PhysicsSystem* phys = mWorld.getPhysicsSystem())
        {
            if (!phys->getActor(anchor))
                phys->addActor(anchor, anchor.getClass().getCorrectedModel(anchor));
        }
    }

    MWWorld::Ptr HeadlessPlayerAnchorManager::spawnOrMoveAnchor(
        unsigned short pid, MWWorld::CellStore* targetCell, const ESM::Position& pos)
    {
        if (!targetCell)
            return MWWorld::Ptr();

        auto it = mPlayerAnchors.find(pid);
        if (it == mPlayerAnchors.end() || it->second.isEmpty())
        {
            MWWorld::ManualRef ref(mWorld.getStore(), mWorld.getPlayerPtr(), 1);
            MWWorld::Ptr anchor = mWorld.placeObject(ref.getPtr(), targetCell, pos);
            if (!anchor.isEmpty())
            {
                mWorld.enable(anchor);
                ensurePhysicsActor(anchor);
                anchor.getClass().getCreatureStats(anchor).getAiSequence().clear();
                mPlayerAnchors[pid] = anchor;
            }
            return anchor;
        }

        MWWorld::Ptr anchor = it->second;
        if (anchor.getCell() != targetCell
            || (anchor.getRefData().getPosition().asVec3() - pos.asVec3()).length2() > 0.0001f)
        {
            const bool cellChanged = (anchor.getCell() != targetCell);
            anchor = mWorld.moveObject(anchor, targetCell, pos.asVec3());
            it->second = anchor;
            if (cellChanged)
                ensurePhysicsActor(anchor);
        }
        mWorld.rotateObject(anchor, osg::Vec3f(pos.rot[0], pos.rot[1], pos.rot[2]));
        return anchor;
    }

    void HeadlessPlayerAnchorManager::resurrectAnchor(MWWorld::Ptr& anchor, std::string_view playerName)
    {
        if (anchor.isEmpty() || !anchor.getClass().isActor())
            return;

        MWBase::Environment::get().getMechanicsManager()->resurrect(anchor);
        anchor.getClass().getCreatureStats(anchor).getAiSequence().clear();
        ensurePhysicsActor(anchor);
        Log(Debug::Info) << "[HeadlessServer] Resurrected player anchor for player=" << playerName;
    }

    void HeadlessPlayerAnchorManager::removeAnchor(unsigned short pid)
    {
        auto it = mPlayerAnchors.find(pid);
        if (it != mPlayerAnchors.end())
        {
            if (!it->second.isEmpty())
            {
                if (MWPhysics::PhysicsSystem* phys = mWorld.getPhysicsSystem())
                    phys->remove(it->second);
                mWorld.deleteObject(it->second);
            }
            mPlayerAnchors.erase(it);
        }
    }

    void HeadlessPlayerAnchorManager::clear()
    {
        for (auto& [pid, ptr] : mPlayerAnchors)
        {
            if (!ptr.isEmpty())
            {
                if (MWPhysics::PhysicsSystem* phys = mWorld.getPhysicsSystem())
                    phys->remove(ptr);
                mWorld.deleteObject(ptr);
            }
        }
        mPlayerAnchors.clear();
    }
}
