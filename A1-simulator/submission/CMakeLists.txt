cmake_minimum_required(VERSION 3.28)
project(a1_simulator LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

set(FETCHCONTENT_FULLY_DISCONNECTED ON CACHE BOOL "" FORCE)
set(FETCHCONTENT_SOURCE_DIR_FMT
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/fmt" CACHE PATH "" FORCE)
set(FETCHCONTENT_SOURCE_DIR_BOOST_REGEX
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/boost-regex" CACHE PATH "" FORCE)
set(FETCHCONTENT_SOURCE_DIR_TOMLPLUSPLUS
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/tomlplusplus" CACHE PATH "" FORCE)

set(SLANG_INCLUDE_TOOLS OFF CACHE BOOL "" FORCE)
set(SLANG_INCLUDE_TESTS OFF CACHE BOOL "" FORCE)
set(SLANG_INCLUDE_DOCS OFF CACHE BOOL "" FORCE)
set(SLANG_INCLUDE_PYLIB OFF CACHE BOOL "" FORCE)
set(SLANG_INCLUDE_PYTHON_DOCS OFF CACHE BOOL "" FORCE)
set(SLANG_INCLUDE_INSTALL OFF CACHE BOOL "" FORCE)
set(SLANG_INCLUDE_THREADTEST OFF CACHE BOOL "" FORCE)
set(SLANG_INCLUDE_UVM_TEST OFF CACHE BOOL "" FORCE)
set(SLANG_INCLUDE_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(SLANG_INCLUDE_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SLANG_USE_MIMALLOC OFF CACHE BOOL "" FORCE)

add_subdirectory(third_party/slang third_party/slang-build EXCLUDE_FROM_ALL)

add_library(a1_frontend STATIC
  src/frontend/probe_report.cpp
  src/frontend/ast_probe.cpp
  src/frontend/frontend.cpp)
target_include_directories(a1_frontend PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_include_directories(a1_frontend PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

add_library(a1_value STATIC runtime/value.cpp)
target_include_directories(a1_value PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/src")

add_library(a1_ir STATIC src/ir/model_ir.cpp)
target_include_directories(a1_ir PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(a1_ir PUBLIC a1_value)

add_library(a1_continuous_evaluator STATIC runtime/continuous_evaluator.cpp)
target_include_directories(a1_continuous_evaluator PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(a1_continuous_evaluator PUBLIC a1_ir)

add_library(a1_edge STATIC runtime/edge.cpp)
target_include_directories(a1_edge PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(a1_edge PUBLIC a1_value)

add_library(a1_scheduler STATIC runtime/scheduler.cpp)
target_include_directories(a1_scheduler PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(a1_scheduler PUBLIC a1_continuous_evaluator a1_edge)

target_link_libraries(a1_frontend PUBLIC a1_ir slang::slang)

add_executable(ir_probe src/frontend/probe_main.cpp)
target_link_libraries(ir_probe PRIVATE slang::slang a1_frontend)

enable_testing()
add_executable(probe_report_test tests/unit/probe_report_test.cpp)
target_link_libraries(probe_report_test PRIVATE a1_frontend)
add_test(NAME probe_report_test COMMAND probe_report_test)

add_executable(value_test tests/unit/value_test.cpp)
target_include_directories(value_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(value_test PRIVATE a1_value)
add_test(NAME value_test COMMAND value_test)

add_executable(net_resolution_test tests/unit/net_resolution_test.cpp)
target_include_directories(net_resolution_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(net_resolution_test PRIVATE a1_value)
add_test(NAME net_resolution_test COMMAND net_resolution_test)

add_executable(model_ir_test tests/unit/model_ir_test.cpp)
target_include_directories(model_ir_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(model_ir_test PRIVATE a1_ir)
add_test(NAME model_ir_test COMMAND model_ir_test)

add_executable(continuous_evaluator_test tests/unit/continuous_evaluator_test.cpp)
target_include_directories(continuous_evaluator_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(continuous_evaluator_test PRIVATE a1_continuous_evaluator)
add_test(NAME continuous_evaluator_test COMMAND continuous_evaluator_test)

add_executable(frontend_lowering_test tests/integration/frontend_lowering_test.cpp)
target_include_directories(frontend_lowering_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(frontend_lowering_test PRIVATE a1_frontend a1_continuous_evaluator)
add_test(NAME frontend_lowering_test COMMAND frontend_lowering_test)

add_executable(edge_test tests/unit/edge_test.cpp)
target_include_directories(edge_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(edge_test PRIVATE a1_edge)
add_test(NAME edge_test COMMAND edge_test)

add_executable(scheduler_test tests/unit/scheduler_test.cpp)
target_include_directories(scheduler_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(scheduler_test PRIVATE a1_scheduler)
add_test(NAME scheduler_test COMMAND scheduler_test)

add_executable(flat_core_slice_test tests/integration/flat_core_slice_test.cpp)
target_include_directories(flat_core_slice_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(flat_core_slice_test PRIVATE a1_frontend a1_scheduler)
add_test(NAME flat_core_slice_test COMMAND flat_core_slice_test)

add_custom_target(a1_phase1_tests
  DEPENDS probe_report_test value_test net_resolution_test model_ir_test
          continuous_evaluator_test frontend_lowering_test)

add_custom_target(a1_phase2_tests
  DEPENDS a1_phase1_tests edge_test scheduler_test flat_core_slice_test)

find_package(Python 3.10 REQUIRED COMPONENTS Interpreter)
add_test(
  NAME probe_smoke
  COMMAND "${Python_EXECUTABLE}"
          "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/probe_smoke.py"
          --probe "$<TARGET_FILE:ir_probe>")
