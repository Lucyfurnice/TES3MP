#ifndef TES3MP_HEADLESS_ENGINE_HPP
#define TES3MP_HEADLESS_ENGINE_HPP

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <components/compiler/extensions.hpp>
#include <components/files/collections.hpp>
#include <components/files/configurationmanager.hpp>
#include <components/toutf8/toutf8.hpp>
#include <components/translation/translation.hpp>

#include <osg/Stats>
#include <osg/Timer>
#include <osg/ref_ptr>

#include <apps/openmw/mwbase/environment.hpp>

namespace VFS
{
    class Manager;
}

namespace Resource
{
    class ResourceSystem;
}

namespace L10n
{
    class Manager;
}

namespace SceneUtil
{
    class WorkQueue;
    class UnrefQueue;
}

namespace MWWorld
{
    class World;
}

namespace MWScript
{
    class CompilerContext;
    class ScriptManager;
}

namespace MWDialogue
{
    class DialogueManager;
    class Journal;
}

namespace MWMechanics
{
    class MechanicsManager;
}

namespace MWState
{
    class StateManager;
}

namespace MWLua
{
    class LuaManager;
    class Worker;
}

namespace TES3MPHeadless
{
    class HeadlessWindowManager;
    class HeadlessSoundManager;
    class HeadlessInputManager;
    class HeadlessServer;

    /// Configuration options for launching the headless simulation host.
    struct HeadlessConfig
    {
        std::vector<std::string> mContentFiles;
        std::vector<std::string> mArchives;
        std::vector<std::string> mGroundcoverFiles;
        Files::PathContainer mDataDirs;
        std::filesystem::path mResourceDir;
        std::filesystem::path mServerCfgPath;
        std::string mStartCell;
        ToUTF8::FromType mEncoding = ToUTF8::WINDOWS_1252;
        uint16_t mPort = 25565;
        bool mHasPortOverride = false;
        bool mAuthoritative = true;
        bool mProfileTicks = false;
        bool mSmokeArrestCombat = false;
        double mTickRate = 60.0;
        uint64_t mMaxTicks = 0; // 0 = run indefinitely until signal/quit
        int mCellRadius = 1; // 1 = 3x3 exterior cell neighborhood per player
        double mCellGracePeriod = 5.0; // seconds before empty unpinned cells unload
        int mPreloadThreads = 1;
        unsigned int mRandomSeed = 0;
        int mWarningsMode = 0;
    };

    /// Manages lifetime and fixed-timestep execution of all headless OpenMW simulation managers.
    class HeadlessEngine
    {
    public:
        explicit HeadlessEngine(Files::ConfigurationManager& cfgMgr, HeadlessConfig config);
        ~HeadlessEngine();

        HeadlessEngine(const HeadlessEngine&) = delete;
        HeadlessEngine& operator=(const HeadlessEngine&) = delete;

        /// Initialize SDL (timer/events only), VFS, resource/ESM stores, simulation managers,
        /// load content files, and spawn directly into the configured starting cell.
        void init();

        /// Run the deterministic fixed-timestep simulation loop until stopped.
        void run();

        /// Request graceful shutdown of the simulation loop.
        void requestStop();

    private:
        struct TickTimingsMs
        {
            double mPollNetwork = 0.0;
            double mWorldUpdate = 0.0;
            double mMechanicsUpdate = 0.0;
            double mLuaUpdate = 0.0;
            double mBroadcastState = 0.0;

            double total() const
            {
                return mPollNetwork + mWorldUpdate + mMechanicsUpdate + mLuaUpdate + mBroadcastState;
            }
        };

        void initManagers();
        void loadContentAndWorld();
        void executeLocalScripts();
        bool stepSimulation(unsigned int tickNumber, float dt, TickTimingsMs& timings);

        /// Hook invoked before each simulation step for inbound RakNet packet consumption.
        void pollNetwork(unsigned int tickNumber);

        /// Hook invoked after each simulation step for outbound RakNet state replication.
        void broadcastNetworkState(unsigned int tickNumber, float dt);

        Files::ConfigurationManager& mCfgMgr;
        HeadlessConfig mConfig;
        Files::Collections mFileCollections;

        MWBase::Environment mEnvironment;
        std::unique_ptr<ToUTF8::Utf8Encoder> mEncoder;
        Translation::Storage mTranslationDataStorage;

        std::unique_ptr<VFS::Manager> mVFS;
        std::unique_ptr<Resource::ResourceSystem> mResourceSystem;
        osg::ref_ptr<SceneUtil::WorkQueue> mWorkQueue;
        std::unique_ptr<SceneUtil::UnrefQueue> mUnrefQueue;
        std::unique_ptr<L10n::Manager> mL10nManager;

        std::unique_ptr<HeadlessWindowManager> mWindowManager;
        std::unique_ptr<HeadlessSoundManager> mSoundManager;
        std::unique_ptr<HeadlessInputManager> mInputManager;
        std::unique_ptr<MWState::StateManager> mStateManager;
        std::unique_ptr<MWWorld::World> mWorld;
        Compiler::Extensions mExtensions;
        std::unique_ptr<MWScript::CompilerContext> mScriptContext;
        std::unique_ptr<MWScript::ScriptManager> mScriptManager;
        std::unique_ptr<MWMechanics::MechanicsManager> mMechanicsManager;
        std::unique_ptr<MWDialogue::Journal> mJournal;
        std::unique_ptr<MWDialogue::DialogueManager> mDialogueManager;
        std::unique_ptr<MWLua::LuaManager> mLuaManager;
        std::unique_ptr<MWLua::Worker> mLuaWorker;
        std::unique_ptr<HeadlessServer> mServer;

        osg::ref_ptr<osg::Stats> mStats;
        const osg::Timer* mTimer = nullptr;
        bool mSdlInitialized = false;
        volatile bool mStopRequested = false;
    };
}

#endif
