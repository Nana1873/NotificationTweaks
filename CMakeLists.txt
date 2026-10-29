cmake_minimum_required(VERSION 3.21)
project(NotificationTweaks VERSION 1.1.0 LANGUAGES CXX)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)

add_subdirectory(extern/commonlibsse-ng commonlib)
if(NOT COMMAND add_commonlibsse_plugin)
    include(extern/commonlibsse-ng/cmake/CommonLibSSE.cmake)
endif()

add_commonlibsse_plugin(${PROJECT_NAME}
    AUTHOR "jmach"
    VERSION ${PROJECT_VERSION}
    SOURCES
        src/main.cpp
        src/Settings.cpp
        src/Capture.cpp
        src/Merge.cpp
        src/Markup.cpp
        src/Overlay.cpp
        src/MenuIntegration.cpp
        src/Translation.cpp
)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)
target_compile_options(${PROJECT_NAME} PRIVATE /Zc:preprocessor /permissive- /W4 /utf-8)
target_compile_definitions(${PROJECT_NAME} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(${PROJECT_NAME} PRIVATE src)
target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE external/skse-menu-framework)
target_precompile_headers(${PROJECT_NAME} PRIVATE src/PCH.h)
