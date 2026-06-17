cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(compilerconfig)
include(defaults)
include(helpers)

option(BUILD_TESTS "Build unit tests" ON)

find_package(libobs REQUIRED)

include(FetchContent)

FetchContent_Declare(json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3)
FetchContent_MakeAvailable(json)

set(USE_TLS ON CACHE BOOL "" FORCE)
set(USE_MBED_TLS ON CACHE BOOL "" FORCE)
FetchContent_Declare(ixwebsocket
  GIT_REPOSITORY https://github.com/machinezone/IXWebSocket.git
  GIT_TAG v11.4.5)
FetchContent_MakeAvailable(ixwebsocket)
# Don't let the third-party IXWebSocket sources fail the build on their own
# warnings. COMPILE_WARNING_AS_ERROR covers CMake's mechanism; the macOS Xcode
# generator applies -Werror via GCC_TREAT_WARNINGS_AS_ERRORS, so disable that too.
set_target_properties(ixwebsocket PROPERTIES
  COMPILE_WARNING_AS_ERROR OFF
  XCODE_ATTRIBUTE_GCC_TREAT_WARNINGS_AS_ERRORS NO)
if(NOT MSVC)
  target_compile_options(ixwebsocket PRIVATE -Wno-error)
endif()
# IXWebSocket v11.4.5 leaks a Windows-only "bcrypt" link dependency; drop it on
# platforms where that library does not exist.
if(NOT WIN32)
  foreach(_prop INTERFACE_LINK_LIBRARIES LINK_LIBRARIES)
    get_target_property(_ixws_libs ixwebsocket ${_prop})
    if(_ixws_libs)
      list(REMOVE_ITEM _ixws_libs bcrypt)
      set_target_properties(ixwebsocket PROPERTIES ${_prop} "${_ixws_libs}")
    endif()
  endforeach()
endif()

add_library(${CMAKE_PROJECT_NAME} MODULE
  src/plugin-main.cpp
  src/base64.cpp
  src/audio-convert.cpp
  src/audio-pacing.cpp
  src/filter.cpp
  src/ring-buffer.cpp
  src/live-protocol.cpp
  src/backoff.cpp
  src/source.cpp
  src/translation-session.cpp
)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE
  OBS::libobs
  ixwebsocket
  nlohmann_json::nlohmann_json
  bcrypt
)
set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})

if(BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
