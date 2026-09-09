# Explicit R2 benchmark; never part of the ordinary quick CTest run.
add_executable(undo_measure tests/undo_measure.cpp tests/core_test_globals.cpp)
target_compile_features(undo_measure PRIVATE cxx_std_17)
target_include_directories(undo_measure PRIVATE tests client/gframe)
target_link_libraries(undo_measure PRIVATE undo_rebuilder psapi)
target_link_options(undo_measure PRIVATE -static -Wl,--gc-sections)
