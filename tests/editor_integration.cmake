add_undo_test(editor_input_tests tests/editor_input_tests.cpp)
# Requires a freshly built product and tools/Prepare-SmokeRuntime.ps1 assets.
# This is actual in-process Irrlicht event integration, not visual GUI approval.
option(UNDO_RUN_EDITOR_INTEGRATION "Run the actual initialized client editor integration" OFF)
if(UNDO_RUN_EDITOR_INTEGRATION AND WIN32)
    add_test(NAME editor_client_integration
        COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass -File
        "${CMAKE_SOURCE_DIR}/tools/Test-EditorIntegration.ps1" -Configuration Release)
    set_tests_properties(editor_client_integration PROPERTIES LABELS "actual-client" TIMEOUT 120)
endif()