/*
 * Copyright (C) 2026 Joe Rivera <transfix@sublevels.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/*
 * Unit tests for dcpp::Text encoding utilities
 *
 * Tests UTF-8 ↔ wide string conversions, validation, and case handling.
 * These are regression tests for the Qt6 migration.
 */

#include <catch2/catch_test_macros.hpp>

#include "stdinc.h"
#include "Text.h"

using namespace dcpp;

// ── isAscii ─────────────────────────────────────────────────────────────

TEST_CASE("Text::isAscii accepts pure ASCII", "[Text]") {
    REQUIRE(Text::isAscii("Hello, World!"));
    REQUIRE(Text::isAscii(""));
    REQUIRE(Text::isAscii("0123456789"));
    REQUIRE(Text::isAscii("~")); // 0x7E is valid ASCII
}

TEST_CASE("Text::isAscii rejects non-ASCII", "[Text]") {
    REQUIRE_FALSE(Text::isAscii("\xC3\xA9"));         // é (UTF-8)
    REQUIRE_FALSE(Text::isAscii("caf\xC3\xA9"));      // café
    REQUIRE_FALSE(Text::isAscii("\x80"));               // bare high byte
}

// ── validateUtf8 ────────────────────────────────────────────────────────

TEST_CASE("Text::validateUtf8 accepts valid UTF-8", "[Text]") {
    REQUIRE(Text::validateUtf8("Hello"));
    REQUIRE(Text::validateUtf8(""));
    REQUIRE(Text::validateUtf8("café"));                // 2-byte sequences
    REQUIRE(Text::validateUtf8("日本語"));              // 3-byte sequences
    REQUIRE(Text::validateUtf8("𝕳𝖊𝖑𝖑𝖔"));    // 4-byte sequences (Mathematical Fraktur)
}

TEST_CASE("Text::validateUtf8 rejects invalid sequences", "[Text]") {
    REQUIRE_FALSE(Text::validateUtf8("\x80"));          // bare continuation byte
    REQUIRE_FALSE(Text::validateUtf8("\xFE\xFF"));      // invalid lead bytes
    REQUIRE_FALSE(Text::validateUtf8("\xC3"));          // truncated 2-byte sequence
    // Note: overlong sequences like \xC0\xAF may pass validation depending
    // on the iconv-based implementation; only test clearly invalid bytes.
}

// ── UTF-8 ↔ Wide round-trip ─────────────────────────────────────────────

TEST_CASE("Text::utf8ToWide and wideToUtf8 round-trip ASCII", "[Text]") {
    const std::string original = "Hello, World!";
    std::wstring wide = Text::utf8ToWide(original);
    std::string back = Text::wideToUtf8(wide);
    REQUIRE(back == original);
}

TEST_CASE("Text::utf8ToWide and wideToUtf8 round-trip multibyte", "[Text]") {
    const std::string original = "café résumé naïve";
    std::wstring wide = Text::utf8ToWide(original);
    REQUIRE_FALSE(wide.empty());
    std::string back = Text::wideToUtf8(wide);
    REQUIRE(back == original);
}

TEST_CASE("Text::utf8ToWide and wideToUtf8 round-trip CJK", "[Text]") {
    const std::string original = "日本語テスト";
    std::wstring wide = Text::utf8ToWide(original);
    std::string back = Text::wideToUtf8(wide);
    REQUIRE(back == original);
}

TEST_CASE("Text::utf8ToWide and wideToUtf8 round-trip empty", "[Text]") {
    const std::string original;
    std::wstring wide = Text::utf8ToWide(original);
    std::string back = Text::wideToUtf8(wide);
    REQUIRE(back.empty());
}

// ── utf8ToWc / wcToUtf8 ────────────────────────────────────────────────

TEST_CASE("Text::utf8ToWc decodes ASCII character", "[Text]") {
    wchar_t c = 0;
    int bytes = Text::utf8ToWc("A", c);
    REQUIRE(bytes == 1);
    REQUIRE(c == L'A');
}

TEST_CASE("Text::utf8ToWc decodes 2-byte character", "[Text]") {
    wchar_t c = 0;
    int bytes = Text::utf8ToWc("\xC3\xA9", c); // é = U+00E9
    REQUIRE(bytes == 2);
    REQUIRE(c == L'\u00E9');
}

TEST_CASE("Text::utf8ToWc decodes 3-byte character", "[Text]") {
    wchar_t c = 0;
    int bytes = Text::utf8ToWc("\xE6\x97\xA5", c); // 日 = U+65E5
    REQUIRE(bytes == 3);
    REQUIRE(c == L'\u65E5');
}

TEST_CASE("Text::wcToUtf8 encodes ASCII", "[Text]") {
    std::string out;
    Text::wcToUtf8(L'A', out);
    REQUIRE(out == "A");
}

TEST_CASE("Text::wcToUtf8 encodes 2-byte character", "[Text]") {
    std::string out;
    Text::wcToUtf8(L'\u00E9', out); // é
    REQUIRE(out == "\xC3\xA9");
}

// ── toLower ─────────────────────────────────────────────────────────────

TEST_CASE("Text::toLower converts ASCII to lowercase", "[Text]") {
    std::string result = Text::toLower("HELLO WORLD");
    REQUIRE(result == "hello world");
}

TEST_CASE("Text::toLower preserves already-lowercase", "[Text]") {
    std::string result = Text::toLower("hello");
    REQUIRE(result == "hello");
}

TEST_CASE("Text::toLower handles mixed case with numbers", "[Text]") {
    std::string result = Text::toLower("Test123ABC");
    REQUIRE(result == "test123abc");
}

TEST_CASE("Text::toLower handles empty string", "[Text]") {
    std::string result = Text::toLower("");
    REQUIRE(result.empty());
}

// ── toDOS ───────────────────────────────────────────────────────────────

TEST_CASE("Text::toDOS converts LF to CRLF", "[Text]") {
    std::string result = Text::toDOS("line1\nline2\nline3");
    REQUIRE(result == "line1\r\nline2\r\nline3");
}

TEST_CASE("Text::toDOS preserves existing CRLF", "[Text]") {
    std::string result = Text::toDOS("line1\r\nline2\r\n");
    REQUIRE(result == "line1\r\nline2\r\n");
}

TEST_CASE("Text::toDOS handles empty string", "[Text]") {
    std::string result = Text::toDOS(std::string(""));
    REQUIRE(result.empty());
}

TEST_CASE("Text::toDOS handles string with no newlines", "[Text]") {
    std::string result = Text::toDOS("hello world");
    REQUIRE(result == "hello world");
}
