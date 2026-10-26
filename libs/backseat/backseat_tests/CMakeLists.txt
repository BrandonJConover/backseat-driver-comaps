project(backseat_tests)

# Plain executable with its own tiny harness (check.hpp), so the tests also run in the
# standalone build of libs/backseat without the CoMaps testing framework.
add_executable(${PROJECT_NAME}
  check.hpp
  engine_test.cpp
  main.cpp
  navigator_test.cpp
  parity_test.cpp
  plate_game_test.cpp
  voice_command_test.cpp
  rewriter_test.cpp
)
target_link_libraries(${PROJECT_NAME} PRIVATE backseat)
target_compile_definitions(${PROJECT_NAME} PRIVATE BACKSEAT_TRACE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/parity_trace.txt")
add_test(NAME ${PROJECT_NAME} COMMAND ${PROJECT_NAME})
