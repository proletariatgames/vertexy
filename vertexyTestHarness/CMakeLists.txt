cmake_minimum_required (VERSION 3.15)
set(CMAKE_DEBUG_POSTFIX -debug)
set(CMAKE_DEVELOPMENT_POSTFIX -dev)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin/${CONFIG}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/lib/${CONFIG}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/lib/${CONFIG}")

project ("VertexyTestHarness")

file(GLOB TESTHARNESS_SOURCES
    "src/*.cpp"
    "../thirdParty/eastl/doc/EASTL.natvis"  # debugging visualization
    "../vertexy/vertexy.natvis"  # debugging visualization
)

# Add source to this project's executable.
add_executable (VertexyTestHarness ${TESTHARNESS_SOURCES})
set_target_properties(VertexyTestHarness PROPERTIES DEBUG_POSTFIX ${CMAKE_DEBUG_POSTFIX})
target_compile_definitions(VertexyTestHarness PUBLIC $<$<CONFIG:Debug>:EA_ASSERT_ENABLED>)
target_compile_definitions(VertexyTestHarness PUBLIC VERTEXY_SANITY_CHECKS=$<CONFIG:Debug>)
# target_compile_definitions(VertexyTestHarness PUBLIC EASTL_SIZE_T_32BIT=1)

target_link_libraries(VertexyTestHarness PRIVATE VertexyLib VertexyTestsLib EATest)