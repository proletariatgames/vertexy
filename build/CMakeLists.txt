
cmake_minimum_required (VERSION 3.15)

if(CMAKE_CONFIGURATION_TYPES)
  list(APPEND CMAKE_CONFIGURATION_TYPES Development)
  list(REMOVE_ITEM CMAKE_CONFIGURATION_TYPES RelWithDebInfo)
  list(REMOVE_ITEM CMAKE_CONFIGURATION_TYPES MinSizeRel)
  list(REMOVE_DUPLICATES CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_CONFIGURATION_TYPES "${CMAKE_CONFIGURATION_TYPES}" CACHE STRING "Build configs" FORCE)
else()
    list(APPEND CMAKE_CONFIGURATION_TYPES Debug)
    list(APPEND CMAKE_CONFIGURATION_TYPES Development)
    list(APPEND CMAKE_CONFIGURATION_TYPES Release)
    set(CMAKE_CONFIGURATION_TYPES "${CMAKE_CONFIGURATION_TYPES}" CACHE STRING "Build configs" FORCE)
endif()

set(CMAKE_DEBUG_POSTFIX -debug)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(CMAKE_USER_MAKE_RULES_OVERRIDE_CXX ${CMAKE_CURRENT_SOURCE_DIR}/cxx_flag_overrides.cmake)

project ("ConstraintSystem")
cmake_policy(SET CMP0077 NEW)
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)

add_subdirectory(../csolver ./csolver)
add_subdirectory(../csolverTests ./csolverTests)
add_subdirectory(../testHarness ./testHarness)
