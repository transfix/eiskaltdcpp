project(eiskaltdcpp-tests)

# Fetch Catch2 v3
include(FetchContent)
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.5.2
)
FetchContent_MakeAvailable(Catch2)

# Include Catch2's CMake helpers (provides catch_discover_tests)
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)

# Test executable
add_executable(eiskaltdcpp-tests
    test_main.cpp
    test_cid.cpp
    test_util.cpp
    test_util_extended.cpp
    test_dccontext.cpp
    test_encoder.cpp
    test_string_tokenizer.cpp
    test_text.cpp
    test_adccommand.cpp
    test_simplexml.cpp
)

target_include_directories(eiskaltdcpp-tests PRIVATE
    ${CMAKE_SOURCE_DIR}          # for "dcpp/..." includes from extra/
    ${CMAKE_SOURCE_DIR}/dcpp     # for "Singleton.h" etc.
    ${BZIP2_INCLUDE_DIR}
    ${ZLIB_INCLUDE_DIR}
    ${OPENSSL_INCLUDE_DIR}
    ${GETTEXT_INCLUDE_DIR}
    ${IDN2_INCLUDE_DIR}
)

target_link_libraries(eiskaltdcpp-tests PRIVATE
    dcpp
    Catch2::Catch2WithMain
)

# Auto-discover test cases
catch_discover_tests(eiskaltdcpp-tests)
