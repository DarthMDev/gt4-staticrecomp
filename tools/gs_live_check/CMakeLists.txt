# TASK-036: window and lifecycle check for a GS process (hidden parent window, .gscap replay). Not a CTest: it needs
# the pinned gt4-gs package and a capture from private/ (see docs/GS-LIVE.md).
add_executable(gs_live_check gs_live_check.cpp)
target_link_libraries(gs_live_check PRIVATE gt4_gs_stream)
target_compile_definitions(gs_live_check PRIVATE _CRT_SECURE_NO_WARNINGS UNICODE _UNICODE)
target_compile_options(gs_live_check PRIVATE /W4)
target_link_options(gs_live_check PRIVATE /ENTRY:wmainCRTStartup)
