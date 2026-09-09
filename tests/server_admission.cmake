add_undo_test(server_admission_tests tests/server_admission_tests.cpp)
target_sources(server_admission_tests PRIVATE
    client/gframe/netserver.cpp client/gframe/single_duel.cpp client/gframe/tag_duel.cpp
    client/gframe/deck_manager.cpp client/gframe/replay.cpp client/gframe/undo_replay.cpp
    client/gframe/undo/duel_history.cpp client/gframe/undo/room_admission.cpp
    client/gframe/undo/protocol.cpp client/gframe/undo/player_visible_filter.cpp
    tests/core_test_globals.cpp)
target_include_directories(server_admission_tests PRIVATE client/event/include client/lzma/src/liblzma/api)
target_compile_definitions(server_admission_tests PRIVATE _IRR_STATIC_LIB_ NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(server_admission_tests PRIVATE -ffunction-sections -fdata-sections)
target_link_libraries(server_admission_tests PRIVATE undo_rebuilder "${undo_libdir}/event.lib" ws2_32 iphlpapi)
target_link_options(server_admission_tests PRIVATE -Wl,--gc-sections)
set_tests_properties(server_admission_tests PROPERTIES TIMEOUT 60)
