cmake_minimum_required(VERSION 3.21)
project(BattleSummaries VERSION 1.0.0 LANGUAGES CXX)  # the mod's version: the DLL reports it, the SkyrimNet manifest repeats it

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(COMMONLIB_DIR "$ENV{COMMONLIB_DIR}" CACHE PATH "CommonLibVR (alandtse, ng branch) checkout")
if(NOT COMMONLIB_DIR)
	message(FATAL_ERROR "Set COMMONLIB_DIR to a CommonLibVR (alandtse, ng branch) checkout (build.ps1 passes it from local.env)")
endif()
add_subdirectory(${COMMONLIB_DIR} CommonLibSSE EXCLUDE_FROM_ALL)

find_package(nlohmann_json CONFIG REQUIRED)
find_path(SIMPLEINI_INCLUDE_DIRS "SimpleIni.h")

# The battle model and its narrative: no game types, shared by the plugin and the offline tests.
set(CORE_SOURCES src/Core.cpp src/Narrative.cpp)

add_library(BattleSummaries SHARED ${CORE_SOURCES} src/main.cpp src/Settings.cpp src/Effects.cpp src/Tracker.cpp src/Perf.cpp src/SkyrimNet.cpp src/Papyrus.cpp)
target_link_libraries(BattleSummaries PRIVATE CommonLibSSE::CommonLibSSE nlohmann_json::nlohmann_json)
target_include_directories(BattleSummaries PRIVATE ${SIMPLEINI_INCLUDE_DIRS})
target_precompile_headers(BattleSummaries PRIVATE <SKSE/SKSE.h> <RE/Skyrim.h>)
target_compile_definitions(BattleSummaries PRIVATE _UNICODE UNICODE NOMINMAX
	BSM_VERSION_MAJOR=${PROJECT_VERSION_MAJOR} BSM_VERSION_MINOR=${PROJECT_VERSION_MINOR} BSM_VERSION_PATCH=${PROJECT_VERSION_PATCH})
set_target_properties(BattleSummaries PROPERTIES OUTPUT_NAME "BattleSummaries")

# Offline tests: build.ps1 builds and runs them before the DLL counts as built.
add_executable(BattleSummariesTests ${CORE_SOURCES} tests/tests.cpp)
