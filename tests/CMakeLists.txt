FetchContent_Declare(Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG v3.5.2)
FetchContent_MakeAvailable(Catch2)

add_executable(unit-tests
  test-main.cpp
  test-smoke.cpp
)
target_include_directories(unit-tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(unit-tests PRIVATE Catch2::Catch2)

include(CTest)
include(Catch)
catch_discover_tests(unit-tests)
