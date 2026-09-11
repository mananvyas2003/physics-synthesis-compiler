cmake_minimum_required(VERSION 3.16)
project(physics_synthesis_compiler C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(SYNTH_ENABLE_SANITIZERS "Enable AddressSanitizer and UBSan" OFF)

if(MSVC)
  add_compile_options(/W4)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

if(SYNTH_ENABLE_SANITIZERS)
  if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined)
    add_link_options(-fsanitize=address,undefined)
  else()
    message(WARNING "SYNTH_ENABLE_SANITIZERS is supported on GCC/Clang only; ignoring on this compiler")
  endif()
endif()

set(SYNTH_INCLUDE_DIRS
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/cli
  ${CMAKE_CURRENT_SOURCE_DIR}/seed
  ${CMAKE_CURRENT_SOURCE_DIR}/emit
  ${CMAKE_CURRENT_SOURCE_DIR}/spec
  ${CMAKE_CURRENT_SOURCE_DIR}/compose
  ${CMAKE_CURRENT_SOURCE_DIR}/bind
  ${CMAKE_CURRENT_SOURCE_DIR}/verify
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/sqlite3
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cJSON
)

add_library(sqlite3 STATIC third_party/sqlite3/sqlite3.c)
target_include_directories(sqlite3 PUBLIC third_party/sqlite3)
if(NOT MSVC)
  target_compile_options(sqlite3 PRIVATE -Wno-unused-parameter -Wno-sign-compare)
endif()

add_library(cjson STATIC third_party/cJSON/cJSON.c)
target_include_directories(cjson PUBLIC third_party/cJSON)

add_library(synth_core STATIC
  db.c
  catalogue.c
  jlcparts_import.c
  DFM.c
  compiler.c
  physics2_isa.c
  physics2_interpreter.c
  physics2_types.c
  physics2_print.c
  physics2_symbols.c
  physics2_typecheck.c
  seed/seed_topology.c
  emit/emit_bom.c
  emit/emit_netlist.c
  emit/emit_snapshot.c
  spec/spec_load.c
  spec/llm_provider.c
  compose/compose.c
  bind/bind_scorer.c
  verify/verify_report.c
  cli/cli_common.c
  cli/cmd_db.c
  cli/cmd_dfm.c
  cli/cmd_compile.c
  cli/cmd_physics2.c
  cli/cmd_generate.c
)
target_include_directories(synth_core PUBLIC ${SYNTH_INCLUDE_DIRS})
target_link_libraries(synth_core PUBLIC sqlite3 cjson)

if(MINGW)
  target_link_libraries(synth_core PUBLIC pthread)
elseif(UNIX AND NOT APPLE)
  target_link_libraries(synth_core PUBLIC pthread dl m)
elseif(APPLE)
  target_link_libraries(synth_core PUBLIC pthread m)
endif()

add_executable(synth main.c)
target_link_libraries(synth PRIVATE synth_core)

add_executable(golden_runner
  tests/golden_runner.c
  tests/golden_cases.c
)
target_include_directories(golden_runner PRIVATE ${SYNTH_INCLUDE_DIRS} tests)
target_link_libraries(golden_runner PRIVATE synth_core)
if(UNIX OR MINGW)
  target_link_libraries(golden_runner PRIVATE m)
endif()

enable_testing()
add_test(NAME golden COMMAND golden_runner
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
set_tests_properties(golden PROPERTIES
  ENVIRONMENT "SYNTH_FIXTURE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}"
)

install(TARGETS synth RUNTIME DESTINATION bin)
