cmake_minimum_required(VERSION 3.22)
project(MiniExchange VERSION 0.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(mini_exchange
    src/order_book.cpp
    src/matching_engine.cpp
    src/journal.cpp
    src/order_generator.cpp
    src/replay.cpp
)
target_include_directories(mini_exchange PUBLIC include)
target_compile_options(mini_exchange PRIVATE -Wall -Wextra -Wpedantic)

add_executable(cli_demo src/main.cpp)
target_link_libraries(cli_demo PRIVATE mini_exchange)

add_executable(replay apps/replay.cpp)
target_link_libraries(replay PRIVATE mini_exchange)

include(CTest)
if(BUILD_TESTING)
    foreach(test_name journal_test spsc_test matching_test replay_test)
        add_executable(${test_name} tests/${test_name}.cpp)
        target_link_libraries(${test_name} PRIVATE mini_exchange)
        add_test(NAME ${test_name} COMMAND ${test_name})
    endforeach()
endif()

# 网络部分仅支持 POSIX（Linux/WSL）；Windows 主机构建时跳过。
if(UNIX)
    add_library(exchange_net src/net.cpp)
    target_include_directories(exchange_net PUBLIC include)
    target_compile_options(exchange_net PRIVATE -Wall -Wextra -Wpedantic)

    add_executable(exchange_server apps/exchange_server.cpp)
    target_link_libraries(exchange_server PRIVATE mini_exchange exchange_net)

    add_executable(sample_client apps/sample_client.cpp)
    target_link_libraries(sample_client PRIVATE mini_exchange exchange_net)

    if(BUILD_TESTING)
        find_package(Threads REQUIRED)
        add_executable(framing_test tests/framing_test.cpp)
        target_link_libraries(framing_test PRIVATE exchange_net Threads::Threads)
        add_test(NAME framing_test COMMAND framing_test)
    endif()
endif()
