cmake_minimum_required(VERSION 3.5)

project(zng2xtr LANGUAGES CXX C)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

SET(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin)

set(PROJECT_SOURCES 
    xtr.cpp
    xtr.h
    zng2cmd.h
    zng2common.h
    zng2xtr.cpp
    types.h
)

if(WIN32)
    add_executable(zng2xtr
        lzss.c
        res/zng2xtr.rc
        ${CMAKE_CURRENT_SOURCE_DIR}/${PROJECT_SOURCES}
    )
else()
    add_executable(zng2xtr
        lzss.c
        ${PROJECT_SOURCES}
    )
endif()
