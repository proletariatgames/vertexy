cmake_minimum_required (VERSION 3.15)
set(CMAKE_DEBUG_POSTFIX -debug)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ../../Intermediate/CMake/bin/${CONFIG})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ../../Intermediate/CMake/lib/${CONFIG})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ../../Intermediate/CMake/lib/${CONFIG})

project ("CSolverLib")

file(GLOB CS_SOURCES
	"Private/constraints/*.cpp"
	"Private/decision/*.cpp"
	"Private/ds/*.cpp"
	"Private/learning/*.cpp"
	"Private/restart/*.cpp"
	"Private/util/*.cpp"
	"Private/topology/*.cpp"
	"Private/variable/*.cpp"
	"Private/*.cpp"
	"*.cpp"
	"../ThirdParty/eastl/doc/EASTL.natvis"  # debugging visualization
	"../../csolver.natvis"  # debugging visualization
)

file(GLOB_RECURSE CS_HEADERS "*.h")

add_library(CSolverLib ${CS_SOURCES} ${CS_HEADERS})
target_compile_definitions(CSolverLib PUBLIC CS_SANITY_CHECKS=$<CONFIG:Debug>)
# target_compile_definitions(CSolverLib PUBLIC EASTL_SIZE_T_32BIT=1)
target_link_libraries(CSolverLib EASTL EAAssert)
target_include_directories(CSolverLib PRIVATE "Private")
target_include_directories(CSolverLib PUBLIC "Public")