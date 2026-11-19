#include <components/debug/debugging.hpp>
#include <components/fallback/fallback.hpp>
#include <components/fallback/validate.hpp>
#include <components/files/configurationmanager.hpp>
#include <components/misc/osgpluginchecker.hpp>
#include <components/misc/rng.hpp>
#include <components/platform/platform.hpp>
#include <components/version/version.hpp>

#include "mwgui/debugwindow.hpp"

#include "engine.hpp"
#include "options.hpp"

/*
    Start of tes3mp addition

    Include additional headers for multiplayer purposes
*/
#include "mwmp/Main.hpp"
#include <components/openmw-mp/TimedLog.hpp>
#include <components/openmw-mp/Utils.hpp>
#include <components/openmw-mp/Version.hpp>
/*
    End of tes3mp addition
*/

#include <boost/program_options/variables_map.hpp>

#if defined(_WIN32)
#include <components/misc/windows.hpp>
#include <DbgHelp.h>
// makes __argc and __argv available on windows
#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#ifdef _DEBUG
#include <crtdbg.h>
#endif
#include <exception>
#include <sstream>

extern "C" __declspec(dllexport) DWORD AmdPowerXpressRequestHighPerformance = 0x00000001;
#endif

#include <filesystem>

#if (defined(__APPLE__) || defined(__linux) || defined(__unix) || defined(__posix))
#include <unistd.h>
#endif

/**
 * \brief Parses application command line and calls \ref Cfg::ConfigurationManager
 * to parse configuration files.
 *
 * Results are directly written to \ref Engine class.
 *
 * \retval true - Everything goes OK
 * \retval false - Error
 */
bool parseOptions(int argc, char** argv, OMW::Engine& engine, Files::ConfigurationManager& cfgMgr)
{
    // Create a local alias for brevity
    namespace bpo = boost::program_options;
    typedef std::vector<std::string> StringsVector;

    bpo::options_description desc = OpenMW::makeOptionsDescription();
    /*
        Start of tes3mp addition

        Parse options added by multiplayer
    */
    mwmp::Main::optionsDesc(&desc);
    /*
        End of tes3mp addition
    */
    bpo::variables_map variables;

    Files::parseArgs(argc, argv, variables, desc);
    bpo::notify(variables);

    if (variables.count("help"))
    {
        Debug::getRawStdout() << desc << std::endl;
        return false;
    }

    if (variables.count("version"))
    {
        Debug::getRawStdout() << Version::getOpenmwVersionDescription() << std::endl;
        return false;
    }

    cfgMgr.processPaths(variables, std::filesystem::current_path());

    cfgMgr.readConfiguration(variables, desc);

    Debug::setupLogging(cfgMgr.getLogPath(), "tes3mp-client-" + TimedLog::getFilenameTimestamp());
    /*
        Start of tes3mp addition

        Print the multiplayer version first
    */
    Log(Debug::Info) << Utils::getVersionInfo(
        "TES3MP client", TES3MP_VERSION, std::string(Version::getCommitHash()), TES3MP_PROTO_VERSION);
    /*
        End of tes3mp addition
    */
    Log(Debug::Info) << "OpenMW version " << Version::getVersion();

    Settings::Manager::load(cfgMgr);

    MWGui::DebugWindow::startLogRecording();

    engine.setGrabMouse(!variables["no-grab"].as<bool>());

    // Font encoding settings
    std::string encoding(variables["encoding"].as<std::string>());
    Log(Debug::Info) << ToUTF8::encodingUsingMessage(encoding);
    engine.setEncoding(ToUTF8::calculateEncoding(encoding));

    Files::PathContainer dataDirs(asPathContainer(variables["data"].as<Files::MaybeQuotedPathContainer>()));

    Files::PathContainer::value_type local(variables["data-local"]
                                               .as<Files::MaybeQuotedPathContainer::value_type>()
                                               .u8string()); // This call to u8string is redundant, but required to
                                                             // build on MSVC 14.26 due to implementation bugs.
    if (!local.empty())
        dataDirs.push_back(std::move(local));

    cfgMgr.filterOutNonExistingPaths(dataDirs);

    engine.setResourceDir(variables["resources"]
                              .as<Files::MaybeQuotedPath>()
                              .u8string()); // This call to u8string is redundant, but required to build on MSVC 14.26
                                            // due to implementation bugs.
    engine.setDataDirs(dataDirs);

    // fallback archives
    StringsVector archives = variables["fallback-archive"].as<StringsVector>();
    for (StringsVector::const_iterator it = archives.begin(); it != archives.end(); ++it)
    {
        engine.addArchive(*it);
    }

    StringsVector content = variables["content"].as<StringsVector>();
    if (content.empty())
    {
        Log(Debug::Error) << "No content file given (esm/esp, nor omwgame/omwaddon). Aborting...";
        return false;
    }
    engine.addContentFile("builtin.omwscripts");
    std::set<std::string> contentDedupe{ "builtin.omwscripts" };
    for (const auto& contentFile : content)
    {
        if (!contentDedupe.insert(contentFile).second)
        {
            Log(Debug::Error) << "Content file specified more than once: " << contentFile << ". Aborting...";
            return false;
        }
    }

    for (auto& file : content)
    {
        engine.addContentFile(file);
    }

    StringsVector groundcover = variables["groundcover"].as<StringsVector>();
    for (auto& file : groundcover)
    {
        engine.addGroundcoverFile(file);
    }

    if (variables.count("lua-scripts"))
    {
        Log(Debug::Warning) << "Lua scripts have been specified via the old lua-scripts option and will not be loaded. "
                               "Please update them to a version which uses the new omwscripts format.";
    }

    // startup-settings
    engine.setCell(variables["start"].as<std::string>());
    engine.setSkipMenu(variables["skip-menu"].as<bool>(), variables["new-game"].as<bool>());
    if (!variables["skip-menu"].as<bool>() && variables["new-game"].as<bool>())
        Log(Debug::Warning) << "Warning: new-game used without skip-menu -> ignoring it";

    // scripts
    engine.setCompileAll(variables["script-all"].as<bool>());
    engine.setCompileAllDialogue(variables["script-all-dialogue"].as<bool>());
    engine.setScriptConsoleMode(variables["script-console"].as<bool>());
    /*
        Start of tes3mp change (major)

        Clients should not be allowed to set any of these unilaterally in multiplayer, so
        disable them
    */
    /*
    engine.setStartupScript(variables["script-run"].as<std::string>());
    engine.setWarningsMode(variables["script-warn"].as<int>());
    engine.setSaveGameFile(variables["load-savegame"].as<Files::MaybeQuotedPath>().u8string());
    */
    /*
        End of tes3mp change (major)
    */

    // other settings
    Fallback::Map::init(variables["fallback"].as<Fallback::FallbackMap>().mMap);
    engine.setSoundUsage(!variables["no-sound"].as<bool>());
    engine.setActivationDistanceOverride(variables["activate-dist"].as<int>());
    engine.enableFontExport(variables["export-fonts"].as<bool>());
    engine.setRandomSeed(variables["random-seed"].as<unsigned int>());

    /*
        Start of tes3mp addition

        Configure multiplayer using parsed variables
    */
    mwmp::Main::configure(variables);
    /*
        End of tes3mp addition
    */

    return true;
}

namespace
{
    class OSGLogHandler : public osg::NotifyHandler
    {
        void notify(osg::NotifySeverity severity, const char* msg) override
        {
            // Copy, because osg logging is not thread safe.
            std::string msgCopy(msg);
            if (msgCopy.empty())
                return;

            Debug::Level level;
            switch (severity)
            {
                case osg::ALWAYS:
                case osg::FATAL:
                    level = Debug::Error;
                    break;
                case osg::WARN:
                case osg::NOTICE:
                    level = Debug::Warning;
                    break;
                case osg::INFO:
                    level = Debug::Info;
                    break;
                case osg::DEBUG_INFO:
                case osg::DEBUG_FP:
                default:
                    level = Debug::Debug;
            }
            std::string_view s(msgCopy);
            if (s.size() < 1024)
                Log(level) << (s.back() == '\n' ? s.substr(0, s.size() - 1) : s);
            else
            {
                while (!s.empty())
                {
                    size_t lineSize = 1;
                    while (lineSize < s.size() && s[lineSize - 1] != '\n')
                        lineSize++;
                    Log(level) << s.substr(0, s[lineSize - 1] == '\n' ? lineSize - 1 : lineSize);
                    s = s.substr(lineSize);
                }
            }
        }
    };
}

#if defined(_WIN32)
namespace
{
    LPTOP_LEVEL_EXCEPTION_FILTER sPreviousExceptionFilter = nullptr;

    void writeWindowsCrashReport(const char* header, CONTEXT* context)
    {
        std::ostringstream out;
        out << "\n=== TES3MP CRASH REPORT ===\n" << header << "\n";

        if (context != nullptr)
        {
            HANDLE process = GetCurrentProcess();
            HANDLE thread = GetCurrentThread();
            SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
            SymInitialize(process, nullptr, TRUE);

            CONTEXT ctxCopy = *context;
            STACKFRAME64 frame{};
            DWORD machineType = 0;
#ifdef _M_X64
            machineType = IMAGE_FILE_MACHINE_AMD64;
            frame.AddrPC.Offset = ctxCopy.Rip;
            frame.AddrFrame.Offset = ctxCopy.Rsp;
            frame.AddrStack.Offset = ctxCopy.Rsp;
#elif defined(_M_IX86)
            machineType = IMAGE_FILE_MACHINE_I386;
            frame.AddrPC.Offset = ctxCopy.Eip;
            frame.AddrFrame.Offset = ctxCopy.Ebp;
            frame.AddrStack.Offset = ctxCopy.Esp;
#elif defined(_M_ARM64)
            machineType = IMAGE_FILE_MACHINE_ARM64;
            frame.AddrPC.Offset = ctxCopy.Pc;
            frame.AddrFrame.Offset = ctxCopy.Fp;
            frame.AddrStack.Offset = ctxCopy.Sp;
#endif
            frame.AddrPC.Mode = AddrModeFlat;
            frame.AddrFrame.Mode = AddrModeFlat;
            frame.AddrStack.Mode = AddrModeFlat;

            char symBuffer[sizeof(SYMBOL_INFO) + 512]{};
            auto* sym = reinterpret_cast<SYMBOL_INFO*>(symBuffer);
            sym->SizeOfStruct = sizeof(SYMBOL_INFO);
            sym->MaxNameLen = 511;

            IMAGEHLP_LINE64 line{};
            line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);

            for (int i = 0; i < 64 && machineType != 0; ++i)
            {
                if (!StackWalk64(machineType, process, thread, &frame, &ctxCopy, nullptr,
                        SymFunctionTableAccess64, SymGetModuleBase64, nullptr))
                    break;
                if (frame.AddrPC.Offset == 0)
                    break;

                DWORD64 symDisp = 0;
                DWORD lineDisp = 0;
                const char* funcName = "<unknown>";
                if (SymFromAddr(process, frame.AddrPC.Offset, &symDisp, sym))
                    funcName = sym->Name;

                out << "  #" << i << " " << funcName;
                if (SymGetLineFromAddr64(process, frame.AddrPC.Offset, &lineDisp, &line))
                    out << " (" << line.FileName << ":" << line.LineNumber << ")";
                else
                    out << " (0x" << std::hex << frame.AddrPC.Offset << std::dec << ")";
                out << "\n";
            }

            SymCleanup(process);
        }
        out << "===========================\n";

        const std::string report = out.str();
        std::fputs(report.c_str(), stderr);
        std::fflush(stderr);
        Log(Debug::Error) << report;
        std::cerr.flush();
        std::cout.flush();
    }

    LONG WINAPI tes3mpUnhandledExceptionFilter(EXCEPTION_POINTERS* info)
    {
        std::ostringstream hdr;
        if (info && info->ExceptionRecord)
        {
            hdr << "Unhandled SEH exception code=0x" << std::hex << info->ExceptionRecord->ExceptionCode
                << " address=" << info->ExceptionRecord->ExceptionAddress << std::dec;
        }
        else
        {
            hdr << "Unhandled SEH exception (no record)";
        }
        writeWindowsCrashReport(hdr.str().c_str(), info ? info->ContextRecord : nullptr);

        if (sPreviousExceptionFilter != nullptr)
            return sPreviousExceptionFilter(info);
        return EXCEPTION_EXECUTE_HANDLER;
    }

    void tes3mpTerminateHandler()
    {
        CONTEXT ctx{};
        RtlCaptureContext(&ctx);
        std::string reason = "std::terminate() called";
        if (const auto eptr = std::current_exception())
        {
            try
            {
                std::rethrow_exception(eptr);
            }
            catch (const std::exception& e)
            {
                reason += std::string(": ") + e.what();
            }
            catch (...)
            {
                reason += ": unknown exception";
            }
        }
        writeWindowsCrashReport(reason.c_str(), &ctx);
        std::_Exit(1);
    }

    void tes3mpAbortHandler(int)
    {
        CONTEXT ctx{};
        RtlCaptureContext(&ctx);
        writeWindowsCrashReport("SIGABRT received", &ctx);
        std::_Exit(3);
    }

#if defined(_DEBUG)
    int __cdecl tes3mpCrtReportHook(int reportType, char* message, int* returnValue)
    {
        if (reportType == _CRT_ASSERT || reportType == _CRT_ERROR)
        {
            static std::atomic_flag sInReport = ATOMIC_FLAG_INIT;
            if (!sInReport.test_and_set())
            {
                CONTEXT ctx{};
                RtlCaptureContext(&ctx);
                std::string header = "MSVC CRT report (type=" + std::to_string(reportType) + ")";
                if (message != nullptr)
                    header += std::string(": ") + message;
                writeWindowsCrashReport(header.c_str(), &ctx);
                sInReport.clear();
            }
        }
        if (returnValue != nullptr)
            *returnValue = 0;
        return FALSE;
    }
#endif
}
#endif

int runApplication(int argc, char* argv[])
{
#if defined(_WIN32)
    sPreviousExceptionFilter = SetUnhandledExceptionFilter(&tes3mpUnhandledExceptionFilter);
    std::set_terminate(&tes3mpTerminateHandler);
    std::signal(SIGABRT, &tes3mpAbortHandler);
#if defined(_DEBUG)
    _CrtSetReportHook2(_CRT_RPTHOOK_INSTALL, &tes3mpCrtReportHook);
#endif
#endif
    Platform::init();

#ifdef __APPLE__
    setenv("OSG_GL_TEXTURE_STORAGE", "OFF", 0);
#endif

    osg::setNotifyHandler(new OSGLogHandler());
    Files::ConfigurationManager cfgMgr;
    std::unique_ptr<OMW::Engine> engine = std::make_unique<OMW::Engine>(cfgMgr);

    engine->setRecastMaxLogLevel(Debug::getRecastMaxLogLevel());

    if (parseOptions(argc, argv, *engine, cfgMgr))
    {
        if (!Misc::checkRequiredOSGPluginsArePresent())
            return 1;

        engine->go();
    }

    return 0;
}

#ifdef ANDROID
extern "C" int SDL_main(int argc, char** argv)
#else
int main(int argc, char** argv)
#endif
{
    /*
        Start of tes3mp addition

        Initialize the logger added for multiplayer
    */
    LOG_INIT(TimedLog::LOG_INFO);
    /*
        End of tes3mp addition
    */

    return Debug::wrapApplication(&runApplication, argc, argv, "TES3MP");
}

// Platform specific for Windows when there is no console built into the executable.
// Windows will call the WinMain function instead of main in this case, the normal
// main function is then called with the __argc and __argv parameters.
#if defined(_WIN32) && !defined(_CONSOLE)
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
    return main(__argc, __argv);
}
#endif
