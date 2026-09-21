cmake_minimum_required(VERSION 3.20)
project(RVCRealtimeRecoveryTests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
enable_testing()
find_package(Python3 REQUIRED COMPONENTS Interpreter)

add_executable(rvc-ring-tests ring_tests.cpp)
target_include_directories(rvc-ring-tests PRIVATE ../src)
add_test(NAME ring-recovery COMMAND rvc-ring-tests)
add_test(NAME python-reset COMMAND ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_SOURCE_DIR}/test_worker_reset.py)

if(WIN32)
    add_executable(rvc-recovery-tests bridge_tests.cpp ../src/WorkerClient.cpp)
    target_include_directories(rvc-recovery-tests PRIVATE .. ../src)
    target_compile_definitions(rvc-recovery-tests PRIVATE UNICODE _UNICODE NOMINMAX)
    # This directory is deliberately separate from the production smoke worker.
    add_custom_command(TARGET rvc-recovery-tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:rvc-recovery-tests>/RVCRealtime.resources/worker"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/fake_worker.py"
            "$<TARGET_FILE_DIR:rvc-recovery-tests>/RVCRealtime.resources/worker/rvc_worker.py")
    add_test(NAME bridge-recovery COMMAND rvc-recovery-tests
        ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../worker)
    set_tests_properties(bridge-recovery PROPERTIES TIMEOUT 30)
endif()
