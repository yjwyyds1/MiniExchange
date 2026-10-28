cmake_minimum_required(VERSION 3.22)
project(MiniExchange VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(mini_exchange
    src/order_book.cpp
    src/matching_engine.cpp
    src/journal.cpp
)
target_include_directories(mini_exchange PUBLIC include)
target_compile_options(mini_exchange PRIVATE -Wall -Wextra -Wpedantic)

add_executable(cli_demo src/main.cpp)
target_link_libraries(cli_demo PRIVATE mini_exchange)

include(CTest)
if(BUILD_TESTING)
    foreach(test_name journal_test spsc_test matching_test)
        add_executable(${test_name} tests/${test_name}.cpp)
        target_link_libraries(${test_name} PRIVATE mini_exchange)
        add_test(NAME ${test_name} COMMAND ${test_name})
    endforeach()
endif()
