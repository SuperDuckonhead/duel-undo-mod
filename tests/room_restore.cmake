add_undo_test(room_restore_tests tests/room_restore_tests.cpp)
target_sources(room_restore_tests PRIVATE client/gframe/undo/room_restore.cpp)
