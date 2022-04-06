cmake_minimum_required (VERSION 3.15)
set(CMAKE_DEBUG_POSTFIX -debug)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ../../Intermediate/CMake/bin/${CONFIG})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ../../Intermediate/CMake/lib/${CONFIG})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ../../Intermediate/CMake/lib/${CONFIG})

project ("TestRunner")

file(GLOB TESTRUNNER_SOURCES
    "./*.cpp"
    "../ThirdParty/eastl/doc/EASTL.natvis"  # debugging visualization
    "../../csolver.natvis"  # debugging visualization
)

# Add source to this project's executable.
add_executable (TestRunner ${TESTRUNNER_SOURCES})
set_target_properties(TestRunner PROPERTIES DEBUG_POSTFIX ${CMAKE_DEBUG_POSTFIX})
target_compile_definitions(TestRunner PUBLIC $<$<CONFIG:Debug>:EA_ASSERT_ENABLED>)
target_compile_definitions(TestRunner PUBLIC CS_SANITY_CHECKS=$<CONFIG:Debug>)
# target_compile_definitions(TestRunner PUBLIC EASTL_SIZE_T_32BIT=1)

target_link_libraries(TestRunner PRIVATE CSolverLib CSolverTestsLib EATest)