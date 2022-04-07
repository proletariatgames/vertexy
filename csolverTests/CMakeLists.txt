cmake_minimum_required (VERSION 3.15)
set(CMAKE_DEBUG_POSTFIX -debug)
set(CMAKE_DEVELOPMENT_POSTFIX -dev)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin/${CONFIG}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/lib/${CONFIG}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/lib/${CONFIG}")

set (CMAKE_MODULE_PATH "${CMAKE_MODULE_PATH};${CMAKE_CURRENT_SOURCE_DIR}/../thirdParty/eastl/scripts/CMake")

project ("CSolverTestsLib")

file(GLOB CSTEST_SOURCES
    "src/private/*.cpp"
	"../thirdParty/eastl/doc/EASTL.natvis"  # debugging visualization
	"../csolver/csolver.natvis"  # debugging visualization
)

file(GLOB_RECURSE CSTEST_HEADERS "*.h")

add_subdirectory(../thirdParty/EAMain ./thirdParty/EAMain)
add_subdirectory(../thirdParty/EATest ./thirdParty/EATest)
add_subdirectory(../thirdParty/EAThread ./thirdParty/EAThread)
add_subdirectory(../thirdParty/EAStdC ./thirdParty/EAStdC)

# MSVC: disable some warnings
target_compile_options(EAMain PRIVATE /wd4100)
target_compile_options(EAStdC PRIVATE /wd4100)

# Add source to this project's executable.
add_library (CSolverTestsLib ${CSTEST_SOURCES} ${CSTEST_HEADERS})
set_target_properties(CSolverTestsLib PROPERTIES DEBUG_POSTFIX ${CMAKE_DEBUG_POSTFIX})
target_compile_definitions(CSolverTestsLib PUBLIC $<$<CONFIG:Debug>:EA_ASSERT_ENABLED>)
target_compile_definitions(CSolverTestsLib PUBLIC CS_SANITY_CHECKS=$<CONFIG:Debug>)
# target_compile_definitions(CSolverTestsLib PUBLIC EASTL_SIZE_T_32BIT=1)
target_include_directories(CSolverTestsLib PRIVATE "src/private")
target_include_directories(CSolverTestsLib PUBLIC "src/public")
target_link_libraries(CSolverTestsLib PRIVATE CSolverLib EATest)