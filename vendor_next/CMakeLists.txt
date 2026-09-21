cmake_minimum_required(VERSION 3.16)

project(electronics_vendor_v2
    LANGUAGES C
)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# ============================================================
# Core library
# ============================================================

add_library(electronics_core STATIC

    src/vec.c
    src/intern.c
    src/range.c
    src/constraint.c

    src/component.c
    src/net.c
    src/design.c

    src/component_model.c
    src/model_registry.c

    src/diagnostic.c
    src/dfm.c
    src/part_provider.c
    src/mna.c
)

target_include_directories(electronics_core
    PUBLIC
        ${PROJECT_SOURCE_DIR}/src
)

# ============================================================
# Warnings
# ============================================================

if (MSVC)

    target_compile_options(electronics_core PRIVATE
        /W4
    )

else()

    target_compile_options(electronics_core PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )

endif()

# ============================================================
# Tests
# ============================================================

enable_testing()

function(add_core_test TEST_NAME TEST_SOURCE)

    add_executable(${TEST_NAME}
        ${TEST_SOURCE}
    )

    target_link_libraries(${TEST_NAME}
        PRIVATE
            electronics_core
    )

    target_include_directories(${TEST_NAME}
        PRIVATE
            ${PROJECT_SOURCE_DIR}/src
    )

    add_test(
        NAME ${TEST_NAME}
        COMMAND ${TEST_NAME}
    )

endfunction()

add_core_test(
    vec_test
    tests/vec_test.c
)

add_core_test(
    intern_test
    tests/intern_test.c
)

add_core_test(
    design_test
    tests/design_test.c
)

add_core_test(
    component_model_test
    tests/component_model_test.c
)

add_core_test(
    model_test
    tests/model_test.c
)

add_core_test(
    dfm_test
    tests/dfm_test.c
)

add_core_test(
    provider_test
    tests/provider_test.c
)

add_core_test(
    mna_test
    tests/mna_test.c
)