add_undo_test(duel_timer_callback_tests tests/duel_timer_callback_tests.cpp)
configure_undo_host_target(duel_timer_callback_tests)
# The test includes netserver.cpp to reach the actual anonymous callback.
get_target_property(timer_callback_sources duel_timer_callback_tests SOURCES)
list(REMOVE_ITEM timer_callback_sources client/gframe/netserver.cpp)
set_property(TARGET duel_timer_callback_tests PROPERTY SOURCES "${timer_callback_sources}")
set_tests_properties(duel_timer_callback_tests PROPERTIES TIMEOUT 15)
