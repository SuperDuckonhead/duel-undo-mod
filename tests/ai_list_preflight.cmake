add_executable(ai_list_preflight_tests tests/ai_list_preflight_tests.cpp client/gframe/undo/bot_controller.cpp client/gframe/undo/protocol.cpp tests/core_test_globals.cpp)
target_compile_features(ai_list_preflight_tests PRIVATE cxx_std_17)
target_include_directories(ai_list_preflight_tests PRIVATE tests client/gframe)
target_link_libraries(ai_list_preflight_tests PRIVATE undo_resources advapi32 shell32)
target_link_options(ai_list_preflight_tests PRIVATE -static -Wl,--gc-sections)
# The PowerShell driver freezes the actual list and supplies a unique output path.
# Deliberately not registered as a generic ctest: this is an explicit 62-process-list preflight.