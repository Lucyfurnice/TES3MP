#ifndef OPENMW_MECHANICS_SUMMONING_H
#define OPENMW_MECHANICS_SUMMONING_H

#include <functional>
#include <string_view>
#include <utility>

#include <components/esm/refid.hpp>
#include <components/esm3/refnum.hpp>

namespace MWWorld
{
    class Ptr;
}

namespace MWMechanics
{
    bool isSummoningEffect(ESM::RefId effectId);

    ESM::RefId getSummonedCreature(ESM::RefId effectId);

    void purgeSummonEffect(const MWWorld::Ptr& summoner, const std::pair<ESM::RefId, ESM::RefNum>& summon);

    ESM::RefNum summonCreature(ESM::RefId effectId, const MWWorld::Ptr& summoner, ESM::RefId sourceId = {});

    void updateSummons(const MWWorld::Ptr& summoner, bool cleanup);

#ifdef TES3MP_HEADLESS
    using HeadlessSummonCallback = std::function<void(
        const MWWorld::Ptr& placed, const MWWorld::Ptr& summoner, ESM::RefId effectId, ESM::RefId sourceId)>;
    using HeadlessUnsummonCallback = std::function<void(const MWWorld::Ptr& ptr)>;
    void setHeadlessSummonCallback(HeadlessSummonCallback cb);
    void setHeadlessUnsummonCallback(HeadlessUnsummonCallback cb);
    void notifyHeadlessUnsummon(const MWWorld::Ptr& ptr);
#endif
}

#endif
