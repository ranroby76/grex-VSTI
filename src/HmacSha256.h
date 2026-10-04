
/*
    ============================================================================
    HmacSha256.h  —  SHA-256 and HMAC-SHA256, self-contained.
    ============================================================================

    WHY THIS IS NOT juce::SHA256

    JUCE has a perfectly good SHA256, but it lives in the juce_cryptography
    module, and this project does not link that module.  Adding it would mean
    editing CMakeLists and re-running the macOS GitHub Actions build to find out
    whether it still works - a build-configuration risk taken for about ninety
    lines of extremely well-specified code.  So the algorithm is here instead.
    Nothing else in the tree changes, and the Mac workflow is untouched.

    This file is PURE C++ - no JUCE types at all - which is also what let it be
    compiled and tested standalone against the official test vectors before it
    was ever committed.  Keep it that way.

    VERIFIED AGAINST:
      * FIPS 180-4 SHA-256 vectors ("abc", the empty string, the 448-bit case)
      * RFC 4231 HMAC-SHA256 test cases 1, 2, 3, 4 and 6 (the long-key case,
        which is the one that exercises the "hash the key first" branch)
      * the live serials produced by the Base44 generator

    ── THE ONE THING TO NEVER CHANGE ──────────────────────────────────────────

    The serial is computed from the EXACT BYTES of the machine ID string.  The
    generator on the website hashes plain ASCII decimal with no padding and no
    whitespace, so this side must do the same, forever.  A stray newline or a
    zero-pad produces a completely different serial with no visible clue that
    anything is wrong - the numbers look equally random either way.
*/

#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string>

namespace Betel
{
    //==========================================================================
    /** SHA-256, FIPS 180-4.  Streaming interface so HMAC can feed it in parts. */
    class Sha256
    {
    public:
        Sha256() { reset(); }

        void reset() noexcept
        {
            state[0] = 0x6a09e667; state[1] = 0xbb67ae85;
            state[2] = 0x3c6ef372; state[3] = 0xa54ff53a;
            state[4] = 0x510e527f; state[5] = 0x9b05688c;
            state[6] = 0x1f83d9ab; state[7] = 0x5be0cd19;
            bitLen = 0; bufLen = 0;
        }

        void update (const void* data, size_t len) noexcept
        {
            const auto* p = static_cast<const uint8_t*> (data);
            for (size_t i = 0; i < len; ++i)
            {
                buffer[bufLen++] = p[i];
                if (bufLen == 64) { transform (buffer); bitLen += 512; bufLen = 0; }
            }
        }

        /** Writes 32 bytes to `out`.  The object must not be reused afterwards
            without reset(). */
        void finish (uint8_t* out) noexcept
        {
            uint64_t total = bitLen + (uint64_t) bufLen * 8;
            size_t i = bufLen;

            // Pad: 0x80, then zeros, leaving 8 bytes for the length.
            buffer[i++] = 0x80;
            if (i > 56) { while (i < 64) buffer[i++] = 0; transform (buffer); i = 0; }
            while (i < 56) buffer[i++] = 0;

            for (int k = 7; k >= 0; --k)
                buffer[i++] = (uint8_t) ((total >> (k * 8)) & 0xFF);
            transform (buffer);

            for (int k = 0; k < 8; ++k)
            {
                out[k * 4 + 0] = (uint8_t) ((state[k] >> 24) & 0xFF);
                out[k * 4 + 1] = (uint8_t) ((state[k] >> 16) & 0xFF);
                out[k * 4 + 2] = (uint8_t) ((state[k] >>  8) & 0xFF);
                out[k * 4 + 3] = (uint8_t) ( state[k]        & 0xFF);
            }
        }

        static void hash (const void* data, size_t len, uint8_t* out) noexcept
        {
            Sha256 h; h.update (data, len); h.finish (out);
        }

    private:
        static uint32_t rotr (uint32_t x, int n) noexcept { return (x >> n) | (x << (32 - n)); }

        void transform (const uint8_t* chunk) noexcept
        {
            static const uint32_t K[64] = {
                0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
                0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
                0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
                0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
                0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
                0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
                0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
                0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2 };

            uint32_t w[64];
            for (int i = 0; i < 16; ++i)
                w[i] = ((uint32_t) chunk[i*4] << 24) | ((uint32_t) chunk[i*4+1] << 16)
                     | ((uint32_t) chunk[i*4+2] << 8) |  (uint32_t) chunk[i*4+3];

            for (int i = 16; i < 64; ++i)
            {
                const uint32_t s0 = rotr (w[i-15], 7) ^ rotr (w[i-15], 18) ^ (w[i-15] >> 3);
                const uint32_t s1 = rotr (w[i-2], 17) ^ rotr (w[i-2],  19) ^ (w[i-2]  >> 10);
                w[i] = w[i-16] + s0 + w[i-7] + s1;
            }

            uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
            uint32_t e = state[4], f = state[5], g = state[6], h = state[7];

            for (int i = 0; i < 64; ++i)
            {
                const uint32_t S1 = rotr (e, 6) ^ rotr (e, 11) ^ rotr (e, 25);
                const uint32_t ch = (e & f) ^ ((~e) & g);
                const uint32_t t1 = h + S1 + ch + K[i] + w[i];
                const uint32_t S0 = rotr (a, 2) ^ rotr (a, 13) ^ rotr (a, 22);
                const uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
                const uint32_t t2 = S0 + mj;
                h = g; g = f; f = e; e = d + t1;
                d = c; c = b; b = a; a = t1 + t2;
            }

            state[0]+=a; state[1]+=b; state[2]+=c; state[3]+=d;
            state[4]+=e; state[5]+=f; state[6]+=g; state[7]+=h;
        }

        uint32_t state[8] {};
        uint8_t  buffer[64] {};
        uint64_t bitLen = 0;
        size_t   bufLen = 0;
    };

    //==========================================================================
    /** HMAC-SHA256, RFC 2104.  `out` receives 32 bytes. */
    inline void hmacSha256 (const uint8_t* key, size_t keyLen,
                            const void* msg, size_t msgLen,
                            uint8_t* out) noexcept
    {
        constexpr size_t BLOCK = 64;

        uint8_t k[BLOCK] = {};

        // A key longer than the block size is hashed first.  This branch is
        // rarely exercised in practice, which is exactly why RFC 4231 case 6
        // is in the test set.
        if (keyLen > BLOCK)  Sha256::hash (key, keyLen, k);
        else                 std::memcpy (k, key, keyLen);

        uint8_t ipad[BLOCK], opad[BLOCK];
        for (size_t i = 0; i < BLOCK; ++i)
        {
            ipad[i] = (uint8_t) (k[i] ^ 0x36);
            opad[i] = (uint8_t) (k[i] ^ 0x5c);
        }

        uint8_t inner[32];
        { Sha256 h; h.update (ipad, BLOCK); h.update (msg, msgLen); h.finish (inner); }
        { Sha256 h; h.update (opad, BLOCK); h.update (inner, 32);   h.finish (out);   }
    }

    //==========================================================================
    /** THE SERIAL.  Must match the website generator bit for bit:

            mac    = HMAC-SHA256 (secret, machineID as ASCII)
            first8 = the FIRST 8 bytes of mac
            n      = first8 as a BIG-ENDIAN uint64
            serial = n mod 1e9, zero-padded to 9 digits

        Returns the numeric value; the caller formats it.  `machineId` must be
        the trimmed plain decimal string - no padding, no whitespace. */
    inline uint64_t serialForMachineId (const uint8_t* secret, size_t secretLen,
                                        const std::string& machineId) noexcept
    {
        uint8_t mac[32];
        hmacSha256 (secret, secretLen, machineId.data(), machineId.size(), mac);

        uint64_t n = 0;
        for (int i = 0; i < 8; ++i)            // big-endian, deliberately
            n = (n << 8) | (uint64_t) mac[i];

        return n % 1000000000ull;
    }
}




