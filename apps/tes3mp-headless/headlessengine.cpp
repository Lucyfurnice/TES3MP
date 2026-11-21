#include "headlessengine.hpp"

#include "server/headlessserver.hpp"
#include "stubs/headlessinputmanager.hpp"
#include "stubs/headlesssoundmanager.hpp"
#include "stubs/headlesswindowmanager.hpp"

#include <SDL.h>

#include <components/compiler/extensions0.hpp>
#include <components/debug/debuglog.hpp>
#include <components/esm/esmbridge.hpp>
#include <components/fallback/fallback.hpp>
#include <components/l10n/manager.hpp>
#include <components/misc/rng.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/resource/scenemanager.hpp>
#include <components/sceneutil/unrefqueue.hpp>
#include <components/sceneutil/workqueue.hpp>
#include <components/settings/values.hpp>
#include <components/version/version.hpp>
#include <components/vfs/manager.hpp>
#include <components/vfs/registerarchives.hpp>

#include <apps/openmw/mwclass/classes.hpp>
#include <apps/openmw/mwdialogue/dialoguemanagerimp.hpp>
#include <apps/openmw/mwdialogue/journalimp.hpp>
#include <apps/openmw/mwlua/luamanagerimp.hpp>
#include <apps/openmw/mwlua/worker.hpp>
#include <apps/openmw/mwmechanics/mechanicsmanagerimp.hpp>
#include <apps/openmw/mwscript/compilercontext.hpp>
#include <apps/openmw/mwscript/extensions.hpp>
#include <apps/openmw/mwscript/interpretercontext.hpp>
#include <apps/openmw/mwscript/scriptmanagerimp.hpp>
#include <apps/openmw/mwstate/statemanagerimp.hpp>
#include <apps/openmw/mwworld/datetimemanager.hpp>
#include <apps/openmw/mwworld/esmstore.hpp>
#include <apps/openmw/mwworld/localscripts.hpp>
#include <apps/openmw/mwworld/scene.hpp>
#include <apps/openmw/mwworld/worldimp.hpp>

#include <chrono>
#include <stdexcept>
#include <thread>

namespace TES3MPHeadless
{
    HeadlessEngine::HeadlessEngine(Files::ConfigurationManager& cfgMgr, HeadlessConfig config)
        : mCfgMgr(cfgMgr)
        , mConfig(std::move(config))
        , mStats(new osg::Stats("HeadlessStats"))
        , mTimer(osg::Timer::instance())
    {
        if (!mConfig.mResourceDir.empty())
            mConfig.mDataDirs.insert(mConfig.mDataDirs.begin(), mConfig.mResourceDir / "vfs");
        mFileCollections = Files::Collections(mConfig.mDataDirs);
    }

    HeadlessEngine::~HeadlessEngine()
    {
        if (mServer)
            mServer->shutdown();
        mServer.reset();

        if (mLuaWorker)
            mLuaWorker->join();

        mMechanicsManager.reset();
        mDialogueManager.reset();
        mJournal.reset();
        mWindowManager.reset();
        mScriptManager.reset();
        mWorld.reset();
        mSoundManager.reset();
        mInputManager.reset();
        mStateManager.reset();

        mLuaWorker.reset();
        mLuaManager.reset();
        mL10nManager.reset();
        mScriptContext.reset();

        mUnrefQueue.reset();
        mWorkQueue = nullptr;
        mResourceSystem.reset();
        mVFS.reset();
        mEncoder.reset();

        if (mSdlInitialized)
            SDL_Quit();

        Log(Debug::Info) << "Headless simulation shut down cleanly.";
    }

    void HeadlessEngine::init()
    {
        if (mConfig.mContentFiles.empty())
            throw std::runtime_error("No content files specified (pass --content <file.esm/esp/omwscripts>)");

        // Initialize SDL without video/audio devices: only timer & event subsystems are needed.
        if (SDL_Init(SDL_INIT_TIMER | SDL_INIT_EVENTS) != 0)
            throw std::runtime_error(std::string("Failed to initialize headless SDL subsystems: ") + SDL_GetError());
        mSdlInitialized = true;

        Misc::Rng::init(mConfig.mRandomSeed);
        MWClass::registerClasses();
        mEncoder = std::make_unique<ToUTF8::Utf8Encoder>(mConfig.mEncoding);

        initManagers();
        loadContentAndWorld();

        HeadlessServerConfig serverCfg;
        serverCfg.mServerCfgPath = mConfig.mServerCfgPath;
        serverCfg.mPortOverride = mConfig.mPort;
        serverCfg.mHasPortOverride = mConfig.mHasPortOverride;
        mServer = std::make_unique<HeadlessServer>(mCfgMgr, *mWorld, std::move(serverCfg));
        mServer->init();
    }

    void HeadlessEngine::initManagers()
    {
        mStateManager
            = std::make_unique<MWState::StateManager>(mCfgMgr.getUserDataPath() / "saves", mConfig.mContentFiles);
        mEnvironment.setStateManager(*mStateManager);

        mVFS = std::make_unique<VFS::Manager>();
        VFS::registerArchives(
            mVFS.get(), mFileCollections, mConfig.mArchives, true, &mEncoder->getStatelessEncoder());

        // Headless resource system: parses OSG models / keyframe text keys / NIF geometry without GPU uploads.
        mResourceSystem = std::make_unique<Resource::ResourceSystem>(
            mVFS.get(), Settings::cells().mCacheExpiryDelay, &mEncoder->getStatelessEncoder());
        mEnvironment.setResourceSystem(*mResourceSystem);

        mWorkQueue = new SceneUtil::WorkQueue(mConfig.mPreloadThreads);
        mUnrefQueue = std::make_unique<SceneUtil::UnrefQueue>();

        mL10nManager = std::make_unique<L10n::Manager>(mVFS.get());
        mL10nManager->setPreferredLocales(
            Settings::general().mPreferredLocales, Settings::general().mGmstOverridesL10n);
        mEnvironment.setL10nManager(*mL10nManager);

        const std::filesystem::path luaLibsDir
            = mConfig.mResourceDir.empty() ? std::filesystem::path() : (mConfig.mResourceDir / "lua_libs");
        mLuaManager = std::make_unique<MWLua::LuaManager>(mVFS.get(), luaLibsDir);
        mEnvironment.setLuaManager(*mLuaManager);

        mWindowManager = std::make_unique<HeadlessWindowManager>(
            mTranslationDataStorage, Version::getOpenmwVersionDescription());
        mEnvironment.setWindowManager(*mWindowManager);

        mInputManager = std::make_unique<HeadlessInputManager>();
        mEnvironment.setInputManager(*mInputManager);

        mSoundManager = std::make_unique<HeadlessSoundManager>();
        mEnvironment.setSoundManager(*mSoundManager);

        mWorld = std::make_unique<MWWorld::World>(
            mResourceSystem.get(), 0, mConfig.mStartCell, mCfgMgr.getUserDataPath());
        mEnvironment.setWorld(*mWorld);
        mEnvironment.setWorldModel(mWorld->getWorldModel());
        mEnvironment.setESMStore(mWorld->getStore());

        const MWWorld::Store<ESM::GameSetting>* gmst = &mWorld->getStore().get<ESM::GameSetting>();
        mL10nManager->setGmstLoader([gmst](std::string_view gmstName) -> const std::string* {
            const ESM::GameSetting* res = gmst->search(gmstName);
            if (res && res->mValue.getType() == ESM::VT_String)
                return &res->mValue.getString();
            return nullptr;
        });

        mWindowManager->setStore(mWorld->getStore());

        mTranslationDataStorage.setEncoder(mEncoder.get());
        for (const auto& contentFile : mConfig.mContentFiles)
            mTranslationDataStorage.loadTranslationData(mFileCollections, contentFile);

        Compiler::registerExtensions(mExtensions);

        mScriptContext = std::make_unique<MWScript::CompilerContext>(MWScript::CompilerContext::Type_Full);
        mScriptContext->setExtensions(&mExtensions);

        mScriptManager = std::make_unique<MWScript::ScriptManager>(
            mWorld->getStore(), *mScriptContext, mConfig.mWarningsMode);
        mEnvironment.setScriptManager(*mScriptManager);

        mMechanicsManager = std::make_unique<MWMechanics::MechanicsManager>();
        mEnvironment.setMechanicsManager(*mMechanicsManager);

        mJournal = std::make_unique<MWDialogue::Journal>();
        mEnvironment.setJournal(*mJournal);

        mDialogueManager = std::make_unique<MWDialogue::DialogueManager>(mExtensions, mTranslationDataStorage);
        mEnvironment.setDialogueManager(*mDialogueManager);

        mLuaManager->loadPermanentStorage(mCfgMgr.getUserConfigPath());
        mLuaManager->initPreLoad();
    }

    void HeadlessEngine::loadContentAndWorld()
    {
        Loading::Listener* listener = mWindowManager->getLoadingScreen();
        listener->loadingOn();
        mWorld->loadData(
            mFileCollections, mConfig.mContentFiles, mConfig.mGroundcoverFiles, mEncoder.get(), listener);
        listener->loadingOff();

        osg::ref_ptr<osg::Group> rootNode(new osg::Group);
        mWorld->init(Debug::Warning, nullptr, std::move(rootNode), mWorkQueue.get(), *mUnrefQueue);
        mWorld->getWorldScene().setResidencyConfig(mConfig.mCellRadius, mConfig.mCellGracePeriod);
        mEnvironment.setWorldScene(mWorld->getWorldScene());
        mWorld->setupPlayer();
        mWorld->setRandomSeed(mConfig.mRandomSeed);
        mLuaManager->initPostLoad();

        mLuaWorker = std::make_unique<MWLua::Worker>(*mLuaManager);

        // Bypass main menu GUI: start world simulation directly and optionally teleport to --start-cell.
        mStateManager->newGame(true);

        if (!mConfig.mStartCell.empty())
        {
            Log(Debug::Info) << "Teleporting headless host to start cell: " << mConfig.mStartCell;
            if (const ESM::Cell* cell
                = mWorld->getStore().get<ESM::Cell>().search(std::string_view(mConfig.mStartCell)))
            {
                ESM::Position pos{};
                if (cell->isExterior())
                {
                    const ESM::RefId worldspace = ESM::Cell::sDefaultWorldspaceId;
                    pos.pos[0] = (cell->mData.mX + 0.5f) * ESM::getCellSize(worldspace);
                    pos.pos[1] = (cell->mData.mY + 0.5f) * ESM::getCellSize(worldspace);
                    pos.pos[2] = mWorld->getTerrainHeightAt(
                        osg::Vec3f(pos.pos[0], pos.pos[1], 0.f), worldspace);
                }
                mWorld->changeToCell(cell->mId, pos, true);
            }
            else
            {
                Log(Debug::Warning) << "Requested --start-cell '" << mConfig.mStartCell
                                    << "' was not found in ESM store; keeping default start cell.";
            }
        }

        if (const MWWorld::CellStore* currentCell = mWorld->getWorldScene().getCurrentCell())
        {
            Log(Debug::Info) << "Headless world initialized. Active cell: "
                             << currentCell->getCell()->getDescription();
        }
        else
        {
            Log(Debug::Info) << "Headless world initialized (state=" << mStateManager->getState() << ").";
        }
    }

    void HeadlessEngine::executeLocalScripts()
    {
        MWWorld::LocalScripts& localScripts = mWorld->getLocalScripts();
        localScripts.startIteration();
        std::pair<ESM::RefId, MWWorld::Ptr> script;
        while (localScripts.getNext(script))
        {
            MWScript::InterpreterContext interpreterContext(&script.second.getRefData().getLocals(), script.second);
            mScriptManager->run(script.first, interpreterContext);
        }
    }

    void HeadlessEngine::pollNetwork(unsigned int /*tickNumber*/)
    {
        if (mServer)
            mServer->pollNetwork();
    }

    void HeadlessEngine::broadcastNetworkState(unsigned int /*tickNumber*/)
    {
        if (mServer)
            mServer->broadcastNetworkState();
    }

    bool HeadlessEngine::stepSimulation(unsigned int tickNumber, float dt)
    {
        const osg::Timer_t tickStart = mTimer->tick();

        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_QUIT)
            {
                requestStop();
                return false;
            }
        }

        try
        {
            mLuaManager->synchronizedUpdate();
            mStateManager->update(dt);

            if (mStateManager->getState() != MWBase::StateManager::State_NoGame)
            {
                std::vector<ESM::Cell> occupiedCells;
                if (mServer)
                    mServer->collectPlayerCells(occupiedCells);
                mWorld->getWorldScene().syncCellResidency(occupiedCells, dt);

                if (mWorld->getScriptsEnabled())
                {
                    executeLocalScripts();
                    mScriptManager->getGlobalScripts().run();
                }

                mWorld->getWorldScene().markCellAsUnchanged();

                const double hours = (static_cast<double>(dt) * mWorld->getTimeManager()->getGameTimeScale()) / 3600.0;
                mWorld->advanceTime(hours, true);
                mWorld->rechargeItems(dt, true);

                mMechanicsManager->update(dt, false);
                mWorld->updatePhysics(dt, false, tickStart, tickNumber, *mStats);
                mWorld->update(dt, false);
            }
        }
        catch (const std::exception& e)
        {
            Log(Debug::Error) << "Error in headless simulation tick " << tickNumber << ": " << e.what();
        }

        mUnrefQueue->flush(*mWorkQueue);

        mLuaWorker->allowUpdate(tickStart, tickNumber, *mStats);
        mLuaWorker->finishUpdate(tickStart, tickNumber, *mStats);

        MWWorld::DateTimeManager& timeManager = *mWorld->getTimeManager();
        timeManager.updateIsPaused();
        timeManager.setSimulationTime(timeManager.getSimulationTime() + dt);
        timeManager.setRenderingSimulationTime(timeManager.getRenderingSimulationTime() + dt);

        return true;
    }

    void HeadlessEngine::run()
    {
        const double fixedDt = 1.0 / std::max(1.0, mConfig.mTickRate);
        const auto stepDuration = std::chrono::duration<double>(fixedDt);

        Log(Debug::Info) << "Entering headless simulation loop: tickRate=" << mConfig.mTickRate
                         << " Hz, fixedDt=" << fixedDt << " s, port=" << mConfig.mPort
                         << (mConfig.mMaxTicks > 0 ? ", maxTicks=" + std::to_string(mConfig.mMaxTicks) : "");

        unsigned int tickNumber = 0;
        auto nextTickTime = std::chrono::steady_clock::now();

        while (!mStopRequested && !mStateManager->hasQuitRequest() && (!mServer || mServer->isRunning()))
        {
            if (mConfig.mMaxTicks > 0 && tickNumber >= mConfig.mMaxTicks)
                break;

            pollNetwork(tickNumber);
            if (mServer && !mServer->isRunning())
                break;

            const float scaledDt
                = static_cast<float>(fixedDt * mWorld->getTimeManager()->getSimulationTimeScale());
            if (!stepSimulation(tickNumber, scaledDt))
                break;

            broadcastNetworkState(tickNumber);
            ++tickNumber;

            if (tickNumber == 1 || (tickNumber % static_cast<unsigned int>(std::max(1.0, mConfig.mTickRate))) == 0)
            {
                const MWWorld::CellStore* cell = mWorld->getWorldScene().getCurrentCell();
                Log(Debug::Info) << "Headless tick " << tickNumber
                                 << " | simTime=" << mWorld->getTimeManager()->getSimulationTime()
                                 << "s | cell=" << (cell ? cell->getCell()->getDescription() : "<none>")
                                 << " | activeCells=" << mWorld->getWorldScene().getActiveCells().size()
                                 << " | players=" << (mServer ? mServer->getConnectedPlayerCount() : 0);
            }

            nextTickTime += std::chrono::duration_cast<std::chrono::steady_clock::duration>(stepDuration);
            const auto now = std::chrono::steady_clock::now();
            if (nextTickTime > now)
                std::this_thread::sleep_until(nextTickTime);
            else
                nextTickTime = now;
        }

        Log(Debug::Info) << "Completed " << tickNumber << " headless simulation tick(s).";
    }

    void HeadlessEngine::requestStop()
    {
        mStopRequested = true;
    }
}
