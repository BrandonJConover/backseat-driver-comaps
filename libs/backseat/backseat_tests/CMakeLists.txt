project(backseat_tests)

# Plain executable with its own tiny harness (check.hpp), so the tests also run in the
# standalone build of libs/backseat without the CoMaps testing framework.
add_executable(${PROJECT_NAME}
  check.hpp
  engine_test.cpp
  indoors_test.cpp
  main.cpp
  mileage_test.cpp
  navigator_test.cpp
  onboarding_test.cpp
  parity_test.cpp
  plate_game_test.cpp
  pop_quiz_test.cpp
  pronunciation_test.cpp
  quiet_bits_test.cpp
  road_bits_test.cpp
  voice_command_test.cpp
  rewriter_test.cpp
  route_order_test.cpp
  running_bits_test.cpp
  route_sketch_test.cpp
  sim_drive.hpp
  soak_test.cpp
  debug_log_test.cpp
  lines_test.cpp
  stop_go_test.cpp
  twenty_questions_test.cpp
)
target_link_libraries(${PROJECT_NAME} PRIVATE backseat)
target_compile_definitions(${PROJECT_NAME} PRIVATE
  BACKSEAT_TRACE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/parity_trace.txt"
  BACKSEAT_SPOKEN_RANGES_PATH="${CMAKE_CURRENT_SOURCE_DIR}/spoken_ranges.json"
  BACKSEAT_SPOKEN_ROADS_PATH="${CMAKE_CURRENT_SOURCE_DIR}/spoken_roads.json"
  BACKSEAT_SPOKEN_ADDRESSES_PATH="${CMAKE_CURRENT_SOURCE_DIR}/spoken_addresses.json"
  BACKSEAT_SPOKEN_TIMES_PATH="${CMAKE_CURRENT_SOURCE_DIR}/spoken_times.json"
  BACKSEAT_SPOKEN_YEARS_PATH="${CMAKE_CURRENT_SOURCE_DIR}/spoken_years.json"
  BACKSEAT_SPOKEN_CYRILLIC_PATH="${CMAKE_CURRENT_SOURCE_DIR}/spoken_cyrillic.json")
add_test(NAME ${PROJECT_NAME} COMMAND ${PROJECT_NAME})
