#include <apps/openmw/mwmp/CellController.hpp>
#include <apps/openmw/mwmp/DedicatedActor.hpp>
#include <apps/openmw/mwmp/GUIController.hpp>
#include <apps/openmw/mwmp/LocalActor.hpp>
#include <apps/openmw/mwmp/LocalPlayer.hpp>
#include <apps/openmw/mwmp/LocalSystem.hpp>
#include <apps/openmw/mwmp/Main.hpp>
#include <apps/openmw/mwmp/MechanicsHelper.hpp>
#include <apps/openmw/mwmp/Networking.hpp>
#include <apps/openmw/mwmp/ObjectList.hpp>
#include <apps/openmw/mwmp/PlayerList.hpp>
#include <apps/openmw/mwmp/RecordHelper.hpp>
#include <apps/openmw/mwmp/Worldstate.hpp>

#include <apps/openmw/mwmechanics/spellcasting.hpp>
#include <apps/openmw/mwworld/cellstore.hpp>

// Complete stub for forward-declared GUIChat inside mwmp::GUIController.
namespace mwmp
{
    class GUIChat
    {
    };
}

namespace mwmp
{
    // ---- Cell / Actors / Players / Lists ----

    Cell::Cell(MWWorld::CellStore* /*cellStore*/) {}
    Cell::~Cell() = default;
    LocalActor* Cell::getLocalActor(std::string /*actorIndex*/) { return nullptr; }
    DedicatedActor* Cell::getDedicatedActor(std::string /*actorIndex*/) { return nullptr; }

    LocalSystem::LocalSystem() = default;
    LocalSystem::~LocalSystem() = default;

    LocalActor::~LocalActor() = default;
    void LocalActor::setPtr(const MWWorld::Ptr& /*ptr*/) {}
    void LocalActor::sendDeath(char /*newDeathState*/) {}
    void LocalActor::sendSpellsActiveAddition(
        const std::string /*id*/, bool /*isStackingSpell*/, const MWMechanics::ActiveSpells::ActiveSpellParams& /*params*/)
    {
    }
    void LocalActor::sendSpellsActiveRemoval(
        const std::string /*id*/, bool /*isStackingSpell*/, MWWorld::TimeStamp /*timestamp*/)
    {
    }

    DedicatedActor::~DedicatedActor() = default;
    void DedicatedActor::setPtr(const MWWorld::Ptr& /*ptr*/) {}

    DedicatedPlayer::~DedicatedPlayer() = default;
    void DedicatedPlayer::setPtr(const MWWorld::Ptr& /*ptr*/) {}

    ActorList::ActorList() = default;
    ActorList::~ActorList() = default;
    void ActorList::reset() {}
    void ActorList::addAiActor(BaseActor /*baseActor*/) {}
    void ActorList::addAttackActor(const MWWorld::Ptr& /*actorPtr*/, const mwmp::Attack& /*attack*/) {}
    void ActorList::addCellChangeActor(BaseActor /*baseActor*/) {}
    void ActorList::sendAiActors() {}
    void ActorList::sendAttackActors() {}
    void ActorList::sendCellChangeActors() {}

    CellController::CellController() = default;
    CellController::~CellController() = default;

    bool CellController::hasQueuedDeathState(MWWorld::Ptr /*ptr*/) { return false; }
    unsigned int CellController::getQueuedDeathState(MWWorld::Ptr /*ptr*/) { return 0; }
    void CellController::clearQueuedDeathState(MWWorld::Ptr /*ptr*/) {}

    bool CellController::isLocalActor(MWWorld::Ptr /*ptr*/) { return false; }
    bool CellController::isLocalActor(int /*refNum*/, int /*mpNum*/) { return false; }
    LocalActor* CellController::getLocalActor(MWWorld::Ptr /*ptr*/) { return nullptr; }
    LocalActor* CellController::getLocalActor(int /*refNum*/, int /*mpNum*/) { return nullptr; }

    bool CellController::isDedicatedActor(MWWorld::Ptr /*ptr*/) { return false; }
    bool CellController::isDedicatedActor(int /*refNum*/, int /*mpNum*/) { return false; }
    DedicatedActor* CellController::getDedicatedActor(MWWorld::Ptr /*ptr*/) { return nullptr; }
    DedicatedActor* CellController::getDedicatedActor(int /*refNum*/, int /*mpNum*/) { return nullptr; }

    bool CellController::hasLocalAuthority(const ESM::Cell& /*cell*/) { return true; }
    bool CellController::isInitializedCell(const ESM::Cell& /*cell*/) { return true; }
    Cell* CellController::getCell(const ESM::Cell& /*cell*/)
    {
        static Cell dummyCell(nullptr);
        return &dummyCell;
    }
    MWWorld::CellStore* CellController::getCellStore(const ESM::Cell& /*cell*/) { return nullptr; }
    bool CellController::isSameCell(const ESM::Cell& a, const ESM::Cell& b)
    {
        return a.mId == b.mId;
    }

    LocalPlayer::LocalPlayer() = default;
    LocalPlayer::~LocalPlayer() = default;
    bool LocalPlayer::isLoggedIn() { return false; }
    void LocalPlayer::closeInventoryWindows() {}
    void LocalPlayer::updateInventoryWindow() {}
    void LocalPlayer::sendDeath(char /*newDeathState*/) {}
    void LocalPlayer::sendClass() {}
    void LocalPlayer::sendInventory() {}
    void LocalPlayer::sendItemChange(const mwmp::Item& /*item*/, unsigned int /*action*/) {}
    void LocalPlayer::sendItemChange(const MWWorld::Ptr& /*itemPtr*/, int /*count*/, unsigned int /*action*/) {}
    void LocalPlayer::sendItemChange(const std::string& /*refId*/, int /*count*/, unsigned int /*action*/) {}
    void LocalPlayer::sendStoredItemRemovals() {}
    void LocalPlayer::sendSpellbook() {}
    void LocalPlayer::sendSpellChange(std::string /*id*/, unsigned int /*action*/) {}
    void LocalPlayer::sendSpellsActive() {}
    void LocalPlayer::sendSpellsActiveAddition(
        const std::string /*id*/, bool /*isStackingSpell*/, const MWMechanics::ActiveSpells::ActiveSpellParams& /*params*/)
    {
    }
    void LocalPlayer::sendSpellsActiveRemoval(
        const std::string /*id*/, bool /*isStackingSpell*/, MWWorld::TimeStamp /*timestamp*/)
    {
    }
    void LocalPlayer::sendCooldownChange(
        std::string /*id*/, int /*startTimestampDay*/, float /*startTimestampHour*/)
    {
    }
    void LocalPlayer::sendQuickKey(unsigned short /*slot*/, int /*type*/, const std::string& /*itemId*/) {}
    void LocalPlayer::sendJournalEntry(const std::string& /*quest*/, int /*index*/, const MWWorld::Ptr& /*actorPtr*/) {}
    void LocalPlayer::sendJournalIndex(const std::string& /*quest*/, int /*index*/) {}
    void LocalPlayer::sendFactionRank(const std::string& /*factionId*/, int /*rank*/) {}
    void LocalPlayer::sendFactionExpulsionState(const std::string& /*factionId*/, bool /*isExpelled*/) {}
    void LocalPlayer::sendFactionReputation(const std::string& /*factionId*/, int /*reputation*/) {}
    void LocalPlayer::sendTopic(const std::string& /*topicId*/) {}
    void LocalPlayer::sendBook(const std::string& /*bookId*/) {}
    void LocalPlayer::sendWerewolfState(bool /*isWerewolf*/) {}
    void LocalPlayer::sendMarkLocation(const ESM::Cell& /*newMarkCell*/, const ESM::Position& /*newMarkPosition*/) {}
    void LocalPlayer::sendSelectedSpell(const std::string& /*newSelectedSpellId*/) {}
    void LocalPlayer::sendItemUse(const MWWorld::Ptr& /*itemPtr*/, bool /*usingItemMagic*/, char /*currentDrawState*/) {}
    void LocalPlayer::sendCellStates() {}
    void LocalPlayer::clearCellStates() {}
    void LocalPlayer::storeCellState(const ESM::Cell& /*cell*/, int /*stateType*/) {}
    void LocalPlayer::storeItemRemoval(const std::string& /*refId*/, int /*count*/) {}
    void LocalPlayer::storeLastEnchantmentQuantity(unsigned int /*quantity*/) {}

    ObjectList::ObjectList() = default;
    ObjectList::~ObjectList() = default;
    void ObjectList::reset() {}
    void ObjectList::addBaseObject(BaseObject /*baseObject*/) {}
    mwmp::BaseObject ObjectList::getBaseObjectFromPtr(const MWWorld::Ptr& /*ptr*/) { return {}; }
    void ObjectList::addContainerItem(
        mwmp::BaseObject& /*baseObject*/, const MWWorld::Ptr& /*itemPtr*/, int /*itemCount*/, int /*actionCount*/)
    {
    }
    void ObjectList::addContainerItem(
        mwmp::BaseObject& /*baseObject*/, const MWGui::ItemStack& /*itemStack*/, int /*itemCount*/, int /*actionCount*/)
    {
    }
    void ObjectList::addContainerItem(
        mwmp::BaseObject& /*baseObject*/, const std::string /*itemId*/, int /*itemCount*/, int /*actionCount*/)
    {
    }
    void ObjectList::addEntireContainer(const MWWorld::Ptr& /*ptr*/) {}
    void ObjectList::addObjectGeneric(const MWWorld::Ptr& /*ptr*/) {}
    void ObjectList::addObjectActivate(const MWWorld::Ptr& /*ptr*/, const MWWorld::Ptr& /*activatingActor*/) {}
    void ObjectList::addObjectHit(const MWWorld::Ptr& /*ptr*/, const MWWorld::Ptr& /*hittingActor*/) {}
    void ObjectList::addObjectHit(
        const MWWorld::Ptr& /*ptr*/, const MWWorld::Ptr& /*hittingActor*/, const Attack /*hitAttack*/)
    {
    }
    void ObjectList::addObjectPlace(const MWWorld::Ptr& /*ptr*/, bool /*droppedByPlayer*/) {}
    void ObjectList::addObjectSpawn(const MWWorld::Ptr& /*ptr*/) {}
    void ObjectList::addObjectSpawn(const MWWorld::Ptr& /*ptr*/, const MWWorld::Ptr& /*master*/, std::string /*spellId*/,
        int /*effectId*/, float /*duration*/)
    {
    }
    void ObjectList::addObjectLock(const MWWorld::Ptr& /*ptr*/, int /*lockLevel*/) {}
    void ObjectList::addObjectDialogueChoice(const MWWorld::Ptr& /*ptr*/, std::string /*dialogueChoice*/) {}
    void ObjectList::addObjectMiscellaneous(
        const MWWorld::Ptr& /*ptr*/, unsigned int /*goldPool*/, float /*lastGoldRestockHour*/, int /*lastGoldRestockDay*/)
    {
    }
    void ObjectList::addObjectTrap(
        const MWWorld::Ptr& /*ptr*/, const ESM::Position& /*pos*/, bool /*isDisarmed*/)
    {
    }
    void ObjectList::addObjectScale(const MWWorld::Ptr& /*ptr*/, float /*scale*/) {}
    void ObjectList::addObjectSound(
        const MWWorld::Ptr& /*ptr*/, std::string /*soundId*/, float /*volume*/, float /*pitch*/)
    {
    }
    void ObjectList::addObjectState(const MWWorld::Ptr& /*ptr*/, bool /*objectState*/) {}
    void ObjectList::addObjectAnimPlay(const MWWorld::Ptr& /*ptr*/, std::string /*group*/, int /*mode*/) {}
    void ObjectList::addDoorState(const MWWorld::Ptr& /*ptr*/, MWWorld::DoorState /*state*/) {}
    void ObjectList::addMusicPlay(std::string /*filename*/) {}
    void ObjectList::addVideoPlay(std::string /*filename*/, bool /*allowSkipping*/) {}
    void ObjectList::addClientScriptLocal(
        const MWWorld::Ptr& /*ptr*/, int /*internalIndex*/, int /*value*/, mwmp::VARIABLE_TYPE /*variableType*/)
    {
    }
    void ObjectList::addClientScriptLocal(const MWWorld::Ptr& /*ptr*/, int /*internalIndex*/, float /*value*/) {}
    void ObjectList::addScriptMemberShort(std::string /*refId*/, int /*index*/, int /*shortVal*/) {}

    void ObjectList::sendContainer() {}
    void ObjectList::sendObjectActivate() {}
    void ObjectList::sendObjectHit() {}
    void ObjectList::sendObjectPlace() {}
    void ObjectList::sendObjectSpawn() {}
    void ObjectList::sendObjectDelete() {}
    void ObjectList::sendObjectLock() {}
    void ObjectList::sendObjectDialogueChoice() {}
    void ObjectList::sendObjectMiscellaneous() {}
    void ObjectList::sendObjectRestock() {}
    void ObjectList::sendObjectTrap() {}
    void ObjectList::sendObjectScale() {}
    void ObjectList::sendObjectSound() {}
    void ObjectList::sendObjectState() {}
    void ObjectList::sendObjectAnimPlay() {}
    void ObjectList::sendDoorState() {}
    void ObjectList::sendMusicPlay() {}
    void ObjectList::sendVideoPlay() {}
    void ObjectList::sendClientScriptLocal() {}
    void ObjectList::sendScriptMemberShort() {}

    Worldstate::Worldstate() = default;
    Worldstate::~Worldstate() = default;
    bool Worldstate::containsExploredMapTile(int /*cellX*/, int /*cellY*/) { return false; }
    void Worldstate::markExploredMapTile(int /*cellX*/, int /*cellY*/) {}
    void Worldstate::sendClientGlobal(
        std::string /*varName*/, int /*value*/, mwmp::VARIABLE_TYPE /*variableType*/)
    {
    }
    void Worldstate::sendClientGlobal(std::string /*varName*/, float /*value*/) {}
    void Worldstate::sendMapExplored(int /*cellX*/, int /*cellY*/, const std::vector<char>& /*imageData*/) {}
    void Worldstate::sendWeather(std::string /*region*/, int /*currentWeather*/, int /*nextWeather*/,
        int /*queuedWeather*/, float /*transitionFactor*/)
    {
    }
    void Worldstate::sendEnchantmentRecord(const ESM::Enchantment* /*enchantment*/) {}
    void Worldstate::sendPotionRecord(const ESM::Potion* /*potion*/, unsigned int /*quantity*/) {}
    void Worldstate::sendSpellRecord(const ESM::Spell* /*spell*/) {}
    void Worldstate::sendArmorRecord(const ESM::Armor* /*armor*/, std::string /*baseRefId*/) {}
    void Worldstate::sendBookRecord(const ESM::Book* /*book*/, std::string /*baseRefId*/) {}
    void Worldstate::sendClothingRecord(const ESM::Clothing* /*clothing*/, std::string /*baseRefId*/) {}
    void Worldstate::sendWeaponRecord(
        const ESM::Weapon* /*weapon*/, std::string /*baseRefId*/, unsigned int /*quantity*/)
    {
    }

    GUIController::GUIController() = default;
    GUIController::~GUIController() = default;
    void GUIController::refreshGuiMode(MWGui::GuiMode /*guiMode*/) {}

    Networking::Networking()
        : connected(false)
        , peer(nullptr)
        , systemPacketController(nullptr)
        , playerPacketController(nullptr)
        , actorPacketController(nullptr)
        , objectPacketController(nullptr)
        , worldstatePacketController(nullptr)
    {
    }
    Networking::~Networking() = default;
    bool Networking::isConnected() { return false; }
    LocalSystem* Networking::getLocalSystem() { return Main::get().getLocalSystem(); }
    LocalPlayer* Networking::getLocalPlayer() { return Main::get().getLocalPlayer(); }
    ActorList* Networking::getActorList() { return &actorList; }
    ObjectList* Networking::getObjectList() { return &objectList; }
    Worldstate* Networking::getWorldstate() { return &worldstate; }

    Main* Main::pMain = nullptr;
    std::string Main::address;
    std::string Main::serverPassword;
    std::string Main::resourceDir;

    static LocalPlayer gDummyLocalPlayer;
    static LocalSystem gDummyLocalSystem;
    static GUIController gDummyGuiController;
    static CellController gDummyCellController;
    static Networking gDummyNetworking;

    Main::Main()
        : mNetworking(&gDummyNetworking)
        , mLocalSystem(&gDummyLocalSystem)
        , mLocalPlayer(&gDummyLocalPlayer)
        , mGUIController(&gDummyGuiController)
        , mCellController(&gDummyCellController)
        , port(25565)
    {
    }
    Main::~Main() = default;

    bool Main::isInitialized() { return false; }

    const Main& Main::get()
    {
        static Main instance;
        return instance;
    }

    bool Main::isValidPacketScript(std::string /*scriptId*/) { return false; }
    bool Main::isValidPacketGlobal(std::string /*globalId*/) { return false; }
    Networking* Main::getNetworking() const { return &gDummyNetworking; }
    LocalSystem* Main::getLocalSystem() const { return &gDummyLocalSystem; }
    LocalPlayer* Main::getLocalPlayer() const { return &gDummyLocalPlayer; }
    GUIController* Main::getGUIController() const { return &gDummyGuiController; }
    CellController* Main::getCellController() const { return &gDummyCellController; }

    bool PlayerList::isDedicatedPlayer(const MWWorld::Ptr& /*ptr*/) { return false; }
    DedicatedPlayer* PlayerList::getPlayer(const MWWorld::Ptr& /*ptr*/) { return nullptr; }
    void PlayerList::clearHitAttemptActorId(ESM::RefNum /*refNum*/) {}
}

namespace RecordHelper
{
    const std::string getPlaceholderInteriorCellName()
    {
        return "0, 0";
    }
}

namespace MechanicsHelper
{
    ESM::Position getPositionFromVector(osg::Vec3f vector)
    {
        ESM::Position pos{};
        pos.pos[0] = vector.x();
        pos.pos[1] = vector.y();
        pos.pos[2] = vector.z();
        return pos;
    }

    mwmp::Item getItem(const MWWorld::Ptr& /*itemPtr*/, int /*count*/)
    {
        return {};
    }

    mwmp::Target getTarget(const MWWorld::Ptr& /*ptr*/)
    {
        return {};
    }
    void spawnLeveledCreatures(MWWorld::CellStore* /*cellStore*/) {}
    bool isUsingRangedWeapon(const MWWorld::Ptr& /*ptr*/) { return false; }
    mwmp::Attack* getLocalAttack(const MWWorld::Ptr& /*ptr*/) { return nullptr; }
    mwmp::Attack* getDedicatedAttack(const MWWorld::Ptr& /*ptr*/) { return nullptr; }
    mwmp::Cast* getLocalCast(const MWWorld::Ptr& /*ptr*/) { return nullptr; }
    mwmp::Cast* getDedicatedCast(const MWWorld::Ptr& /*ptr*/) { return nullptr; }
    void assignAttackTarget(mwmp::Attack* /*attack*/, const MWWorld::Ptr& /*target*/) {}
    void resetAttack(mwmp::Attack* /*attack*/) {}
    void resetCast(mwmp::Cast* /*cast*/) {}
    bool isTeamMember(const MWWorld::Ptr& /*playerChecked*/, const MWWorld::Ptr& /*playerWithTeam*/)
    {
        return false;
    }
    bool getSpellSuccess(std::string /*spellId*/, const MWWorld::Ptr& /*caster*/) { return true; }
}
