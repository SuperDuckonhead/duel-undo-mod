# Actual host implementation shared by TCP admission and core transaction tests.
function(configure_undo_host_target target)
    target_sources(${target} PRIVATE
        client/gframe/undo_duel.cpp client/gframe/single_duel.cpp client/gframe/netserver.cpp client/gframe/tag_duel.cpp
        client/gframe/deck_manager.cpp client/gframe/replay.cpp client/gframe/undo_replay.cpp
        client/gframe/undo/duel_history.cpp client/gframe/undo/room_admission.cpp
        client/gframe/undo/host_bot_seat.cpp client/gframe/undo/bot_controller.cpp
        client/gframe/undo/protocol.cpp client/gframe/undo/coordinator.cpp
        client/gframe/undo/room_wire.cpp client/gframe/undo/room_restore.cpp
        client/gframe/undo/player_restore.cpp client/gframe/undo/player_visible_filter.cpp
        client/gframe/client_field_model.cpp client/gframe/client_card_model.cpp client/gframe/materials.cpp
        tests/core_test_globals.cpp)
    target_include_directories(${target} PRIVATE client/event/include client/lzma/src/liblzma/api)
    target_compile_definitions(${target} PRIVATE _IRR_STATIC_LIB_ NOMINMAX)
    target_compile_options(${target} PRIVATE -ffunction-sections -fdata-sections)
    target_link_libraries(${target} PRIVATE undo_rebuilder "${undo_libdir}/event.lib" ws2_32 iphlpapi advapi32 shell32)
    target_link_options(${target} PRIVATE -Wl,--gc-sections)
endfunction()
