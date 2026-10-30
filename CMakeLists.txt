cmake_minimum_required(VERSION 3.21)
project(NotificationTweaks VERSION 1.3.0 LANGUAGES CXX)

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
        src/SwfFont.cpp
        src/IconFont.cpp
        src/CenterNotifications.cpp
)

find_package(ZLIB REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE ZLIB::ZLIB)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)
target_compile_options(${PROJECT_NAME} PRIVATE /Zc:preprocessor /permissive- /W4 /utf-8)
target_compile_definitions(${PROJECT_NAME} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(${PROJECT_NAME} PRIVATE src)
target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE external/skse-menu-framework)
target_precompile_headers(${PROJECT_NAME} PRIVATE src/PCH.h)

# Unit tests for the pure modules (no CommonLibSSE); tools\build.ps1 runs them.
add_executable(NotificationTweaksTests tests/Tests.cpp src/Merge.cpp src/Markup.cpp src/SwfFont.cpp)
target_link_libraries(NotificationTweaksTests PRIVATE ZLIB::ZLIB)
target_compile_features(NotificationTweaksTests PRIVATE cxx_std_23)
target_compile_options(NotificationTweaksTests PRIVATE /Zc:preprocessor /permissive- /W4 /utf-8)
target_compile_definitions(NotificationTweaksTests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(NotificationTweaksTests PRIVATE src)
target_precompile_headers(NotificationTweaksTests PRIVATE tests/TestPCH.h)
