add_undo_test(resource_scope_tests tests/resource_scope_tests.cpp)
configure_undo_host_target(resource_scope_tests)
target_sources(resource_scope_tests PRIVATE
    client/gframe/undo/room_config.cpp client/gframe/undo/runtime_paths.cpp)
target_compile_definitions(resource_scope_tests PRIVATE
    UNDO_SCOPE_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/out/tests/results/resource-scope")
set_tests_properties(resource_scope_tests PROPERTIES TIMEOUT 60)
