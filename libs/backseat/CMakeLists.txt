cmake_minimum_required(VERSION 3.18)
project(backseat)

# Backseat Driver's critique engine: watches the drive and decides what to roast.
# Pure C++ with no dependencies, so it also builds on its own for quick tests:
#   cmake -S libs/backseat -B build/backseat && cmake --build build/backseat && ctest --test-dir build/backseat

set(
  SRC
  achievements.cpp
  achievements.hpp
  clip_pacing.cpp
  clip_pacing.hpp
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
  onboarding.cpp
  onboarding.hpp
  plate_game.cpp
  plate_game.hpp
  pronunciation.cpp
  pronunciation.hpp
  pronunciation_backup.cpp
  pronunciation_backup.hpp
  pronunciation_scripts.cpp
  pronunciation_senses.hpp
  pronunciation_streets.hpp
  pronunciation_text.hpp
  pop_quiz.cpp
  pop_quiz.hpp
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
  option(BACKSEAT_SANITIZE "Build backseat and its tests with AddressSanitizer and UBSan" OFF)
  add_library(${PROJECT_NAME} ${SRC})
  # Includes are written "backseat/engine.hpp", relative to libs/.
  target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
  # Standalone builds keep the library warning-clean; the CoMaps build above keeps its own flags.
  if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wshadow -Wparentheses)
    if (NOT BACKSEAT_NO_WERROR AND NOT BACKSEAT_SANITIZE)
      target_compile_options(${PROJECT_NAME} PRIVATE -Werror)
    endif()
  endif()
  # ASan + UBSan for the library and backseat_tests (standalone only; off by default):
  #   cmake -S libs/backseat -B /tmp/bs-san -DBACKSEAT_SANITIZE=ON, build, run backseat_tests.
  # -Werror stays off here: GCC 13's <regex> trips a false -Wmaybe-uninitialized under -fsanitize.
  if (BACKSEAT_SANITIZE)
    set(BACKSEAT_SAN_FLAGS -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined)
    # PUBLIC so backseat_tests, which links the library, gets the same instrumentation and runtime.
    target_compile_options(${PROJECT_NAME} PUBLIC ${BACKSEAT_SAN_FLAGS})
    target_link_options(${PROJECT_NAME} PUBLIC ${BACKSEAT_SAN_FLAGS})
  endif()
  enable_testing()
  add_subdirectory(backseat_tests)
endif()
