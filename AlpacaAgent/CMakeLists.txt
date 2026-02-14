cmake_minimum_required(VERSION 3.15)

# Read version from workspace VERSION file
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/../VERSION" PROJECT_VERSION LIMIT_COUNT 1)
string(STRIP "${PROJECT_VERSION}" PROJECT_VERSION)

project(AlpacaAgent VERSION ${PROJECT_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ALPACAAGENT_BUILD_TESTS "Build AlpacaAgent test suite" ON)

find_package(nlohmann_json 3.2.0 QUIET)
if(NOT nlohmann_json_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.11.3
    )
    FetchContent_MakeAvailable(nlohmann_json)
endif()

set(ALPACAAGENT_SOURCES
    src/time_utils.cpp
    src/run_registry.cpp
    src/http_server.cpp
)

add_library(alpacaagent STATIC ${ALPACAAGENT_SOURCES})
target_include_directories(alpacaagent PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(alpacaagent PUBLIC nlohmann_json::nlohmann_json)
target_compile_definitions(alpacaagent PUBLIC ALPACAAGENT_VERSION=\"${PROJECT_VERSION}\")

add_executable(alpacaagent_server src/main.cpp)
target_link_libraries(alpacaagent_server PRIVATE alpacaagent)

if(ALPACAAGENT_BUILD_TESTS)
    enable_testing()

    add_executable(test_run_registry tests/test_run_registry.cpp)
    target_link_libraries(test_run_registry PRIVATE alpacaagent)
    add_test(NAME test_run_registry COMMAND test_run_registry)
endif()
