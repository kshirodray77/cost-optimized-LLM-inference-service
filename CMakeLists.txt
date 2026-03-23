cmake_minimum_required(VERSION 3.16)
project(llm_gateway VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Options ──────────────────────────────────────────────────────────
option(BUILD_TESTS "Build unit tests" ON)
option(ENABLE_ASAN "Enable AddressSanitizer" OFF)

if(ENABLE_ASAN)
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
endif()

# ── Dependencies via FetchContent ────────────────────────────────────
include(FetchContent)

# Crow - lightweight HTTP framework (already used in original)
FetchContent_Declare(crow
    GIT_REPOSITORY https://github.com/CrowCpp/Crow.git
    GIT_TAG        v1.2.0
)

# JSON library
FetchContent_Declare(json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
)

# spdlog for structured logging
FetchContent_Declare(spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v1.13.0
)

# cpp-httplib for outbound HTTP calls to providers
FetchContent_Declare(httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG        v0.15.3
)

FetchContent_MakeAvailable(crow json spdlog httplib)

find_package(Threads REQUIRED)
find_package(OpenSSL QUIET)

# ── Gateway Library ──────────────────────────────────────────────────
set(GATEWAY_SOURCES
    src/config.cpp
    src/router.cpp
    src/cache.cpp
    src/cost_tracker.cpp
    src/providers/base_provider.cpp
    src/providers/openai_provider.cpp
    src/providers/anthropic_provider.cpp
    src/providers/together_provider.cpp
    src/providers/groq_provider.cpp
    src/providers/provider_registry.cpp
    src/middleware/auth.cpp
    src/middleware/rate_limiter.cpp
    src/middleware/logger.cpp
    src/api/chat_completions.cpp
    src/api/health.cpp
    src/api/admin.cpp
    src/utils/token_counter.cpp
    src/utils/metrics.cpp
)

add_library(gateway_lib STATIC ${GATEWAY_SOURCES})
target_include_directories(gateway_lib PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${httplib_SOURCE_DIR}
)
target_link_libraries(gateway_lib PUBLIC
    Threads::Threads
    Crow::Crow
    nlohmann_json::nlohmann_json
    spdlog::spdlog
)

if(OPENSSL_FOUND)
    target_link_libraries(gateway_lib PUBLIC OpenSSL::SSL OpenSSL::Crypto)
    target_compile_definitions(gateway_lib PUBLIC CPPHTTPLIB_OPENSSL_SUPPORT)
endif()

# ── Main Executable ──────────────────────────────────────────────────
add_executable(llm_gateway src/main.cpp)
target_link_libraries(llm_gateway PRIVATE gateway_lib)

# ── Tests ────────────────────────────────────────────────────────────
if(BUILD_TESTS)
    enable_testing()
    FetchContent_Declare(googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.14.0
    )
    FetchContent_MakeAvailable(googletest)

    add_executable(gateway_tests
        tests/test_router.cpp
        tests/test_cache.cpp
        tests/test_cost_tracker.cpp
        tests/test_rate_limiter.cpp
        tests/test_auth.cpp
    )
    target_link_libraries(gateway_tests PRIVATE gateway_lib GTest::gtest_main)
    include(GoogleTest)
    gtest_discover_tests(gateway_tests)
endif()

# ── Install ──────────────────────────────────────────────────────────
install(TARGETS llm_gateway RUNTIME DESTINATION bin)
install(FILES config/gateway.json DESTINATION etc/llm-gateway)

message(STATUS "")
message(STATUS "╔══════════════════════════════════════════╗")
message(STATUS "║   LLM Gateway v${PROJECT_VERSION}                    ║")
message(STATUS "║   Build type: ${CMAKE_BUILD_TYPE}                    ║")
message(STATUS "║   SSL: ${OPENSSL_FOUND}                               ║")
message(STATUS "╚══════════════════════════════════════════╝")
message(STATUS "")
