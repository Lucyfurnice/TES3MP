# Once found, defines:
#  LuaJit_FOUND
#  LuaJit_INCLUDE_DIR
#  LuaJit_LIBRARIES

include(LibFindMacros)

libfind_pkg_detect(LuaJit luajit
        FIND_PATH luajit.h lua.h
            PATHS "D:/Users/lucyfurnice/Desktop/Lua-5.5.1/Lua-5.5.1"
            PATH_SUFFIXES luajit luajit-2.1
        FIND_LIBRARY luajit-5.1 luajit lua55
            PATHS "D:/Users/lucyfurnice/Desktop/Lua-5.5.1/x64/Release/lib"
                  "D:/Users/lucyfurnice/Desktop/Lua-5.5.1/x64/Debug/lib"
        )

libfind_process(LuaJit)

