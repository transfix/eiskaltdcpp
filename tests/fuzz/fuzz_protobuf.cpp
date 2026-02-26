/*
 * fuzz_protobuf.cpp — libFuzzer harness for PbEnvelope deserialization
 *
 * Fuzz target: Parse arbitrary bytes as a PbEnvelope protobuf message.
 * Catches: buffer overflows, infinite loops, OOM in protobuf parser.
 *
 * Build:
 *   clang++ -g -O1 -fsanitize=fuzzer,address -I... fuzz_protobuf.cpp \
 *           -lprotobuf -o fuzz_protobuf
 */

#ifdef WITH_NMDCPB

#include "proto/nmdcpb.pb.h"
#include <cstdint>
#include <cstddef>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    // Limit input size to prevent OOM on huge allocations
    if (size > 1024 * 1024) return 0;

    // Phase 1: Parse as PbEnvelope
    {
        nmdcpb::PbEnvelope env;
        env.ParseFromArray(data, static_cast<int>(size));

        // If parsing succeeded, exercise accessors
        if (env.has_chat()) {
            (void)env.chat().text();
            (void)env.chat().is_action();
        }
        if (env.has_encrypted_pm()) {
            (void)env.encrypted_pm().target_nick();
            (void)env.encrypted_pm().nonce();
            (void)env.encrypted_pm().ciphertext();
        }
        if (env.has_relay_request()) {
            (void)env.relay_request().token();
            (void)env.relay_request().public_key();
        }
        if (env.has_relay_data()) {
            (void)env.relay_data().relay_id();
            (void)env.relay_data().payload();
        }
        if (env.has_pm_key_exchange()) {
            (void)env.pm_key_exchange().public_key();
        }
        if (env.has_private_search()) {
            (void)env.private_search().query();
            (void)env.private_search().tth();
        }
        if (env.has_private_search_result()) {
            for (int i = 0; i < env.private_search_result().results_size(); ++i) {
                (void)env.private_search_result().results(i).filename();
            }
        }

        // Re-serialize to catch any serialization issues
        std::string roundtrip;
        env.SerializeToString(&roundtrip);
    }

    // Phase 2: Parse as PbPMPlaintext (inner message)
    {
        nmdcpb::PbPMPlaintext pt;
        pt.ParseFromArray(data, static_cast<int>(size));
        (void)pt.text();
        (void)pt.is_action();
        (void)pt.timestamp();
    }

    return 0;
}

#else

#include <cstdint>
#include <cstddef>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t*, size_t) {
    return 0;
}

#endif
