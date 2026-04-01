cmake_minimum_required(VERSION 3.20)

project(lora_network_simulation LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(-Wall -Wextra -Wpedantic)

option(SIM_BUILD_DEMO "Build simulation demo executable" ON)
option(SIM_BUILD_TESTS "Build simulation tests" ON)

set(REPO_ROOT ${CMAKE_CURRENT_LIST_DIR}/..)
set(NETWORK_COMPONENT_DIR ${REPO_ROOT}/components/lora_network_layer)
set(NETWORK_INCLUDE_DIR ${NETWORK_COMPONENT_DIR}/include)
set(NETWORK_SRC_DIR ${NETWORK_COMPONENT_DIR}/src)

add_library(lora_network_core STATIC
    ${NETWORK_SRC_DIR}/geo_utils.cpp
    ${NETWORK_SRC_DIR}/duplicate_filter.cpp
    ${NETWORK_SRC_DIR}/routing_engine.cpp
    ${NETWORK_SRC_DIR}/forwarding_queue.cpp
    ${NETWORK_SRC_DIR}/network_manager.cpp
)

target_include_directories(lora_network_core PUBLIC
    ${NETWORK_INCLUDE_DIR}
)

target_compile_features(lora_network_core PUBLIC cxx_std_17)

add_library(network_simulation STATIC
    src/simulation_clock.cpp
    src/simulation_event_queue.cpp
    src/simulation_metrics.cpp
    src/simulated_location_provider.cpp
    src/simulated_link_layer.cpp
    src/simulated_network.cpp
    src/config_loader.cpp
    src/simulation_builder.cpp
)

target_include_directories(network_simulation PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/include
    ${NETWORK_INCLUDE_DIR}
)

target_link_libraries(network_simulation PUBLIC
    lora_network_core
)

target_compile_features(network_simulation PUBLIC cxx_std_17)

if(SIM_BUILD_DEMO)
    add_executable(simulation_demo src/main.cpp)
    target_link_libraries(simulation_demo PRIVATE
        network_simulation
    )
endif()

if(SIM_BUILD_TESTS)
    enable_testing()

    add_library(simulation_test_support STATIC
        tests/src/simulation_test_base.cpp
    )

    target_include_directories(simulation_test_support PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}/tests/include
    )

    target_link_libraries(simulation_test_support PUBLIC
        network_simulation
    )

    target_compile_features(simulation_test_support PUBLIC cxx_std_17)

    add_executable(simulation_phase3_tests
        tests/src/test_simulation_phase3.cpp
    )

    target_link_libraries(simulation_phase3_tests PRIVATE
        simulation_test_support
    )

    add_executable(simulation_phase4_tests
        tests/src/test_simulation_phase4.cpp
    )

    target_link_libraries(simulation_phase4_tests PRIVATE
        simulation_test_support
    )

    add_executable(simulation_phase5_tests
        tests/src/test_simulation_phase5.cpp
    )

    target_link_libraries(simulation_phase5_tests PRIVATE
        simulation_test_support
    )

    add_test(
        NAME simulation_phase3_tests
        COMMAND simulation_phase3_tests
    )

    add_test(
        NAME simulation_phase4_tests
        COMMAND simulation_phase4_tests
    )

    add_test(
        NAME simulation_phase5_tests
        COMMAND simulation_phase5_tests
    )
endif()
