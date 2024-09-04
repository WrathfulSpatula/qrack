option (ENABLE_PTHREAD "Enable pthread parallelism (for CPU)" ON)
option (ENABLE_QBDT_CPU_PARALLEL "Enable QBdt parallelism (for CPU)" ON)
option (ENABLE_QUNIT_CPU_PARALLEL "Make QEngineCPU partially async, so QUnit can parallelize over it (may exceed shell threading limits)" ON)
if (NOT ENABLE_PTHREAD)
    set(ENABLE_QBDT_CPU_PARALLEL OFF)
    set(ENABLE_QUNIT_CPU_PARALLEL OFF)
endif (NOT ENABLE_PTHREAD)
