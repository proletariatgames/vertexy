cmake_minimum_required (VERSION 3.15)
set(CMAKE_DEBUG_POSTFIX -debug)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ../../Intermediate/CMake/bin/${CONFIG})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ../../Intermediate/CMake/lib/${CONFIG})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ../../Intermediate/CMake/lib/${CONFIG})

project ("CSolverTestsLib")

file(GLOB CSTEST_SOURCES
    "Private/*.cpp"
	"../ThirdParty/eastl/doc/EASTL.natvis"  # debugging visualization
	"../../csolver.natvis"  # debugging visualization
)

file(GLOB_RECURSE CSTEST_HEADERS "*.h")

# Add source to this project's executable.
add_library (CSolverTestsLib ${CSTEST_SOURCES} ${CSTEST_HEADERS})
set_target_properties(CSolverTestsLib PROPERTIES DEBUG_POSTFIX ${CMAKE_DEBUG_POSTFIX})
target_compile_definitions(CSolverTestsLib PUBLIC $<$<CONFIG:Debug>:EA_ASSERT_ENABLED>)
target_compile_definitions(CSolverTestsLib PUBLIC CS_SANITY_CHECKS=$<CONFIG:Debug>)
# target_compile_definitions(CSolverTestsLib PUBLIC EASTL_SIZE_T_32BIT=1)
target_include_directories(CSolverTestsLib PRIVATE "Private")
target_include_directories(CSolverTestsLib PUBLIC "Public")
target_link_libraries(CSolverTestsLib PRIVATE CSolverLib EATest)