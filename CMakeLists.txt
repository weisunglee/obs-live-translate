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
set_target_properties(ixwebsocket PROPERTIES COMPILE_WARNING_AS_ERROR OFF)

add_library(${CMAKE_PROJECT_NAME} MODULE
  src/plugin-main.cpp
  src/base64.cpp
  src/audio-convert.cpp
  src/filter.cpp
  src/ring-buffer.cpp
  src/live-protocol.cpp
  src/backoff.cpp
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
