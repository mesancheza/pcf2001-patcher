# Tracing proxy and crash recorder for the graphics backend interface. Built as dcogl.dll (see backend_trace.cpp).
add_library(backend_trace SHARED backend_trace.cpp heap_tracker.cpp recorder.cpp trace_log.cpp dcogl.def)
set_target_properties(backend_trace PROPERTIES OUTPUT_NAME dcogl)
target_compile_definitions(backend_trace PRIVATE _CRT_SECURE_NO_WARNINGS)
