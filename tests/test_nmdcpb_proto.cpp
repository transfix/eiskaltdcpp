/*
 * test_nmdcpb_proto.cpp — PbEnvelope roundtrip tests for ALL payload types
 *
 * Fills coverage gaps for Phases 1-4 by testing serialization/deserialization
 * of every PbEnvelope oneof payload type and routing field combination.
 *
 * Phase 1 (Core):     PbUserInfo, PbSearch, PbSearchResult, PbConnect,
 *                     PbHubInfo, PbStatus, PbExtension, routing fields
 * Phase 2 (Relay):    PbRelayAck, PbRelayData, PbRelayClosed, PbRelayStatus,
 *                     PbPMSessionEnd
 * Phase 3.5 (Adv):    PbRelayResume, PbSegmentRequest, PbSegmentInfo,
 *                     PbUserQuery, PbUserQueryResult
 * Phase 4 (Media):    PbMediaUpload, PbMediaMeta, PbMediaDelete,
 *                     PbMediaCapabilities, PbMediaRef in PbChat,
 *                     PbEncryptedMediaRef in PbPMPlaintext
 */

#ifdef WITH_NMDCPB

#include <catch2/catch_test_macros.hpp>
#include "proto/nmdcpb.pb.h"
#include <string>

// =========================================================================
// Helper: serialize + deserialize roundtrip
// =========================================================================
static nmdcpb::PbEnvelope roundtrip(const nmdcpb::PbEnvelope& env) {
    std::string wire;
    REQUIRE(env.SerializeToString(&wire));
    REQUIRE(!wire.empty());
    nmdcpb::PbEnvelope out;
    REQUIRE(out.ParseFromString(wire));
    return out;
}

// =========================================================================
// Phase 1: Routing field coverage
// =========================================================================

TEST_CASE("PbEnvelope routing: all RouteType values roundtrip", "[protobuf][phase1]") {
    using RT = nmdcpb::PbEnvelope::RouteType;
    RT types[] = { RT::PbEnvelope_RouteType_BROADCAST,
                   RT::PbEnvelope_RouteType_DIRECT,
                   RT::PbEnvelope_RouteType_HUB,
                   RT::PbEnvelope_RouteType_INFO,
                   RT::PbEnvelope_RouteType_ECHO,
                   RT::PbEnvelope_RouteType_FEATURE };

    for (auto rt : types) {
        nmdcpb::PbEnvelope env;
        env.set_route(rt);
        env.set_from_nick("sender");
        env.mutable_chat()->set_text("test");

        auto out = roundtrip(env);
        REQUIRE(out.route() == rt);
    }
}

TEST_CASE("PbEnvelope routing: DIRECT with to_nick roundtrip", "[protobuf][phase1]") {
    nmdcpb::PbEnvelope env;
    env.set_route(nmdcpb::PbEnvelope::DIRECT);
    env.set_from_nick("Alice");
    env.set_to_nick("Bob");
    env.set_timestamp(1700000000000ULL);
    env.set_sequence(42);
    env.mutable_chat()->set_text("hi");

    auto out = roundtrip(env);
    REQUIRE(out.from_nick() == "Alice");
    REQUIRE(out.to_nick() == "Bob");
    REQUIRE(out.timestamp() == 1700000000000ULL);
    REQUIRE(out.sequence() == 42);
}

TEST_CASE("PbEnvelope routing: FEATURE with features string roundtrip", "[protobuf][phase1]") {
    nmdcpb::PbEnvelope env;
    env.set_route(nmdcpb::PbEnvelope::FEATURE);
    env.set_from_nick("hub-bot");
    env.set_features("+NMDCpb +E2EPM");
    env.mutable_chat()->set_text("feature broadcast");

    auto out = roundtrip(env);
    REQUIRE(out.features() == "+NMDCpb +E2EPM");
    REQUIRE(out.route() == nmdcpb::PbEnvelope::FEATURE);
}

// =========================================================================
// Phase 1: Core message roundtrips
// =========================================================================

TEST_CASE("PbEnvelope PbUserInfo roundtrip", "[protobuf][phase1]") {
    nmdcpb::PbEnvelope env;
    env.set_route(nmdcpb::PbEnvelope::BROADCAST);
    auto* ui = env.mutable_user_info();
    ui->set_nick("TestUser");
    ui->set_description("A test user");
    ui->set_tag("<EiskaltDC++ V:2.4.2,M:P,H:1/0/0,S:5>");
    ui->set_email("test@example.com");
    ui->set_share_size(1073741824ULL);
    ui->set_shared_files(1234);
    ui->set_upload_slots(5);
    ui->set_free_slots(3);
    ui->set_connection_speed("100");
    ui->set_is_passive(true);
    ui->set_supports_tls(true);
    ui->set_tls_keyprint("ABCDEF1234567890");
    ui->set_user_class(nmdcpb::PbUserInfo::OPERATOR);
    ui->set_is_away(false);
    ui->add_features("NMDCpb");
    ui->add_features("E2EPM");
    (*ui->mutable_extra())["custom_key"] = "custom_value";

    auto out = roundtrip(env);
    REQUIRE(out.has_user_info());
    auto& u = out.user_info();
    REQUIRE(u.nick() == "TestUser");
    REQUIRE(u.description() == "A test user");
    REQUIRE(u.tag() == "<EiskaltDC++ V:2.4.2,M:P,H:1/0/0,S:5>");
    REQUIRE(u.share_size() == 1073741824ULL);
    REQUIRE(u.shared_files() == 1234);
    REQUIRE(u.is_passive() == true);
    REQUIRE(u.supports_tls() == true);
    REQUIRE(u.user_class() == nmdcpb::PbUserInfo::OPERATOR);
    REQUIRE(u.features_size() == 2);
    REQUIRE(u.features(0) == "NMDCpb");
    REQUIRE(u.extra().at("custom_key") == "custom_value");
}

TEST_CASE("PbEnvelope PbSearch roundtrip", "[protobuf][phase1]") {
    nmdcpb::PbEnvelope env;
    env.set_route(nmdcpb::PbEnvelope::BROADCAST);
    env.set_from_nick("Searcher");
    auto* s = env.mutable_search();
    s->set_query("ubuntu iso");
    s->add_exclude("beta");
    s->set_file_type(nmdcpb::PbSearch::COMPRESSED);
    s->set_size_mode(nmdcpb::PbSearch::AT_LEAST);
    s->set_size(1048576ULL);
    s->set_token("search-tok-1");
    s->add_extensions("iso");
    s->add_extensions("img");
    s->set_max_results(50);

    auto out = roundtrip(env);
    REQUIRE(out.has_search());
    auto& sr = out.search();
    REQUIRE(sr.query() == "ubuntu iso");
    REQUIRE(sr.exclude_size() == 1);
    REQUIRE(sr.exclude(0) == "beta");
    REQUIRE(sr.file_type() == nmdcpb::PbSearch::COMPRESSED);
    REQUIRE(sr.size_mode() == nmdcpb::PbSearch::AT_LEAST);
    REQUIRE(sr.size() == 1048576ULL);
    REQUIRE(sr.token() == "search-tok-1");
    REQUIRE(sr.extensions_size() == 2);
    REQUIRE(sr.max_results() == 50);
}

TEST_CASE("PbEnvelope PbSearch TTH mode roundtrip", "[protobuf][phase1]") {
    nmdcpb::PbEnvelope env;
    env.set_route(nmdcpb::PbEnvelope::BROADCAST);
    auto* s = env.mutable_search();
    s->set_tth("ABCDEFGHIJKLMNOPQRSTUVWXYZ234567AAAABBBB");
    s->set_file_type(nmdcpb::PbSearch::TTH);
    s->set_max_results(1);

    auto out = roundtrip(env);
    REQUIRE(out.has_search());
    REQUIRE(out.search().tth() == "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567AAAABBBB");
    REQUIRE(out.search().file_type() == nmdcpb::PbSearch::TTH);
    REQUIRE(out.search().query().empty());
}

TEST_CASE("PbEnvelope PbSearchResult roundtrip", "[protobuf][phase1]") {
    nmdcpb::PbEnvelope env;
    env.set_route(nmdcpb::PbEnvelope::DIRECT);
    env.set_from_nick("Sharer");
    env.set_to_nick("Searcher");
    auto* sr = env.mutable_search_result();
    sr->set_nick("Sharer");
    sr->set_filename("ubuntu-24.04.iso");
    sr->set_size(4700000000ULL);
    sr->set_free_slots(3);
    sr->set_total_slots(5);
    sr->set_tth("ABCDEFGHIJKLMNOPQRSTUVWXYZ234567AAAABBBB");
    sr->set_hub_name("TestHub");
    sr->set_hub_url("dchub://test.hub:411");
    sr->set_token("search-tok-1");
    sr->set_is_directory(false);
    sr->set_path("ISOs/");

    auto out = roundtrip(env);
    REQUIRE(out.has_search_result());
    auto& r = out.search_result();
    REQUIRE(r.filename() == "ubuntu-24.04.iso");
    REQUIRE(r.size() == 4700000000ULL);
    REQUIRE(r.free_slots() == 3);
    REQUIRE(r.tth() == "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567AAAABBBB");
    REQUIRE(r.path() == "ISOs/");
    REQUIRE(r.is_directory() == false);
}

TEST_CASE("PbEnvelope PbConnect roundtrip", "[protobuf][phase1]") {
    nmdcpb::PbEnvelope env;
    env.set_route(nmdcpb::PbEnvelope::DIRECT);
    auto* c = env.mutable_connect();
    c->set_type(nmdcpb::PbConnect::CONNECT_TO_ME);
    c->set_target_nick("PeerB");
    c->set_ip("192.168.1.50");
    c->set_port(12345);
    c->set_use_tls(true);
    c->set_token("conn-tok-1");
    c->set_protocol("NMDC");

    auto out = roundtrip(env);
    REQUIRE(out.has_connect());
    auto& co = out.connect();
    REQUIRE(co.type() == nmdcpb::PbConnect::CONNECT_TO_ME);
    REQUIRE(co.target_nick() == "PeerB");
    REQUIRE(co.ip() == "192.168.1.50");
    REQUIRE(co.port() == 12345);
    REQUIRE(co.use_tls() == true);
    REQUIRE(co.token() == "conn-tok-1");
}

TEST_CASE("PbEnvelope PbConnect HUB_RELAY type roundtrip", "[protobuf][phase1]") {
    nmdcpb::PbEnvelope env;
    env.set_route(nmdcpb::PbEnvelope::DIRECT);
    auto* c = env.mutable_connect();
    c->set_type(nmdcpb::PbConnect::HUB_RELAY);
    c->set_target_nick("PassiveUser");
    c->set_token("relay-conn-1");

    auto out = roundtrip(env);
    REQUIRE(out.connect().type() == nmdcpb::PbConnect::HUB_RELAY);
}

TEST_CASE("PbEnvelope PbHubInfo roundtrip", "[protobuf][phase1]") {
    nmdcpb::PbEnvelope env;
    env.set_route(nmdcpb::PbEnvelope::INFO);
    auto* hi = env.mutable_hub_info();
    hi->set_name("Test Hub");
    hi->set_topic("Welcome to the test hub");
    hi->set_description("A hub for testing");
    hi->set_user_count(42);
    hi->set_total_share(109951162777600ULL); // ~100 TB
    hi->set_min_share(1073741824);
    hi->set_max_users(500);
    hi->set_hub_url("dchub://test.hub:411");
    hi->add_failover_urls("dchub://backup.hub:411");
    hi->set_encoding("UTF-8");
    (*hi->mutable_rules())["min_share"] = "1 GB";
    hi->set_protocol_version(1);
    hi->set_session_token("sess-abc-123");

    auto out = roundtrip(env);
    REQUIRE(out.has_hub_info());
    auto& h = out.hub_info();
    REQUIRE(h.name() == "Test Hub");
    REQUIRE(h.user_count() == 42);
    REQUIRE(h.total_share() == 109951162777600ULL);
    REQUIRE(h.failover_urls_size() == 1);
    REQUIRE(h.failover_urls(0) == "dchub://backup.hub:411");
    REQUIRE(h.rules().at("min_share") == "1 GB");
    REQUIRE(h.session_token() == "sess-abc-123");
}

TEST_CASE("PbEnvelope PbStatus roundtrip", "[protobuf][phase1]") {
    nmdcpb::PbEnvelope env;
    env.set_route(nmdcpb::PbEnvelope::INFO);
    auto* st = env.mutable_status();
    st->set_code(200);
    st->set_severity(nmdcpb::PbStatus::WARNING);
    st->set_message("Share too low");
    st->set_detail("Minimum share is 1 GB, you share 500 MB");

    auto out = roundtrip(env);
    REQUIRE(out.has_status());
    auto& s = out.status();
    REQUIRE(s.code() == 200);
    REQUIRE(s.severity() == nmdcpb::PbStatus::WARNING);
    REQUIRE(s.message() == "Share too low");
    REQUIRE(s.detail() == "Minimum share is 1 GB, you share 500 MB");
}

TEST_CASE("PbEnvelope PbStatus all severity levels", "[protobuf][phase1]") {
    auto test = [](nmdcpb::PbStatus::Severity sev) {
        nmdcpb::PbEnvelope env;
        env.set_route(nmdcpb::PbEnvelope::INFO);
        auto* st = env.mutable_status();
        st->set_severity(sev);
        st->set_message("test");
        auto out = roundtrip(env);
        REQUIRE(out.status().severity() == sev);
    };
    test(nmdcpb::PbStatus::INFO);
    test(nmdcpb::PbStatus::WARNING);
    test(nmdcpb::PbStatus::ERROR);
    test(nmdcpb::PbStatus::FATAL);
}

TEST_CASE("PbEnvelope PbExtension roundtrip", "[protobuf][phase1]") {
    nmdcpb::PbEnvelope env;
    env.set_route(nmdcpb::PbEnvelope::BROADCAST);
    auto* ext = env.mutable_extension();
    ext->set_type_url("nmdcpb.custom/my_plugin");
    std::string payload = "arbitrary binary data \x00\x01\x02";
    ext->set_payload(payload);

    auto out = roundtrip(env);
    REQUIRE(out.has_extension());
    REQUIRE(out.extension().type_url() == "nmdcpb.custom/my_plugin");
    REQUIRE(out.extension().payload() == payload);
}

// =========================================================================
// Phase 2: Relay + E2EPM message roundtrips (gap-fill)
// =========================================================================

TEST_CASE("PbEnvelope PbRelayAck roundtrip", "[protobuf][phase2]") {
    nmdcpb::PbEnvelope env;
    env.set_route(nmdcpb::PbEnvelope::DIRECT);
    auto* ack = env.mutable_relay_ack();
    ack->set_token("relay-tok-abc");
    ack->set_accepted(true);
    uint8_t fakeKey[32];
    memset(fakeKey, 0xBB, 32);
    ack->set_public_key(fakeKey, 32);
    ack->set_relay_id(7);

    auto out = roundtrip(env);
    REQUIRE(out.has_relay_ack());
    auto& a = out.relay_ack();
    REQUIRE(a.token() == "relay-tok-abc");
    REQUIRE(a.accepted() == true);
    REQUIRE(a.public_key().size() == 32);
    REQUIRE(a.relay_id() == 7);
    REQUIRE(a.reject_reason().empty());
}

TEST_CASE("PbEnvelope PbRelayAck rejection roundtrip", "[protobuf][phase2]") {
    nmdcpb::PbEnvelope env;
    env.set_route(nmdcpb::PbEnvelope::DIRECT);
    auto* ack = env.mutable_relay_ack();
    ack->set_token("relay-tok-def");
    ack->set_accepted(false);
    ack->set_reject_reason("too many sessions");

    auto out = roundtrip(env);
    REQUIRE(out.relay_ack().accepted() == false);
    REQUIRE(out.relay_ack().reject_reason() == "too many sessions");
}

TEST_CASE("PbEnvelope PbRelayData roundtrip", "[protobuf][phase2]") {
    nmdcpb::PbEnvelope env;
    env.set_route(nmdcpb::PbEnvelope::DIRECT);
    auto* rd = env.mutable_relay_data();
    rd->set_relay_id(42);
    std::string data(4096, '\xAA');
    rd->set_data(data);
    rd->set_offset(1048576ULL);

    auto out = roundtrip(env);
    REQUIRE(out.has_relay_data());
    REQUIRE(out.relay_data().relay_id() == 42);
    REQUIRE(out.relay_data().data().size() == 4096);
    REQUIRE(out.relay_data().offset() == 1048576ULL);
}

TEST_CASE("PbEnvelope PbRelayClosed roundtrip", "[protobuf][phase2]") {
    nmdcpb::PbEnvelope env;
    env.set_route(nmdcpb::PbEnvelope::DIRECT);
    auto* rc = env.mutable_relay_closed();
    rc->set_relay_id(42);
    rc->set_reason(nmdcpb::PbRelayClosed::TIMEOUT);

    auto out = roundtrip(env);
    REQUIRE(out.has_relay_closed());
    REQUIRE(out.relay_closed().relay_id() == 42);
    REQUIRE(out.relay_closed().reason() == nmdcpb::PbRelayClosed::TIMEOUT);
}

TEST_CASE("PbEnvelope PbRelayClosed all reasons", "[protobuf][phase2]") {
    using CR = nmdcpb::PbRelayClosed;
    CR::CloseReason reasons[] = { CR::NORMAL, CR::ERROR, CR::TIMEOUT,
                                   CR::HUB_LIMIT, CR::USER_DISCONNECT };
    for (auto r : reasons) {
        nmdcpb::PbEnvelope env;
        auto* rc = env.mutable_relay_closed();
        rc->set_relay_id(1);
        rc->set_reason(r);
        auto out = roundtrip(env);
        REQUIRE(out.relay_closed().reason() == r);
    }
}

TEST_CASE("PbEnvelope PbRelayStatus roundtrip", "[protobuf][phase2]") {
    nmdcpb::PbEnvelope env;
    env.set_route(nmdcpb::PbEnvelope::INFO);
    auto* rs = env.mutable_relay_status();
    rs->set_relay_id(5);
    rs->set_bytes_relayed(10485760ULL);
    rs->set_active_sessions(3);
    rs->set_max_sessions(10);
    rs->set_bandwidth_used(524288ULL);
    rs->set_bandwidth_limit(1048576ULL);

    auto out = roundtrip(env);
    REQUIRE(out.has_relay_status());
    auto& s = out.relay_status();
    REQUIRE(s.bytes_relayed() == 10485760ULL);
    REQUIRE(s.active_sessions() == 3);
    REQUIRE(s.max_sessions() == 10);
    REQUIRE(s.bandwidth_used() == 524288ULL);
    REQUIRE(s.bandwidth_limit() == 1048576ULL);
}

TEST_CASE("PbEnvelope PbPMSessionEnd roundtrip", "[protobuf][phase2]") {
    nmdcpb::PbEnvelope env;
    env.set_route(nmdcpb::PbEnvelope::DIRECT);
    auto* end = env.mutable_pm_session_end();
    end->set_target_nick("Bob");
    end->set_reason(nmdcpb::PbPMSessionEnd::NORMAL_CLOSE);

    auto out = roundtrip(env);
    REQUIRE(out.has_pm_session_end());
    REQUIRE(out.pm_session_end().target_nick() == "Bob");
    REQUIRE(out.pm_session_end().reason() == nmdcpb::PbPMSessionEnd::NORMAL_CLOSE);
}

TEST_CASE("PbEnvelope PbPMSessionEnd all reasons", "[protobuf][phase2]") {
    using R = nmdcpb::PbPMSessionEnd;
    R::Reason reasons[] = { R::NORMAL_CLOSE, R::KEY_ROTATION, R::SECURITY_ALERT };
    for (auto r : reasons) {
        nmdcpb::PbEnvelope env;
        auto* end = env.mutable_pm_session_end();
        end->set_target_nick("Peer");
        end->set_reason(r);
        auto out = roundtrip(env);
        REQUIRE(out.pm_session_end().reason() == r);
    }
}

// =========================================================================
// Phase 3.5: Advanced Relay messages
// =========================================================================

TEST_CASE("PbEnvelope PbRelayResume roundtrip", "[protobuf][phase3.5]") {
    nmdcpb::PbEnvelope env;
    env.set_route(nmdcpb::PbEnvelope::DIRECT);
    auto* rr = env.mutable_relay_resume();
    rr->set_relay_id(42);
    rr->set_resume_offset(8388608ULL); // 8MB
    std::string sha(32, '\xDD');
    rr->set_partial_sha256(sha);
    rr->set_token("relay-tok-resume");

    auto out = roundtrip(env);
    REQUIRE(out.has_relay_resume());
    auto& r = out.relay_resume();
    REQUIRE(r.relay_id() == 42);
    REQUIRE(r.resume_offset() == 8388608ULL);
    REQUIRE(r.partial_sha256().size() == 32);
    REQUIRE(r.token() == "relay-tok-resume");
}

TEST_CASE("PbEnvelope PbSegmentRequest roundtrip", "[protobuf][phase3.5]") {
    nmdcpb::PbEnvelope env;
    env.set_route(nmdcpb::PbEnvelope::DIRECT);
    env.set_from_nick("Downloader");
    env.set_to_nick("Source");
    auto* seg = env.mutable_segment_request();
    seg->set_file_tth("ABCDEFGHIJKLMNOPQRSTUVWXYZ234567AAAABBBB");
    seg->set_file_size(104857600ULL); // 100MB
    seg->set_segment_offset(4194304ULL); // 4MB
    seg->set_segment_length(4194304ULL);
    seg->set_request_id("seg-req-001");

    auto out = roundtrip(env);
    REQUIRE(out.has_segment_request());
    auto& s = out.segment_request();
    REQUIRE(s.file_tth() == "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567AAAABBBB");
    REQUIRE(s.file_size() == 104857600ULL);
    REQUIRE(s.segment_offset() == 4194304ULL);
    REQUIRE(s.segment_length() == 4194304ULL);
    REQUIRE(s.request_id() == "seg-req-001");
}

TEST_CASE("PbEnvelope PbSegmentInfo roundtrip", "[protobuf][phase3.5]") {
    nmdcpb::PbEnvelope env;
    env.set_route(nmdcpb::PbEnvelope::DIRECT);
    auto* si = env.mutable_segment_info();
    si->set_request_id("seg-req-001");
    si->set_available(true);
    si->set_segment_offset(4194304ULL);
    si->set_segment_length(4194304ULL);
    si->set_peer_nick("Source1");

    auto out = roundtrip(env);
    REQUIRE(out.has_segment_info());
    auto& s = out.segment_info();
    REQUIRE(s.request_id() == "seg-req-001");
    REQUIRE(s.available() == true);
    REQUIRE(s.segment_offset() == 4194304ULL);
    REQUIRE(s.peer_nick() == "Source1");
}

TEST_CASE("PbEnvelope PbSegmentInfo unavailable response", "[protobuf][phase3.5]") {
    nmdcpb::PbEnvelope env;
    auto* si = env.mutable_segment_info();
    si->set_request_id("seg-req-002");
    si->set_available(false);

    auto out = roundtrip(env);
    REQUIRE(out.segment_info().available() == false);
    REQUIRE(out.segment_info().peer_nick().empty());
}

TEST_CASE("PbEnvelope PbUserQuery roundtrip", "[protobuf][phase3.5]") {
    nmdcpb::PbEnvelope env;
    env.set_route(nmdcpb::PbEnvelope::HUB);
    env.set_from_nick("Searcher");
    auto* uq = env.mutable_user_query();
    uq->set_query_id("uq-001");
    uq->set_feature_filter("NMDCpb");
    uq->set_min_share_size(1073741824ULL);
    uq->set_max_results(25);
    uq->set_sweep(false);

    auto out = roundtrip(env);
    REQUIRE(out.has_user_query());
    auto& q = out.user_query();
    REQUIRE(q.query_id() == "uq-001");
    REQUIRE(q.feature_filter() == "NMDCpb");
    REQUIRE(q.min_share_size() == 1073741824ULL);
    REQUIRE(q.max_results() == 25);
    REQUIRE(q.sweep() == false);
}

TEST_CASE("PbEnvelope PbUserQuery with sweep + embedded search", "[protobuf][phase3.5]") {
    nmdcpb::PbEnvelope env;
    env.set_route(nmdcpb::PbEnvelope::HUB);
    auto* uq = env.mutable_user_query();
    uq->set_query_id("uq-sweep-1");
    uq->set_sweep(true);
    uq->set_max_results(50);

    auto* srch = uq->mutable_search();
    srch->set_search_id("sweep-search-1");
    srch->set_query("ubuntu");
    srch->set_file_type(nmdcpb::PbPrivateSearch::COMPRESSED);

    auto out = roundtrip(env);
    REQUIRE(out.user_query().sweep() == true);
    REQUIRE(out.user_query().has_search());
    REQUIRE(out.user_query().search().query() == "ubuntu");
    REQUIRE(out.user_query().search().file_type() == nmdcpb::PbPrivateSearch::COMPRESSED);
}

TEST_CASE("PbEnvelope PbUserQueryResult roundtrip", "[protobuf][phase3.5]") {
    nmdcpb::PbEnvelope env;
    env.set_route(nmdcpb::PbEnvelope::DIRECT);
    auto* uqr = env.mutable_user_query_result();
    uqr->set_query_id("uq-001");
    uqr->add_nicks("Alice");
    uqr->add_nicks("Bob");
    uqr->add_nicks("Charlie");
    uqr->set_total_matching(42);
    uqr->set_sweep_started(true);
    uqr->set_sweep_count(3);

    auto out = roundtrip(env);
    REQUIRE(out.has_user_query_result());
    auto& r = out.user_query_result();
    REQUIRE(r.query_id() == "uq-001");
    REQUIRE(r.nicks_size() == 3);
    REQUIRE(r.nicks(0) == "Alice");
    REQUIRE(r.total_matching() == 42);
    REQUIRE(r.sweep_started() == true);
    REQUIRE(r.sweep_count() == 3);
    REQUIRE(r.error().empty());
}

TEST_CASE("PbEnvelope PbUserQueryResult error", "[protobuf][phase3.5]") {
    nmdcpb::PbEnvelope env;
    auto* uqr = env.mutable_user_query_result();
    uqr->set_query_id("uq-fail");
    uqr->set_error("user query disabled");

    auto out = roundtrip(env);
    REQUIRE(out.user_query_result().error() == "user query disabled");
    REQUIRE(out.user_query_result().nicks_size() == 0);
}

// =========================================================================
// Phase 4: MediaShare messages
// =========================================================================

TEST_CASE("PbEnvelope PbMediaUpload roundtrip", "[protobuf][phase4]") {
    nmdcpb::PbEnvelope env;
    env.set_route(nmdcpb::PbEnvelope::HUB);
    env.set_from_nick("Uploader");
    auto* mu = env.mutable_media_upload();
    mu->set_filename("photo.jpg");
    mu->set_mime_type("image/jpeg");
    mu->set_size(2097152ULL);
    mu->set_requested_ttl(86400);
    mu->set_is_encrypted(false);
    mu->set_checksum_sha256("abcdef1234567890");

    auto out = roundtrip(env);
    REQUIRE(out.has_media_upload());
    auto& u = out.media_upload();
    REQUIRE(u.filename() == "photo.jpg");
    REQUIRE(u.mime_type() == "image/jpeg");
    REQUIRE(u.size() == 2097152ULL);
    REQUIRE(u.requested_ttl() == 86400);
    REQUIRE(u.is_encrypted() == false);
    REQUIRE(u.checksum_sha256() == "abcdef1234567890");
}

TEST_CASE("PbEnvelope PbMediaUpload encrypted roundtrip", "[protobuf][phase4]") {
    nmdcpb::PbEnvelope env;
    env.set_route(nmdcpb::PbEnvelope::HUB);
    auto* mu = env.mutable_media_upload();
    mu->set_filename("secret.bin");
    mu->set_mime_type("application/octet-stream");
    mu->set_size(4096);
    mu->set_is_encrypted(true);

    auto out = roundtrip(env);
    REQUIRE(out.media_upload().is_encrypted() == true);
}

TEST_CASE("PbEnvelope PbMediaMeta roundtrip", "[protobuf][phase4]") {
    nmdcpb::PbEnvelope env;
    env.set_route(nmdcpb::PbEnvelope::DIRECT);
    auto* mm = env.mutable_media_meta();
    mm->set_media_id("media-uuid-123");
    mm->set_url("https://hub.example.com/media/media-uuid-123");
    mm->set_thumbnail_url("https://hub.example.com/media/media-uuid-123/thumb");
    mm->set_mime_type("image/jpeg");
    mm->set_size(2097152ULL);
    mm->set_filename("photo.jpg");
    mm->set_expires_at(1700086400000ULL);
    mm->set_uploader_nick("Alice");
    mm->set_width(1920);
    mm->set_height(1080);
    mm->set_duration_ms(0);
    mm->set_checksum_sha256("sha256hash");

    auto out = roundtrip(env);
    REQUIRE(out.has_media_meta());
    auto& m = out.media_meta();
    REQUIRE(m.media_id() == "media-uuid-123");
    REQUIRE(m.url() == "https://hub.example.com/media/media-uuid-123");
    REQUIRE(m.thumbnail_url() == "https://hub.example.com/media/media-uuid-123/thumb");
    REQUIRE(m.width() == 1920);
    REQUIRE(m.height() == 1080);
    REQUIRE(m.expires_at() == 1700086400000ULL);
    REQUIRE(m.checksum_sha256() == "sha256hash");
}

TEST_CASE("PbEnvelope PbMediaDelete roundtrip", "[protobuf][phase4]") {
    nmdcpb::PbEnvelope env;
    env.set_route(nmdcpb::PbEnvelope::HUB);
    auto* md = env.mutable_media_delete();
    md->set_media_id("media-uuid-456");
    md->set_reason("inappropriate content");

    auto out = roundtrip(env);
    REQUIRE(out.has_media_delete());
    REQUIRE(out.media_delete().media_id() == "media-uuid-456");
    REQUIRE(out.media_delete().reason() == "inappropriate content");
}

TEST_CASE("PbEnvelope PbMediaCapabilities roundtrip", "[protobuf][phase4]") {
    nmdcpb::PbEnvelope env;
    env.set_route(nmdcpb::PbEnvelope::DIRECT);
    auto* mc = env.mutable_media_capabilities();
    mc->set_enabled(true);
    mc->set_max_file_size(10485760ULL);
    mc->set_user_quota_remaining(52428800ULL);
    mc->set_max_ttl(604800);
    mc->set_default_ttl(86400);
    mc->add_allowed_types("image/*");
    mc->add_allowed_types("video/mp4");
    mc->add_allowed_types("audio/opus");
    mc->set_thumbnails_available(true);
    mc->set_upload_url("https://hub.example.com/upload");

    auto out = roundtrip(env);
    REQUIRE(out.has_media_capabilities());
    auto& c = out.media_capabilities();
    REQUIRE(c.enabled() == true);
    REQUIRE(c.max_file_size() == 10485760ULL);
    REQUIRE(c.user_quota_remaining() == 52428800ULL);
    REQUIRE(c.max_ttl() == 604800);
    REQUIRE(c.default_ttl() == 86400);
    REQUIRE(c.allowed_types_size() == 3);
    REQUIRE(c.allowed_types(0) == "image/*");
    REQUIRE(c.thumbnails_available() == true);
    REQUIRE(c.upload_url() == "https://hub.example.com/upload");
}

TEST_CASE("PbEnvelope PbMediaCapabilities disabled hub", "[protobuf][phase4]") {
    nmdcpb::PbEnvelope env;
    auto* mc = env.mutable_media_capabilities();
    mc->set_enabled(false);

    auto out = roundtrip(env);
    REQUIRE(out.media_capabilities().enabled() == false);
    REQUIRE(out.media_capabilities().allowed_types_size() == 0);
}

// =========================================================================
// Phase 4: PbMediaRef in PbChat (media attachments in chat)
// =========================================================================

TEST_CASE("PbChat with PbMediaRef attachments roundtrip", "[protobuf][phase4]") {
    nmdcpb::PbEnvelope env;
    env.set_route(nmdcpb::PbEnvelope::BROADCAST);
    env.set_from_nick("Alice");
    auto* chat = env.mutable_chat();
    chat->set_text("Check out this photo!");

    auto* ref = chat->add_attachments();
    ref->set_media_id("media-001");
    ref->set_url("https://hub.example.com/media/media-001");
    ref->set_thumbnail_url("https://hub.example.com/media/media-001/thumb");
    ref->set_mime_type("image/png");
    ref->set_filename("screenshot.png");
    ref->set_size(524288);
    ref->set_width(1280);
    ref->set_height(720);

    auto out = roundtrip(env);
    REQUIRE(out.chat().attachments_size() == 1);
    auto& a = out.chat().attachments(0);
    REQUIRE(a.media_id() == "media-001");
    REQUIRE(a.url() == "https://hub.example.com/media/media-001");
    REQUIRE(a.mime_type() == "image/png");
    REQUIRE(a.width() == 1280);
    REQUIRE(a.height() == 720);
}

TEST_CASE("PbChat with multiple attachments roundtrip", "[protobuf][phase4]") {
    nmdcpb::PbEnvelope env;
    env.set_route(nmdcpb::PbEnvelope::BROADCAST);
    auto* chat = env.mutable_chat();
    chat->set_text("Album");

    for (int i = 0; i < 5; i++) {
        auto* ref = chat->add_attachments();
        ref->set_media_id("media-" + std::to_string(i));
        ref->set_mime_type("image/jpeg");
        ref->set_size(1024 * (i + 1));
    }

    auto out = roundtrip(env);
    REQUIRE(out.chat().attachments_size() == 5);
    for (int i = 0; i < 5; i++) {
        REQUIRE(out.chat().attachments(i).media_id() == "media-" + std::to_string(i));
        REQUIRE(out.chat().attachments(i).size() == 1024 * (i + 1));
    }
}

// =========================================================================
// Phase 4: PbEncryptedMediaRef in PbPMPlaintext (E2EPM encrypted media)
// =========================================================================

TEST_CASE("PbPMPlaintext with PbEncryptedMediaRef roundtrip", "[protobuf][phase4]") {
    nmdcpb::PbPMPlaintext pt;
    pt.set_text("Here's the secret file");
    pt.set_timestamp(1700000000000ULL);

    auto* ref = pt.add_encrypted_attachments();
    ref->set_media_id("enc-media-001");
    ref->set_url("https://hub.example.com/media/enc-media-001");
    ref->set_filename("secret.docx");
    ref->set_mime_type("application/vnd.openxmlformats-officedocument.wordprocessingml.document");
    ref->set_size(204800);
    std::string key(32, '\xCC');
    ref->set_file_encryption_key(key);
    std::string nonce(12, '\xDD');
    ref->set_file_nonce(nonce);

    std::string wire;
    REQUIRE(pt.SerializeToString(&wire));
    nmdcpb::PbPMPlaintext pt2;
    REQUIRE(pt2.ParseFromString(wire));

    REQUIRE(pt2.encrypted_attachments_size() == 1);
    auto& a = pt2.encrypted_attachments(0);
    REQUIRE(a.media_id() == "enc-media-001");
    REQUIRE(a.file_encryption_key().size() == 32);
    REQUIRE(a.file_nonce().size() == 12);
    REQUIRE(a.mime_type() == "application/vnd.openxmlformats-officedocument.wordprocessingml.document");
}

// =========================================================================
// Cross-cutting: Empty / edge-case payloads
// =========================================================================

TEST_CASE("PbEnvelope with no payload roundtrip", "[protobuf][edge]") {
    nmdcpb::PbEnvelope env;
    env.set_route(nmdcpb::PbEnvelope::BROADCAST);
    env.set_from_nick("test");
    // No payload set

    std::string wire;
    REQUIRE(env.SerializeToString(&wire));
    nmdcpb::PbEnvelope out;
    REQUIRE(out.ParseFromString(wire));
    REQUIRE(out.payload_case() == nmdcpb::PbEnvelope::PAYLOAD_NOT_SET);
}

TEST_CASE("PbEnvelope empty chat text roundtrip", "[protobuf][edge]") {
    nmdcpb::PbEnvelope env;
    env.set_route(nmdcpb::PbEnvelope::BROADCAST);
    auto* chat = env.mutable_chat();
    chat->set_text("");

    auto out = roundtrip(env);
    REQUIRE(out.has_chat());
    REQUIRE(out.chat().text().empty());
}

TEST_CASE("PbEnvelope with unicode nicks and text", "[protobuf][edge]") {
    nmdcpb::PbEnvelope env;
    env.set_route(nmdcpb::PbEnvelope::DIRECT);
    // Russian: "Пользователь"
    env.set_from_nick("\xD0\x9F\xD0\xBE\xD0\xBB\xD1\x8C\xD0\xB7\xD0\xBE\xD0\xB2\xD0\xB0\xD1\x82\xD0\xB5\xD0\xBB\xD1\x8C");
    // Japanese (3 chars)
    env.set_to_nick("\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E");
    auto* chat = env.mutable_chat();
    // "Hello 🌍"
    chat->set_text("Hello \xF0\x9F\x8C\x8D");

    auto out = roundtrip(env);
    REQUIRE(out.from_nick() == "\xD0\x9F\xD0\xBE\xD0\xBB\xD1\x8C\xD0\xB7\xD0\xBE\xD0\xB2\xD0\xB0\xD1\x82\xD0\xB5\xD0\xBB\xD1\x8C");
    REQUIRE(out.to_nick() == "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E");
    REQUIRE(out.chat().text() == "Hello \xF0\x9F\x8C\x8D");
}

TEST_CASE("PbEnvelope payload_case distinguishes types", "[protobuf][edge]") {
    auto check = [](auto setter, nmdcpb::PbEnvelope::PayloadCase expected) {
        nmdcpb::PbEnvelope env;
        setter(env);
        auto out = roundtrip(env);
        REQUIRE(out.payload_case() == expected);
    };

    check([](auto& e) { e.mutable_chat()->set_text("x"); },
          nmdcpb::PbEnvelope::kChat);
    check([](auto& e) { e.mutable_user_info()->set_nick("x"); },
          nmdcpb::PbEnvelope::kUserInfo);
    check([](auto& e) { e.mutable_search()->set_query("x"); },
          nmdcpb::PbEnvelope::kSearch);
    check([](auto& e) { e.mutable_relay_request()->set_token("x"); },
          nmdcpb::PbEnvelope::kRelayRequest);
    check([](auto& e) { e.mutable_relay_ack()->set_token("x"); },
          nmdcpb::PbEnvelope::kRelayAck);
    check([](auto& e) { e.mutable_relay_data()->set_relay_id(1); },
          nmdcpb::PbEnvelope::kRelayData);
    check([](auto& e) { e.mutable_status()->set_code(1); },
          nmdcpb::PbEnvelope::kStatus);
    check([](auto& e) { e.mutable_media_upload()->set_filename("x"); },
          nmdcpb::PbEnvelope::kMediaUpload);
    check([](auto& e) { e.mutable_media_meta()->set_media_id("x"); },
          nmdcpb::PbEnvelope::kMediaMeta);
    check([](auto& e) { e.mutable_segment_request()->set_request_id("x"); },
          nmdcpb::PbEnvelope::kSegmentRequest);
    check([](auto& e) { e.mutable_user_query()->set_query_id("x"); },
          nmdcpb::PbEnvelope::kUserQuery);
}

#else // !WITH_NMDCPB

#include <catch2/catch_test_macros.hpp>

TEST_CASE("NMDCpb disabled — proto tests skipped", "[nmdcpb]") {
    REQUIRE(true);
}

#endif // WITH_NMDCPB
