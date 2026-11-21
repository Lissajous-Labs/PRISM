if(APPLE)
    add_executable(prism_workloads
        BenchSupport.cpp
        WorkloadDense.cpp
        WorkloadEffects.cpp
        WorkloadMain.cpp
        WorkloadMetering.cpp
        WorkloadPaths.cpp
        WorkloadWorkbench.cpp
        WorkloadZoom.cpp
    )
    target_link_libraries(prism_workloads PRIVATE prism_controls prism_widgets prism_text
        prism_backend_metal prism_backend_cpu)
    foreach(workload W1 W2 W3 W4 W5 W6)
        add_test(NAME bench_${workload} COMMAND prism_workloads ${workload})
        set_tests_properties(bench_${workload} PROPERTIES LABELS bench SKIP_RETURN_CODE 77
            RUN_SERIAL TRUE)
    endforeach()
endif()
