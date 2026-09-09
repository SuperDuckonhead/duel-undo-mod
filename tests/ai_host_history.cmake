add_executable(ai_host_history_tests tests/ai_host_history_tests.cpp)
target_compile_features(ai_host_history_tests PRIVATE cxx_std_17)
target_include_directories(ai_host_history_tests PRIVATE tests client/gframe)
target_link_options(ai_host_history_tests PRIVATE -static)
configure_undo_host_target(ai_host_history_tests)
# Explicit driver binds all installed list rows to isolated case processes.