cmake_minimum_required(VERSION 3.25)
project(mprotocol_package_consumer LANGUAGES CXX)

find_package(mprotocol 0.2 REQUIRED CONFIG)
add_executable(mprotocol_package_consumer main.cpp)
target_link_libraries(mprotocol_package_consumer PRIVATE vosp::protocol vosp::transport)
target_compile_features(mprotocol_package_consumer PRIVATE cxx_std_23)
