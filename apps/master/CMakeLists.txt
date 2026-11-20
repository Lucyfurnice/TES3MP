set(SOURCE_FILES
    main.cpp
    MasterServer.cpp
    MasterServer.hpp
    RestServer.cpp
    RestServer.hpp
)

openmw_add_executable(tes3mp-master ${SOURCE_FILES})

target_include_directories(tes3mp-master PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${RakNet_INCLUDES}
)

target_link_libraries(tes3mp-master
    ${RakNet_LIBRARY}
    components
    Base64
    PicoSHA2
)

set_target_properties(tes3mp-master PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS OFF
)

option(BUILD_MASTER_TEST "build master server test program" OFF)

if(BUILD_MASTER_TEST)
    openmw_add_executable(ServerTest ServerTest.cpp)
    target_include_directories(ServerTest PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${RakNet_INCLUDES}
    )
    target_link_libraries(ServerTest ${RakNet_LIBRARY} components)
    set_target_properties(ServerTest PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS OFF
    )
endif()

if (UNIX)
    # Fix for not visible pthreads functions for linker with glibc 2.15
    if(NOT APPLE)
        target_link_libraries(tes3mp-master ${CMAKE_THREAD_LIBS_INIT})
        if(BUILD_MASTER_TEST)
            target_link_libraries(ServerTest ${CMAKE_THREAD_LIBS_INIT})
        endif()
    endif(NOT APPLE)
endif(UNIX)

if(WIN32)
    install(TARGETS tes3mp-master RUNTIME DESTINATION ".")
    target_link_libraries(tes3mp-master ws2_32 wsock32)
endif(WIN32)
