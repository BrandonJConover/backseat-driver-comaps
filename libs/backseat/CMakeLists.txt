cmake_minimum_required(VERSION 3.18)
project(backseat)

# Backseat Driver's critique engine: watches the drive and decides what to roast.
# Pure C++ with no dependencies, so it also builds on its own for quick tests:
#   cmake -S libs/backseat -B build/backseat && cmake --build build/backseat && ctest --test-dir build/backseat

set(
  SRC
  achievements.cpp
  achievements.hpp
  debug_log.cpp
  debug_log.hpp
  engine.cpp
  engine.hpp
  history.cpp
  history.hpp
  indoors.cpp
  indoors.hpp
  lines.cpp
  lines.hpp
  lines_data.cpp
  lines_data.hpp
  mileage.cpp
  mileage.hpp
  navigator.cpp
  navigator.hpp
  plate_game.cpp
  plate_game.hpp
  quiet_bits.cpp
  quiet_bits.hpp
  report.cpp
  report.hpp
  rewriter.cpp
  rewriter.hpp
  road_bits.cpp
  road_bits.hpp
  route_order.cpp
  route_order.hpp
  route_sketch.cpp
  route_sketch.hpp
  running_bits.cpp
  running_bits.hpp
  rundown.cpp
  rundown.hpp
  stop_go.cpp
  stop_go.hpp
  twenty_questions.cpp
  twenty_questions.hpp
  types.cpp
  types.hpp
  units.cpp
  units.hpp
  voice_command.cpp
  voice_command.hpp
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
