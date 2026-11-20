set(BROWSER_UI
    ${CMAKE_SOURCE_DIR}/files/tes3mp/ui/Main.ui
    ${CMAKE_SOURCE_DIR}/files/tes3mp/ui/ServerInfo.ui
)

set(BROWSER
    main.cpp
    MainWindow.cpp
    ServerModel.cpp
    ServerInfoDialog.cpp
    MySortFilterProxyModel.cpp
    netutils/HTTPNetwork.cpp
    netutils/Utils.cpp
    netutils/QueryClient.cpp
    PingUpdater.cpp
    PingHelper.cpp
    QueryHelper.cpp
    ${CMAKE_SOURCE_DIR}/files/tes3mp/browser.rc
)

set(BROWSER_HEADER_MOC
    MainWindow.hpp
    ServerModel.hpp
    ServerInfoDialog.hpp
    MySortFilterProxyModel.hpp
    PingUpdater.hpp
    PingHelper.hpp
    QueryHelper.hpp
)

set(BROWSER_HEADER
    ${BROWSER_HEADER_MOC}
    netutils/HTTPNetwork.hpp
    netutils/Utils.hpp
    netutils/QueryClient.hpp
    Types.hpp
)

source_group(browser FILES ${BROWSER} ${BROWSER_HEADER})

if(WIN32)
    set(GUI_TYPE WIN32)
endif(WIN32)

qt_add_resources(RCC_SRCS ${CMAKE_SOURCE_DIR}/files/launcher/launcher.qrc)

openmw_add_executable(tes3mp-browser
    ${GUI_TYPE}
    ${BROWSER}
    ${BROWSER_HEADER}
    ${BROWSER_UI}
    ${RCC_SRCS}
)

if (WIN32)
    install(TARGETS tes3mp-browser RUNTIME DESTINATION ".")
    target_sources(tes3mp-browser PRIVATE ${CMAKE_SOURCE_DIR}/files/windows/other-apps.manifest)
endif (WIN32)

target_include_directories(tes3mp-browser PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
    ${RakNet_INCLUDES}
)

target_link_libraries(tes3mp-browser
    ${RakNet_LIBRARY}
    components
    components_qt
    Base64
    PicoSHA2
    Qt::Widgets
    Qt::Core
    Qt::Network
    Qt::Svg
)

if (BUILD_WITH_CODE_COVERAGE)
    target_compile_options(tes3mp-browser PRIVATE --coverage)
    target_link_libraries(tes3mp-browser gcov)
endif()

set_target_properties(tes3mp-browser PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS OFF
    AUTOMOC ON
    AUTOUIC ON
    AUTOUIC_SEARCH_PATHS "${CMAKE_SOURCE_DIR}/files/tes3mp/ui"
)
