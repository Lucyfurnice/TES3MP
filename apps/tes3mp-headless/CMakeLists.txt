project(tes3mp-headless)

set(HEADLESS_SIM_SOURCES
    # mwbase
    ../openmw/mwbase/environment.cpp

    # mwdialogue
    ../openmw/mwdialogue/dialoguemanagerimp.cpp
    ../openmw/mwdialogue/filter.cpp
    ../openmw/mwdialogue/journalentry.cpp
    ../openmw/mwdialogue/journalimp.cpp
    ../openmw/mwdialogue/keywordsearch.cpp
    ../openmw/mwdialogue/quest.cpp
    ../openmw/mwdialogue/scripttest.cpp
    ../openmw/mwdialogue/selectwrapper.cpp
    ../openmw/mwdialogue/topic.cpp

    # mwscript
    ../openmw/mwscript/aiextensions.cpp
    ../openmw/mwscript/animationextensions.cpp
    ../openmw/mwscript/cellextensions.cpp
    ../openmw/mwscript/compilercontext.cpp
    ../openmw/mwscript/containerextensions.cpp
    ../openmw/mwscript/controlextensions.cpp
    ../openmw/mwscript/dialogueextensions.cpp
    ../openmw/mwscript/extensions.cpp
    ../openmw/mwscript/globalscripts.cpp
    ../openmw/mwscript/guiextensions.cpp
    ../openmw/mwscript/interpretercontext.cpp
    ../openmw/mwscript/locals.cpp
    ../openmw/mwscript/miscextensions.cpp
    ../openmw/mwscript/ref.cpp
    ../openmw/mwscript/scriptmanagerimp.cpp
    ../openmw/mwscript/skyextensions.cpp
    ../openmw/mwscript/soundextensions.cpp
    ../openmw/mwscript/statsextensions.cpp
    ../openmw/mwscript/transformationextensions.cpp
    ../openmw/mwscript/userextensions.cpp

    # mwlua (uibindings and postprocessingbindings replaced by headless stubs)
    ../openmw/mwlua/animationbindings.cpp
    ../openmw/mwlua/birthsignbindings.cpp
    ../openmw/mwlua/camerabindings.cpp
    ../openmw/mwlua/cellbindings.cpp
    ../openmw/mwlua/classbindings.cpp
    ../openmw/mwlua/contentbindings.cpp
    ../openmw/mwlua/corebindings.cpp
    ../openmw/mwlua/coremwscriptbindings.cpp
    ../openmw/mwlua/debugbindings.cpp
    ../openmw/mwlua/dialoguebindings.cpp
    ../openmw/mwlua/engineevents.cpp
    ../openmw/mwlua/factionbindings.cpp
    ../openmw/mwlua/inputbindings.cpp
    ../openmw/mwlua/itemdata.cpp
    ../openmw/mwlua/landbindings.cpp
    ../openmw/mwlua/localscripts.cpp
    ../openmw/mwlua/luabindings.cpp
    ../openmw/mwlua/luaevents.cpp
    ../openmw/mwlua/luamanagerimp.cpp
    ../openmw/mwlua/magicbindings.cpp
    ../openmw/mwlua/magictypebindings.cpp
    ../openmw/mwlua/markupbindings.cpp
    ../openmw/mwlua/menuscripts.cpp
    ../openmw/mwlua/mwscriptbindings.cpp
    ../openmw/mwlua/nearbybindings.cpp
    ../openmw/mwlua/objectbindings.cpp
    ../openmw/mwlua/objectlists.cpp
    ../openmw/mwlua/racebindings.cpp
    ../openmw/mwlua/regionbindings.cpp
    ../openmw/mwlua/soundbindings.cpp
    ../openmw/mwlua/stats.cpp
    ../openmw/mwlua/userdataserializer.cpp
    ../openmw/mwlua/vfsbindings.cpp
    ../openmw/mwlua/weatherbindings.cpp
    ../openmw/mwlua/worker.cpp
    ../openmw/mwlua/worldbindings.cpp
    ../openmw/mwlua/types/activator.cpp
    ../openmw/mwlua/types/actor.cpp
    ../openmw/mwlua/types/apparatus.cpp
    ../openmw/mwlua/types/armor.cpp
    ../openmw/mwlua/types/book.cpp
    ../openmw/mwlua/types/clothing.cpp
    ../openmw/mwlua/types/container.cpp
    ../openmw/mwlua/types/creature.cpp
    ../openmw/mwlua/types/door.cpp
    ../openmw/mwlua/types/ingredient.cpp
    ../openmw/mwlua/types/item.cpp
    ../openmw/mwlua/types/levelledlist.cpp
    ../openmw/mwlua/types/light.cpp
    ../openmw/mwlua/types/lockable.cpp
    ../openmw/mwlua/types/lockpick.cpp
    ../openmw/mwlua/types/misc.cpp
    ../openmw/mwlua/types/npc.cpp
    ../openmw/mwlua/types/player.cpp
    ../openmw/mwlua/types/potion.cpp
    ../openmw/mwlua/types/probe.cpp
    ../openmw/mwlua/types/repair.cpp
    ../openmw/mwlua/types/static.cpp
    ../openmw/mwlua/types/terminal.cpp
    ../openmw/mwlua/types/types.cpp
    ../openmw/mwlua/types/weapon.cpp

    # mwworld
    ../openmw/mwworld/actionalchemy.cpp
    ../openmw/mwworld/actionapply.cpp
    ../openmw/mwworld/action.cpp
    ../openmw/mwworld/actiondoor.cpp
    ../openmw/mwworld/actioneat.cpp
    ../openmw/mwworld/actionequip.cpp
    ../openmw/mwworld/actionharvest.cpp
    ../openmw/mwworld/actionopen.cpp
    ../openmw/mwworld/actionread.cpp
    ../openmw/mwworld/actionrepair.cpp
    ../openmw/mwworld/actionsoulgem.cpp
    ../openmw/mwworld/actiontake.cpp
    ../openmw/mwworld/actiontalk.cpp
    ../openmw/mwworld/actionteleport.cpp
    ../openmw/mwworld/actiontrap.cpp
    ../openmw/mwworld/cell.cpp
    ../openmw/mwworld/cellpreloader.cpp
    ../openmw/mwworld/cellref.cpp
    ../openmw/mwworld/cellstore.cpp
    ../openmw/mwworld/class.cpp
    ../openmw/mwworld/containerstore.cpp
    ../openmw/mwworld/customdata.cpp
    ../openmw/mwworld/datetimemanager.cpp
    ../openmw/mwworld/esmloader.cpp
    ../openmw/mwworld/esmstore.cpp
    ../openmw/mwworld/failedaction.cpp
    ../openmw/mwworld/globals.cpp
    ../openmw/mwworld/groundcoverstore.cpp
    ../openmw/mwworld/inventorystore.cpp
    ../openmw/mwworld/livecellref.cpp
    ../openmw/mwworld/localscripts.cpp
    ../openmw/mwworld/magiceffects.cpp
    ../openmw/mwworld/manualref.cpp
    ../openmw/mwworld/player.cpp
    ../openmw/mwworld/projectilemanager.cpp
    ../openmw/mwworld/ptr.cpp
    ../openmw/mwworld/refdata.cpp
    ../openmw/mwworld/scene.cpp
    ../openmw/mwworld/store.cpp
    ../openmw/mwworld/timestamp.cpp
    ../openmw/mwworld/weather.cpp
    ../openmw/mwworld/worldimp.cpp
    ../openmw/mwworld/worldmodel.cpp

    # mwphysics
    ../openmw/mwphysics/actorconvexcallback.cpp
    ../openmw/mwphysics/actor.cpp
    ../openmw/mwphysics/closestnotmerayresultcallback.cpp
    ../openmw/mwphysics/contacttestresultcallback.cpp
    ../openmw/mwphysics/contacttestwrapper.cpp
    ../openmw/mwphysics/heightfield.cpp
    ../openmw/mwphysics/movementsolver.cpp
    ../openmw/mwphysics/mtphysics.cpp
    ../openmw/mwphysics/object.cpp
    ../openmw/mwphysics/physicssystem.cpp
    ../openmw/mwphysics/projectileconvexcallback.cpp
    ../openmw/mwphysics/projectile.cpp
    ../openmw/mwphysics/stepper.cpp
    ../openmw/mwphysics/trace.cpp

    # mwclass
    ../openmw/mwclass/activator.cpp
    ../openmw/mwclass/actor.cpp
    ../openmw/mwclass/apparatus.cpp
    ../openmw/mwclass/armor.cpp
    ../openmw/mwclass/bodypart.cpp
    ../openmw/mwclass/book.cpp
    ../openmw/mwclass/classes.cpp
    ../openmw/mwclass/clothing.cpp
    ../openmw/mwclass/container.cpp
    ../openmw/mwclass/creature.cpp
    ../openmw/mwclass/creaturelevlist.cpp
    ../openmw/mwclass/door.cpp
    ../openmw/mwclass/esm4base.cpp
    ../openmw/mwclass/esm4npc.cpp
    ../openmw/mwclass/ingredient.cpp
    ../openmw/mwclass/itemlevlist.cpp
    ../openmw/mwclass/light4.cpp
    ../openmw/mwclass/light.cpp
    ../openmw/mwclass/lockpick.cpp
    ../openmw/mwclass/misc.cpp
    ../openmw/mwclass/npc.cpp
    ../openmw/mwclass/potion.cpp
    ../openmw/mwclass/probe.cpp
    ../openmw/mwclass/repair.cpp
    ../openmw/mwclass/static.cpp
    ../openmw/mwclass/weapon.cpp

    # mwmechanics
    ../openmw/mwmechanics/activespells.cpp
    ../openmw/mwmechanics/actors.cpp
    ../openmw/mwmechanics/actorutil.cpp
    ../openmw/mwmechanics/aiactivate.cpp
    ../openmw/mwmechanics/aiavoiddoor.cpp
    ../openmw/mwmechanics/aibreathe.cpp
    ../openmw/mwmechanics/aicast.cpp
    ../openmw/mwmechanics/aicombataction.cpp
    ../openmw/mwmechanics/aicombat.cpp
    ../openmw/mwmechanics/aiescort.cpp
    ../openmw/mwmechanics/aiface.cpp
    ../openmw/mwmechanics/aifollow.cpp
    ../openmw/mwmechanics/aipackage.cpp
    ../openmw/mwmechanics/aipursue.cpp
    ../openmw/mwmechanics/aisequence.cpp
    ../openmw/mwmechanics/aitravel.cpp
    ../openmw/mwmechanics/aiwander.cpp
    ../openmw/mwmechanics/alchemy.cpp
    ../openmw/mwmechanics/autocalcspell.cpp
    ../openmw/mwmechanics/character.cpp
    ../openmw/mwmechanics/combat.cpp
    ../openmw/mwmechanics/creaturestats.cpp
    ../openmw/mwmechanics/difficultyscaling.cpp
    ../openmw/mwmechanics/enchanting.cpp
    ../openmw/mwmechanics/levelledlist.cpp
    ../openmw/mwmechanics/magiceffects.cpp
    ../openmw/mwmechanics/mechanicsmanagerimp.cpp
    ../openmw/mwmechanics/npcstats.cpp
    ../openmw/mwmechanics/objects.cpp
    ../openmw/mwmechanics/obstacle.cpp
    ../openmw/mwmechanics/pathfinding.cpp
    ../openmw/mwmechanics/pathgrid.cpp
    ../openmw/mwmechanics/pickpocket.cpp
    ../openmw/mwmechanics/recharge.cpp
    ../openmw/mwmechanics/repair.cpp
    ../openmw/mwmechanics/security.cpp
    ../openmw/mwmechanics/spellcasting.cpp
    ../openmw/mwmechanics/spelleffects.cpp
    ../openmw/mwmechanics/spelllist.cpp
    ../openmw/mwmechanics/spellpriority.cpp
    ../openmw/mwmechanics/spellresistance.cpp
    ../openmw/mwmechanics/spells.cpp
    ../openmw/mwmechanics/spellutil.cpp
    ../openmw/mwmechanics/stat.cpp
    ../openmw/mwmechanics/steering.cpp
    ../openmw/mwmechanics/summoning.cpp
    ../openmw/mwmechanics/weaponpriority.cpp
    ../openmw/mwmechanics/weapontype.cpp

    # mwstate
    ../openmw/mwstate/character.cpp
    ../openmw/mwstate/charactermanager.cpp
    ../openmw/mwstate/quicksavemanager.cpp
    ../openmw/mwstate/statemanagerimp.cpp

    # mwrender (data-only land manager for ESM terrain heights/physics)
    ../openmw/mwrender/landmanager.cpp
)

set(HEADLESS_SERVER_SOURCES
    ../openmw-mp/Player.cpp
    ../openmw-mp/Networking.cpp
    ../openmw-mp/MasterClient.cpp
    ../openmw-mp/Cell.cpp
    ../openmw-mp/CellController.cpp
    ../openmw-mp/Utils.cpp
    ../openmw-mp/Script/Script.cpp
    ../openmw-mp/Script/ScriptFunction.cpp
    ../openmw-mp/Script/ScriptFunctions.cpp
    ../openmw-mp/Script/Functions/Actors.cpp
    ../openmw-mp/Script/Functions/Objects.cpp
    ../openmw-mp/Script/Functions/Miscellaneous.cpp
    ../openmw-mp/Script/Functions/Worldstate.cpp
    ../openmw-mp/Script/Functions/Books.cpp
    ../openmw-mp/Script/Functions/Cells.cpp
    ../openmw-mp/Script/Functions/CharClass.cpp
    ../openmw-mp/Script/Functions/Chat.cpp
    ../openmw-mp/Script/Functions/Dialogue.cpp
    ../openmw-mp/Script/Functions/Factions.cpp
    ../openmw-mp/Script/Functions/GUI.cpp
    ../openmw-mp/Script/Functions/Items.cpp
    ../openmw-mp/Script/Functions/Mechanics.cpp
    ../openmw-mp/Script/Functions/Positions.cpp
    ../openmw-mp/Script/Functions/Quests.cpp
    ../openmw-mp/Script/Functions/RecordsDynamic.cpp
    ../openmw-mp/Script/Functions/Server.cpp
    ../openmw-mp/Script/Functions/Settings.cpp
    ../openmw-mp/Script/Functions/Shapeshift.cpp
    ../openmw-mp/Script/Functions/Spells.cpp
    ../openmw-mp/Script/Functions/Stats.cpp
    ../openmw-mp/Script/Functions/Timer.cpp
    ../openmw-mp/Script/API/TimerAPI.cpp
    ../openmw-mp/Script/API/PublicFnAPI.cpp
    ../openmw-mp/Script/LangLua/LangLua.cpp
    ../openmw-mp/Script/LangLua/LuaFunc.cpp
    ../openmw-mp/Script/LangNative/LangNative.cpp
    ../openmw-mp/processors/ProcessorInitializer.cpp
    ../openmw-mp/processors/PlayerProcessor.cpp
    ../openmw-mp/processors/ActorProcessor.cpp
    ../openmw-mp/processors/ObjectProcessor.cpp
    ../openmw-mp/processors/WorldstateProcessor.cpp
)

add_library(tes3mp-headless-server STATIC
    ${HEADLESS_SERVER_SOURCES}
)

set_target_properties(tes3mp-headless-server PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS OFF
)

target_compile_definitions(tes3mp-headless-server PRIVATE
    TES3MP_HEADLESS
    ENABLE_LUA
    $<$<BOOL:${WIN32}>:_USE_MATH_DEFINES>
)

target_compile_options(tes3mp-headless-server PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/permissive->
    $<$<CXX_COMPILER_ID:MSVC>:/templateDepth:2048>
    $<$<CXX_COMPILER_ID:MSVC>:/bigobj>
    $<$<BOOL:${UNIX}>:-Wno-ignored-qualifiers>
)

target_include_directories(tes3mp-headless-server PRIVATE
    ${CMAKE_SOURCE_DIR}/apps/openmw-mp
    ${CMAKE_SOURCE_DIR}/extern
    ${RakNet_INCLUDES}
    ${LUA_INCLUDE_DIR}
    ${CMAKE_SOURCE_DIR}/extern/LuaBridge
    ${CMAKE_SOURCE_DIR}/extern/PicoSHA2
)

target_link_libraries(tes3mp-headless-server PRIVATE
    components
    ${RakNet_LIBRARY}
    ${LUA_LIBRARIES}
    LuaBridge
    PicoSHA2
    $<$<BOOL:${UNIX}>:dl>
)

set(HEADLESS_STUB_SOURCES
    stubs/headlesswindowmanager.hpp
    stubs/headlesswindowmanager.cpp
    stubs/headlesssoundmanager.hpp
    stubs/headlesssoundmanager.cpp
    stubs/headlessinputmanager.hpp
    stubs/headlessinputmanager.cpp
    stubs/headlessmwrender.cpp
    stubs/headlessanimation.cpp
    stubs/headlessluabindings.cpp
    ../openmw/mwmp/RecordHelper.cpp
    stubs/headlessmwmp.cpp
)

set(HEADLESS_ENGINE_SOURCES
    headlessconfig.hpp
    headlessconfig.cpp
    headlessengine.hpp
    headlessengine.cpp
    server/cellinstancemanager.hpp
    server/cellinstancemanager.cpp
    server/headlesscombat.hpp
    server/headlesscombat.cpp
    server/headlessdialogue.hpp
    server/headlessdialogue.cpp
    server/headlessreplication.hpp
    server/headlessreplication.cpp
    server/headlessserver.hpp
    server/headlessserver.cpp
    server/playeranchormanager.hpp
    server/playeranchormanager.cpp
    main.cpp
)

add_executable(tes3mp-headless
    ${HEADLESS_SIM_SOURCES}
    ${HEADLESS_STUB_SOURCES}
    ${HEADLESS_ENGINE_SOURCES}
)

set_target_properties(tes3mp-headless PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS OFF
)

target_compile_definitions(tes3mp-headless PRIVATE TES3MP_HEADLESS ENABLE_LUA)

set_source_files_properties(
    server/headlesscombat.cpp
    server/headlessdialogue.cpp
    server/headlessreplication.cpp
    server/headlessserver.cpp
    server/playeranchormanager.cpp
    PROPERTIES
    INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/apps/openmw-mp;${LUA_INCLUDE_DIR};${CMAKE_SOURCE_DIR}/extern/LuaBridge"
)

target_include_directories(tes3mp-headless PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/apps/openmw
    ${CMAKE_SOURCE_DIR}/extern
    ${RakNet_INCLUDES}
)

# Explicitly link simulation, OSG data/DB/Util, Bullet/Recast (via components),
# SDL2 (timer/events), RakNet, and embedded tes3mp-server runtime; exclude MyGUI, OpenGL, osgViewer, OpenAL, FFmpeg.
target_link_libraries(tes3mp-headless PRIVATE
    tes3mp-headless-server
    components
    ${RakNet_LIBRARY}
    ${LUA_LIBRARIES}
    LuaBridge
    Base64
    PicoSHA2
    Boost::program_options
    SDL2::SDL2
    ${OSGDB_LIBRARIES}
    ${OSGUTIL_LIBRARIES}
    ${OSGANIMATION_LIBRARIES}
    ${OSG_LIBRARIES}
    ${OPENTHREADS_LIBRARIES}
    ${RecastNavigation_LIBRARIES}
)

if (MSVC)
    set_source_files_properties(
        ../openmw/mwlua/dialoguebindings.cpp
        ../openmw/mwlua/magicbindings.cpp
        ../openmw/mwlua/objectbindings.cpp
        ../openmw/mwlua/stats.cpp
        ../openmw/mwworld/esmstore.cpp
        PROPERTIES
        COMPILE_OPTIONS /cgthreads1
    )
endif()

if (UNIX AND NOT APPLE)
    target_link_libraries(tes3mp-headless PRIVATE ${CMAKE_THREAD_LIBS_INIT})
endif()

if (WIN32)
    target_link_libraries(tes3mp-headless PRIVATE dbghelp)
    install(TARGETS tes3mp-headless RUNTIME DESTINATION ".")
endif()
