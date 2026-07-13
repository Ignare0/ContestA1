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
  src/frontend/ast_probe.cpp)
target_include_directories(a1_frontend PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(a1_frontend PUBLIC slang::slang)

add_executable(ir_probe src/frontend/probe_main.cpp)
target_link_libraries(ir_probe PRIVATE slang::slang a1_frontend)

enable_testing()
add_executable(probe_report_test tests/unit/probe_report_test.cpp)
target_link_libraries(probe_report_test PRIVATE a1_frontend)
add_test(NAME probe_report_test COMMAND probe_report_test)

find_package(Python 3.10 REQUIRED COMPONENTS Interpreter)
add_test(
  NAME probe_smoke
  COMMAND "${Python_EXECUTABLE}"
          "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/probe_smoke.py"
          --probe "$<TARGET_FILE:ir_probe>")
