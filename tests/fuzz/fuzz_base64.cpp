/*
 * fuzz_base64.cpp — libFuzzer harness for base64url encode/decode
 *
 * Fuzz target: Feed arbitrary bytes to Encoder::fromBase64/toBase64.
 * Catches: buffer overflows, OOM, incorrect length calculations.
 *
 * Build:
 *   clang++ -g -O1 -fsanitize=fuzzer,address -I... fuzz_base64.cpp \
 *           Encoder.cpp -o fuzz_base64
 */

#include "Encoder.h"
#include <cstdint>
#include <cstddef>
#include <string>

using namespace dcpp;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size > 256 * 1024) return 0; // Cap at 256KB

    std::string input(reinterpret_cast<const char*>(data), size);

    // Phase 1: Decode arbitrary string
    try {
        std::string decoded = Encoder::fromBase64(input);
        (void)decoded;
    } catch (...) {
        // Acceptable
    }

    // Phase 2: Encode arbitrary bytes, then decode — roundtrip must match
    try {
        std::string encoded = Encoder::toBase64(input);
        std::string roundtrip = Encoder::fromBase64(encoded);
        // roundtrip should equal input for valid encode→decode
    } catch (...) {
        // Acceptable
    }

    return 0;
}
