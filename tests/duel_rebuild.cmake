# C3 owns only candidate reconstruction; no GUI/network/file-output sinks link in.
add_library(undo_rebuilder STATIC client/gframe/undo/rebuilder.cpp)
target_link_libraries(undo_rebuilder PUBLIC undo_driver)
add_executable(duel_rebuild_tests tests/duel_rebuild_tests.cpp tests/core_test_globals.cpp)
target_compile_features(duel_rebuild_tests PRIVATE cxx_std_17)
target_include_directories(duel_rebuild_tests PRIVATE tests client/gframe)
target_link_libraries(duel_rebuild_tests PRIVATE undo_rebuilder)
target_link_options(duel_rebuild_tests PRIVATE -static -Wl,--gc-sections)
target_compile_definitions(duel_rebuild_tests PRIVATE
  UNDO_REBUILD_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/duel"
  UNDO_REBUILD_RESULTS="${CMAKE_CURRENT_SOURCE_DIR}/out/tests/results/duel")
set(UNDO_TEST_RUNTIME_ROOT "" CACHE PATH "Read-only installed resources for C3 real-card fixtures")
if(UNDO_TEST_RUNTIME_ROOT)
  foreach(suite deterministic isolation faults)
    add_test(NAME duel_rebuild_${suite} COMMAND duel_rebuild_tests --runtime-root "${UNDO_TEST_RUNTIME_ROOT}" --suite ${suite})
    set_tests_properties(duel_rebuild_${suite} PROPERTIES TIMEOUT 180)
  endforeach()
endif()
