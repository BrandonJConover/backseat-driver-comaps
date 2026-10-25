cmake_minimum_required(VERSION 3.18)
project(backseat)

# Backseat Driver's critique engine: watches the drive and decides what to roast.
# Pure C++ with no dependencies, so it also builds on its own for quick tests:
#   cmake -S libs/backseat -B build/backseat && cmake --build build/backseat && ctest --test-dir build/backseat

set(
  SRC
  engine.cpp
  engine.hpp
  history.cpp
  history.hpp
  lines.cpp
  lines.hpp
  lines_data.cpp
  lines_data.hpp
  navigator.cpp
  navigator.hpp
  report.cpp
  report.hpp
  rundown.cpp
  rundown.hpp
  types.cpp
  types.hpp
  units.cpp
  units.hpp
)

if (COMMAND omim_add_library)
  omim_add_library(${PROJECT_NAME} ${SRC})
  omim_add_test_subdirectory(backseat_tests)
else()
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  add_library(${PROJECT_NAME} ${SRC})
  # Includes are written "backseat/engine.hpp", relative to libs/.
  target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
  enable_testing()
  add_subdirectory(backseat_tests)
endif()
