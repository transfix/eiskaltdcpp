/*
 * Copyright (C) 2001-2019 Jacek Sieka, arnetheduck on gmail point com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "stdinc.h"
#include "Encoder.h"

#include "Exception.h"

#include <cstring>

#include "debug.h"

namespace dcpp {

const int8_t Encoder::base32Table[] = {
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,26,27,28,29,30,31,-1,-1,-1,-1,-1,-1,-1,-1,
    -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
    15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
    -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
    15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
};

const char Encoder::base32Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";

string& Encoder::toBase32(const uint8_t* src, size_t len, string& dst) {
    // Code snagged from the bitzi bitcollider
    size_t i, index;
    uint8_t word;
    dst.reserve(((len * 8) / 5) + 1);

    for(i = 0, index = 0; i < len;) {
        /* Is the current word going to span a byte boundary? */
        if (index > 3) {
            word = (uint8_t)(src[i] & (0xFF >> index));
            index = (index + 5) % 8;
            word <<= index;
            if ((i + 1) < len)
                word |= src[i + 1] >> (8 - index);

            i++;
        } else {
            word = (uint8_t)(src[i] >> (8 - (index + 5))) & 0x1F;
            index = (index + 5) % 8;
            if (index == 0)
                i++;
        }

        dcassert(word < 32);
        dst += base32Alphabet[word];
    }
    return dst;
}

void Encoder::fromBase32(const char* src, uint8_t* dst, size_t len) {
    size_t i, index, offset;

    memset(dst, 0, len);
    for(i = 0, index = 0, offset = 0; src[i]; i++) {
        // Skip what we don't recognise
        int8_t tmp = base32Table[(unsigned char)src[i]];

        if(tmp == -1)
            continue;

        if (index <= 3) {
            index = (index + 5) % 8;
            if (index == 0) {
                dst[offset] |= tmp;
                offset++;
                if(offset == len)
                    break;
            } else {
                dst[offset] |= tmp << (8 - index);
            }
        } else {
            index = (index + 5) % 8;
            dst[offset] |= (tmp >> index);
            offset++;
            if(offset == len)
                break;
            dst[offset] |= tmp << (8 - index);
        }
    }
}

bool Encoder::isBase32(const string& str) {
    return str.find_first_not_of(base32Alphabet) == string::npos;
}

uint8_t decode16(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'A' && c <= 'F')
        return c - 'A';
    if (c >= 'a' && c <= 'f')
        return c - 'a';
    throw Exception("can't decode");
}

void Encoder::fromBase16(const char* src, uint8_t* dst, size_t len) {
    memset(dst, 0, len);
    for(size_t i = 0; src[i] && src[i+1] && i < len * 2; i += 2) {
        // Skip what we don't recognise
        auto tmp = decode16(src[i]);
        auto tmp2 = decode16(src[i+1]);
        dst[i/2] = (tmp << 4) + tmp2;
    }
}

// Base64url encoding alphabet (RFC 4648 §5)
static const char b64url_enc[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

// Decoding table for both standard base64 and base64url
static const int8_t b64_dec[256] = {
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,62,-1,63,  // + and -
    52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,  // 0-9
    -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,  // A-O
    15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,63,  // P-Z and _
    -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,  // a-o
    41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,  // p-z
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
};

string Encoder::toBase64(const string& src) {
    string result;
    const uint8_t* data = reinterpret_cast<const uint8_t*>(src.data());
    size_t len = src.size();
    result.reserve(((len + 2) / 3) * 4);

    for (size_t i = 0; i < len; i += 3) {
        uint32_t n = static_cast<uint32_t>(data[i]) << 16;
        if (i + 1 < len) n |= static_cast<uint32_t>(data[i + 1]) << 8;
        if (i + 2 < len) n |= static_cast<uint32_t>(data[i + 2]);

        result.push_back(b64url_enc[(n >> 18) & 0x3F]);
        result.push_back(b64url_enc[(n >> 12) & 0x3F]);
        if (i + 1 < len) result.push_back(b64url_enc[(n >> 6) & 0x3F]);
        if (i + 2 < len) result.push_back(b64url_enc[n & 0x3F]);
    }

    // No padding for base64url
    return result;
}

string Encoder::fromBase64(const string& src) {
    string result;
    size_t len = src.size();
    if (len == 0) return result;

    // Remove any trailing padding
    while (len > 0 && src[len - 1] == '=') --len;

    result.reserve((len * 3) / 4);

    uint32_t buf = 0;
    int bits = 0;
    for (size_t i = 0; i < len; ++i) {
        int8_t val = b64_dec[static_cast<uint8_t>(src[i])];
        if (val < 0) continue; // Skip invalid chars (whitespace, etc.)
        buf = (buf << 6) | static_cast<uint32_t>(val);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            result.push_back(static_cast<char>((buf >> bits) & 0xFF));
        }
    }

    return result;
}

} // namespace dcpp
