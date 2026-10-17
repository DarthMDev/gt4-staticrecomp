# TASK-035: GS stream recorder / null consumer (MIT). Started by the game through GT4_GS_PROCESS for tests.
add_executable(gs_stream_record gs_stream_record.cpp)
target_link_libraries(gs_stream_record PRIVATE gt4_gs_stream)
target_compile_definitions(gs_stream_record PRIVATE _CRT_SECURE_NO_WARNINGS)
target_compile_options(gs_stream_record PRIVATE /W4)
