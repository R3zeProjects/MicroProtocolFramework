cmake_minimum_required(VERSION 3.25)

project(MicroProtocolFramework VERSION 0.1.0 LANGUAGES CXX)
set(MICRO_PROTOCOL_FRAMEWORK_PRERELEASE "beta")

include(CTest)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(MPROTOCOL_BUILD_EXAMPLES "Build MicroProtocolFramework examples" ON)
option(MPROTOCOL_BUILD_BENCHMARKS "Build opt-in protocol benchmarks" OFF)
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
        find_package(vosp_contracts 0.7 QUIET CONFIG)
        if(NOT vosp_contracts_FOUND)
            if(NOT MPROTOCOL_FETCH_CONTRACTS)
                message(FATAL_ERROR
                    "MicroContractsFramework 0.7 is required. Install it, set "
                    "MPROTOCOL_CONTRACTS_SOURCE_DIR, or enable "
                    "MPROTOCOL_FETCH_CONTRACTS.")
            endif()
            include(FetchContent)
            FetchContent_Declare(vosp_contracts
                GIT_REPOSITORY
                    https://github.com/R3zeProjects/MicroContractsFramework.git
                GIT_TAG 2a97f144868d93c604b6983af1f6e7cdf91656fb
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
        tests/header_contracts/types_header.cpp)
    target_link_libraries(MicroProtocolFrameworkHeaderContracts
        PRIVATE MicroProtocolFramework)

    add_executable(MicroProtocolFrameworkTests tests/protocol_tests.cpp)
    target_link_libraries(MicroProtocolFrameworkTests
        PRIVATE MicroProtocolFramework)
    mprotocol_enable_sanitizers(MicroProtocolFrameworkTests)
    add_test(NAME MicroProtocolFramework.Tests
        COMMAND MicroProtocolFrameworkTests)

    add_executable(MicroProtocolFrameworkInvalidModel EXCLUDE_FROM_ALL
        tests/compile_fail/invalid_model.cpp)
    target_link_libraries(MicroProtocolFrameworkInvalidModel
        PRIVATE MicroProtocolFramework)
    add_test(NAME MicroProtocolFramework.CompileFail.InvalidModel
        COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}
            --config $<CONFIG> --target MicroProtocolFrameworkInvalidModel)
    set_tests_properties(MicroProtocolFramework.CompileFail.InvalidModel
        PROPERTIES WILL_FAIL TRUE)
endif()

if(MPROTOCOL_BUILD_EXAMPLES)
    add_executable(MicroProtocolFrameworkExample examples/basic.cpp)
    target_link_libraries(MicroProtocolFrameworkExample
        PRIVATE MicroProtocolFramework)
    add_executable(MicroProtocolFrameworkStreamExample examples/stream.cpp)
    target_link_libraries(MicroProtocolFrameworkStreamExample
        PRIVATE MicroProtocolFramework)
    if(BUILD_TESTING)
        add_test(NAME MicroProtocolFramework.Example.Basic
            COMMAND MicroProtocolFrameworkExample)
        add_test(NAME MicroProtocolFramework.Example.Stream
            COMMAND MicroProtocolFrameworkStreamExample)
    endif()
endif()

if(MPROTOCOL_BUILD_BENCHMARKS)
    add_executable(MicroProtocolFrameworkBenchmark
        benchmarks/protocol_benchmark.cpp)
    target_link_libraries(MicroProtocolFrameworkBenchmark
        PRIVATE MicroProtocolFramework)
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
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND
       CMAKE_CXX_FLAGS MATCHES "stdlib=libc\\+\\+")
        # Ubuntu compiler-rt packages build libFuzzer against libstdc++ even
        # when the harness uses libc++. Disable the driver's implicit archive
        # so that the engine is linked before its private libstdc++ dependency.
        string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _mprotocol_arch)
        if(_mprotocol_arch STREQUAL "amd64")
            set(_mprotocol_arch "x86_64")
        elseif(_mprotocol_arch STREQUAL "arm64")
            set(_mprotocol_arch "aarch64")
        endif()
        execute_process(
            COMMAND ${CMAKE_CXX_COMPILER}
                --print-file-name=libclang_rt.fuzzer-${_mprotocol_arch}.a
            OUTPUT_VARIABLE _mprotocol_fuzzer_runtime
            OUTPUT_STRIP_TRAILING_WHITESPACE
            COMMAND_ERROR_IS_FATAL ANY)
        if(NOT IS_ABSOLUTE "${_mprotocol_fuzzer_runtime}" OR
           NOT EXISTS "${_mprotocol_fuzzer_runtime}")
            message(FATAL_ERROR
                "Clang did not provide a usable libFuzzer runtime: "
                "${_mprotocol_fuzzer_runtime}")
        endif()
        target_compile_options(MicroProtocolFrameworkFrameFuzzer PRIVATE
            -fsanitize=fuzzer-no-link,address,undefined
            -fno-omit-frame-pointer)
        target_link_options(MicroProtocolFrameworkFrameFuzzer PRIVATE
            -fsanitize=address,undefined)
        target_link_libraries(MicroProtocolFrameworkFrameFuzzer PRIVATE
            "-Wl,--whole-archive"
            "${_mprotocol_fuzzer_runtime}"
            "-Wl,--no-whole-archive"
            stdc++)
        unset(_mprotocol_arch)
        unset(_mprotocol_fuzzer_runtime)
    else()
        target_compile_options(MicroProtocolFrameworkFrameFuzzer PRIVATE
            -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer)
        target_link_options(MicroProtocolFrameworkFrameFuzzer PRIVATE
            -fsanitize=fuzzer,address,undefined)
    endif()
endif()

install(TARGETS MicroProtocolFramework EXPORT mprotocolTargets)
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
