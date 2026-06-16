FetchContent_Declare(Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG v3.5.2)
FetchContent_MakeAvailable(Catch2)

add_executable(unit-tests
  test-main.cpp
  test-audio-convert.cpp
  test-base64.cpp
  test-ring-buffer.cpp
  test-smoke.cpp
  ${CMAKE_SOURCE_DIR}/src/audio-convert.cpp
  ${CMAKE_SOURCE_DIR}/src/base64.cpp
  ${CMAKE_SOURCE_DIR}/src/ring-buffer.cpp
)
target_include_directories(unit-tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(unit-tests PRIVATE Catch2::Catch2)

include(CTest)
include(Catch)
catch_discover_tests(unit-tests)
