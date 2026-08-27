cmake_minimum_required(VERSION 3.25)

project(MicroProtocolFramework VERSION 0.2.0 LANGUAGES CXX)
set(MICRO_PROTOCOL_FRAMEWORK_PRERELEASE "beta")

include(CTest)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
find_package(Threads REQUIRED)

option(MPROTOCOL_BUILD_EXAMPLES "Build MicroProtocolFramework examples" ON)
option(MPROTOCOL_BUILD_BENCHMARKS "Build opt-in protocol and transport benchmarks" OFF)
option(MPROTOCOL_BUILD_FUZZERS "Build libFuzzer targets" OFF)
option(MPROTOCOL_ENABLE_SANITIZERS
    "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(MPROTOCOL_FETCH_CONTRACTS
    "Fetch pinned MicroContractsFramework when not installed" ON)
set(MPROTOCOL_CONTRACTS_SOURCE_DIR "" CACHE PATH
    "Optional MicroContractsFramework source tree")
set(MCF_BUILD_EXAMPLES OFF CACHE BOOL "Build embedded MCF examples" FORCE)

if(NOT TARGET vosp::contracts)
    if(MPROTOCOL_CONTRACTS_SOURCE_DIR)
        if(NOT EXISTS "${MPROTOCOL_CONTRACTS_SOURCE_DIR}/CMakeLists.txt")
            message(FATAL_ERROR
                "MPROTOCOL_CONTRACTS_SOURCE_DIR does not contain MCF")
        endif()
        set(_mprotocol_saved_build_testing "${BUILD_TESTING}")
        set(BUILD_TESTING OFF)
        add_subdirectory("${MPROTOCOL_CONTRACTS_SOURCE_DIR}"
            "${CMAKE_CURRENT_BINARY_DIR}/contracts" EXCLUDE_FROM_ALL)
        set(BUILD_TESTING "${_mprotocol_saved_build_testing}")
        unset(_mprotocol_saved_build_testing)
    else()
        find_package(vosp_contracts 0.8 QUIET CONFIG)
        if(NOT vosp_contracts_FOUND)
            if(NOT MPROTOCOL_FETCH_CONTRACTS)
                message(FATAL_ERROR
                    "MicroContractsFramework 0.8 is required. Install it, set "
                    "MPROTOCOL_CONTRACTS_SOURCE_DIR, or enable "
                    "MPROTOCOL_FETCH_CONTRACTS.")
            endif()
            include(FetchContent)
            FetchContent_Declare(vosp_contracts
                GIT_REPOSITORY
                    https://github.com/R3zeProjects/MicroContractsFramework.git
                GIT_TAG 009eb4a9f340f768842be22d08b3d46aeb997ab9
                GIT_SHALLOW FALSE)
            set(_mprotocol_saved_build_testing "${BUILD_TESTING}")
            set(BUILD_TESTING OFF)
            FetchContent_MakeAvailable(vosp_contracts)
            set(BUILD_TESTING "${_mprotocol_saved_build_testing}")
            unset(_mprotocol_saved_build_testing)
        endif()
    endif()
endif()

add_library(MicroProtocolFramework INTERFACE)
add_library(vosp::protocol ALIAS MicroProtocolFramework)
set_target_properties(MicroProtocolFramework PROPERTIES EXPORT_NAME protocol)
target_compile_features(MicroProtocolFramework INTERFACE cxx_std_23)
target_include_directories(MicroProtocolFramework INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(MicroProtocolFramework INTERFACE vosp::contracts)
target_compile_options(MicroProtocolFramework INTERFACE
    $<BUILD_INTERFACE:$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/W4;/permissive->>
    $<BUILD_INTERFACE:$<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:-Wall;-Wextra;-Wpedantic>>)

set(_mprotocol_transport_sources
    src/transport/tcp.cpp
    src/transport/udp.cpp)
if(WIN32)
    list(APPEND _mprotocol_transport_sources src/transport/socket_windows.cpp)
else()
    list(APPEND _mprotocol_transport_sources src/transport/socket_posix.cpp)
endif()

add_library(MicroTransportFramework STATIC ${_mprotocol_transport_sources})
add_library(vosp::transport ALIAS MicroTransportFramework)
set_target_properties(MicroTransportFramework PROPERTIES
    EXPORT_NAME transport
    POSITION_INDEPENDENT_CODE ON)
target_compile_features(MicroTransportFramework PUBLIC cxx_std_23)
target_include_directories(MicroTransportFramework PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_include_directories(MicroTransportFramework PRIVATE src/transport)
target_link_libraries(MicroTransportFramework PUBLIC vosp::contracts Threads::Threads)
if(WIN32)
    target_link_libraries(MicroTransportFramework PUBLIC ws2_32)
endif()
target_compile_options(MicroTransportFramework PRIVATE
    $<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/W4;/permissive->
    $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:-Wall;-Wextra;-Wpedantic>)

function(mprotocol_enable_sanitizers target)
    if(MPROTOCOL_ENABLE_SANITIZERS)
        if(MSVC)
            target_compile_options(${target} PRIVATE /fsanitize=address)
            target_link_options(${target} PRIVATE /fsanitize=address)
        else()
            target_compile_options(${target} PRIVATE
                -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_link_options(${target} PRIVATE -fsanitize=address,undefined)
        endif()
    endif()
endfunction()

if(BUILD_TESTING)
    add_library(MicroProtocolFrameworkHeaderContracts OBJECT
        tests/header_contracts/binary_header.cpp
        tests/header_contracts/frame_header.cpp
        tests/header_contracts/protocol_header.cpp
        tests/header_contracts/stream_decoder_header.cpp
        tests/header_contracts/text_header.cpp
        tests/header_contracts/types_header.cpp
        tests/header_contracts/transport_header.cpp)
    target_link_libraries(MicroProtocolFrameworkHeaderContracts
        PRIVATE MicroProtocolFramework MicroTransportFramework)

    add_executable(MicroProtocolFrameworkTests tests/protocol_tests.cpp)
    target_link_libraries(MicroProtocolFrameworkTests
        PRIVATE MicroProtocolFramework)
    mprotocol_enable_sanitizers(MicroProtocolFrameworkTests)
    add_test(NAME MicroProtocolFramework.Tests
        COMMAND MicroProtocolFrameworkTests)

    add_executable(MicroTransportFrameworkTests tests/transport_tests.cpp)
    target_link_libraries(MicroTransportFrameworkTests
        PRIVATE MicroProtocolFramework MicroTransportFramework)
    mprotocol_enable_sanitizers(MicroTransportFramework)
    mprotocol_enable_sanitizers(MicroTransportFrameworkTests)
    add_test(NAME MicroTransportFramework.Tests
        COMMAND MicroTransportFrameworkTests)

    add_executable(MicroProtocolFrameworkInvalidModel EXCLUDE_FROM_ALL
        tests/compile_fail/invalid_model.cpp)
    target_link_libraries(MicroProtocolFrameworkInvalidModel
        PRIVATE MicroProtocolFramework)
    add_test(NAME MicroProtocolFramework.CompileFail.InvalidModel
        COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}
            --config $<CONFIG> --target MicroProtocolFrameworkInvalidModel)
    set_tests_properties(MicroProtocolFramework.CompileFail.InvalidModel
        PROPERTIES WILL_FAIL TRUE)

    add_executable(MicroTransportFrameworkInvalidTransport EXCLUDE_FROM_ALL
        tests/compile_fail/invalid_transport.cpp)
    target_link_libraries(MicroTransportFrameworkInvalidTransport
        PRIVATE MicroTransportFramework)
    add_test(NAME MicroTransportFramework.CompileFail.InvalidTransport
        COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}
            --config $<CONFIG> --target MicroTransportFrameworkInvalidTransport)
    set_tests_properties(MicroTransportFramework.CompileFail.InvalidTransport
        PROPERTIES WILL_FAIL TRUE)
endif()

if(MPROTOCOL_BUILD_EXAMPLES)
    add_executable(MicroProtocolFrameworkExample examples/basic.cpp)
    target_link_libraries(MicroProtocolFrameworkExample
        PRIVATE MicroProtocolFramework)
    add_executable(MicroProtocolFrameworkStreamExample examples/stream.cpp)
    target_link_libraries(MicroProtocolFrameworkStreamExample
        PRIVATE MicroProtocolFramework)
    add_executable(MicroTransportFrameworkExample examples/transport.cpp)
    target_link_libraries(MicroTransportFrameworkExample
        PRIVATE MicroTransportFramework)
    if(BUILD_TESTING)
        add_test(NAME MicroProtocolFramework.Example.Basic
            COMMAND MicroProtocolFrameworkExample)
        add_test(NAME MicroProtocolFramework.Example.Stream
            COMMAND MicroProtocolFrameworkStreamExample)
        add_test(NAME MicroTransportFramework.Example
            COMMAND MicroTransportFrameworkExample)
    endif()
endif()

if(MPROTOCOL_BUILD_BENCHMARKS)
    add_executable(MicroProtocolFrameworkBenchmark
        benchmarks/protocol_benchmark.cpp)
    target_link_libraries(MicroProtocolFrameworkBenchmark
        PRIVATE MicroProtocolFramework)
    add_executable(MicroTransportFrameworkBenchmark
        benchmarks/transport_benchmark.cpp)
    target_link_libraries(MicroTransportFrameworkBenchmark
        PRIVATE MicroTransportFramework)
endif()

if(MPROTOCOL_BUILD_FUZZERS)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "MPROTOCOL_BUILD_FUZZERS requires Clang")
    endif()
    if(MINGW)
        message(FATAL_ERROR
            "The MinGW Clang target does not provide libFuzzer. Use a native "
            "Linux/macOS Clang toolchain or disable MPROTOCOL_BUILD_FUZZERS.")
    endif()
    add_executable(MicroProtocolFrameworkFrameFuzzer fuzz/frame_fuzz.cpp)
    target_link_libraries(MicroProtocolFrameworkFrameFuzzer
        PRIVATE MicroProtocolFramework)
    target_compile_options(MicroProtocolFrameworkFrameFuzzer PRIVATE
        -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer)
    target_link_options(MicroProtocolFrameworkFrameFuzzer PRIVATE
        -fsanitize=fuzzer,address,undefined)
endif()

install(TARGETS MicroProtocolFramework MicroTransportFramework
    EXPORT mprotocolTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES LICENSE DESTINATION
    ${CMAKE_INSTALL_DATADIR}/licenses/MicroProtocolFramework)
install(EXPORT mprotocolTargets
    FILE mprotocolTargets.cmake
    NAMESPACE vosp::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/mprotocol)
configure_package_config_file(cmake/mprotocolConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/mprotocolConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/mprotocol)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/mprotocolConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/mprotocolConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/mprotocolConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/mprotocol)
