cmake_minimum_required (VERSION 3.15)
set(CMAKE_DEBUG_POSTFIX -debug)
set(CMAKE_DEVELOPMENT_POSTFIX -dev)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin/${CONFIG}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/lib/${CONFIG}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/lib/${CONFIG}")

set (CMAKE_MODULE_PATH "${CMAKE_MODULE_PATH};${CMAKE_CURRENT_SOURCE_DIR}/../thirdParty/eastl/scripts/CMake")

project ("CSolverLib")

file(GLOB CS_SOURCES
	"src/private/constraints/*.cpp"
	"src/private/decision/*.cpp"
	"src/private/ds/*.cpp"
	"src/private/learning/*.cpp"
	"src/private/restart/*.cpp"
	"src/private/util/*.cpp"
	"src/private/topology/*.cpp"
	"src/private/variable/*.cpp"
	"src/private/*.cpp"
	"src/*.cpp"
	"../thirdParty/eastl/doc/EASTL.natvis"  # debugging visualization
	"./csolver.natvis"  # debugging visualization
)

add_subdirectory(../thirdParty/EABase ./thirdParty/EABase)
add_subdirectory(../thirdParty/eastl ./thirdParty/eastl)
add_subdirectory(../thirdParty/EAAssert ./thirdParty/EAAssert)

file(GLOB_RECURSE CS_HEADERS "*.h")

# MSVC: disable warning in eaassert.cpp about _vsnprintf
target_compile_definitions(EAAssert PRIVATE _CRT_SECURE_NO_WARNINGS)

add_library(CSolverLib ${CS_SOURCES} ${CS_HEADERS})
target_compile_definitions(CSolverLib PUBLIC CS_SANITY_CHECKS=$<CONFIG:Debug>)
target_link_libraries(CSolverLib EASTL EAAssert)
target_include_directories(CSolverLib PRIVATE "src/private")
target_include_directories(CSolverLib PUBLIC "src/public")