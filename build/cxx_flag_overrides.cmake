# TODO: Non-MSVC builds

# Incremental linking for Debug/Development
# Link-Time Code Generation for Release
# All builds generate PDBs
set(CMAKE_EXE_LINKER_FLAGS_DEBUG "/Incremental /debug")
set(CMAKE_EXE_LINKER_FLAGS_DEVELOPMENT "/Incremental /debug")
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "/Incremental:NO /debug /LTCG")
set(CMAKE_SHARED_LINKER_FLAGS_DEBUG "/Incremental /debug")
set(CMAKE_SHARED_LINKER_FLAGS_DEVELOPMENT "/Incremental /debug")
set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "/Incremental:NO /debug /LTCG")

# No inlining in debug builds
set(CMAKE_CXX_FLAGS_DEBUG "/Zi /Ob0 /Od /RTC1 /DUNICODE /D_UNICODE /DEA_ASSERT_ENABLED /D_HAS_EXCEPTIONS=0")
set(CMAKE_C_FLAGS_DEBUG "/Zi /Ob0 /Od /RTC1 /DUNICODE /D_UNICODE /DEA_ASSERT_ENABLED /D_HAS_EXCEPTIONS=0")

# Normal inlining in development builds
set(CMAKE_CXX_FLAGS_DEVELOPMENT "/Zo /Zi /Ob2 /O1 /DUNICODE /D_UNICODE /DEA_ASSERT_ENABLED /D_HAS_EXCEPTIONS=0")
set(CMAKE_C_FLAGS_DEVELOPMENT "/Zo /Zi /Ob2 /O1 /DUNICODE /D_UNICODE /DEA_ASSERT_ENABLED /D_HAS_EXCEPTIONS=0")

# /Ob3: aggressive inlining (requires VS2019+)
set(CMAKE_CXX_FLAGS_RELEASE "/O2 /GL /Ob3 /Zo /Zi /DUNICODE /D_UNICODE /DNDEBUG /D_HAS_EXCEPTIONS=0")
set(CMAKE_C_FLAGS_RELEASE "/O2 /GL /Ob3 /Zo /Zi /DUNICODE /D_UNICODE /DNDEBUG /D_HAS_EXCEPTIONS=0")

if(CMAKE_CXX_FLAGS MATCHES "/EHsc ")
  string(REPLACE "/EHsc" "/EHs-c-" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
  message(STATUS "CMAKE_CXX_FLAGS matches /EHsc before end of string -- replaced...")
  message(STATUS "")
endif()

if(CMAKE_CXX_FLAGS MATCHES "/EHsc$")
  string(REPLACE "/EHsc" "/EHs-c-" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
  message(STATUS "CMAKE_CXX_FLAGS matches /EHsc at end of string -- replaced...")
  message(STATUS "")
endif()