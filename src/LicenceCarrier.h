
/*
    ============================================================================
    LicenceCarrier.h  —  reads the secret key out of the carrier audio file.
    ============================================================================

    WHAT THIS IS

    The HMAC secret is not a constant in the binary.  It lives in the LSBs of a
    16-bit WAV that ships as an ordinary-looking audio asset, scattered across
    the file by a seeded index set.  This reads it back.

    ── BE HONEST ABOUT WHAT IT BUYS ───────────────────────────────────────────

    This is OBFUSCATION, not cryptography.  The entry seed and this extractor
    both ship inside Grex, so anyone willing to read the binary can follow the
    same steps and recover the key.  What it buys is effort: the key is not
    findable with `strings`, not visible in a hex dump, and not recoverable by
    diffing two builds.  That is a real increase in the work required, and it is
    the same trade OnStage's MIDI carrier made.  It is not a guarantee.

    ── WHY THE WAV IS PARSED BY HAND ──────────────────────────────────────────

    We do NOT use juce::WavAudioFormat here, deliberately.  StegoAudio was bitten
    by exactly this: JUCE's WAV *writer* converts float32 samples to 32-bit int
    and destroys embedded LSB data, which forced hand-written IEEE float headers.
    The reader is a different code path, but the lesson generalises - anything
    that converts, dithers or normalises samples destroys the payload.  Reading
    the int16 words straight out of the data chunk cannot convert anything, so
    there is nothing to go wrong.

    The carrier is 16-bit integer PCM for the same reason: an LSB in an int16 is
    exact and survives any correct reader.  36 bytes of payload does not need
    float32's capacity.

    ── FORMAT ─────────────────────────────────────────────────────────────────

        payload  = "GRXK" (4 bytes) + secret key (32 bytes) = 36 bytes = 288 bits
        indices  = splitmix64 seeded with kEntrySeed, first 288 DISTINCT values
                   modulo the total sample count
        bit      = the LSB of |sample|, taken MSB-first within each byte

    The magic is not a secret.  It exists so a truncated, resampled or replaced
    file is detected as such rather than yielding a plausible-looking wrong key.

    THE MAGIC ALONE IS NOT ENOUGH, which is why there is also a checksum.  The
    magic only covers the FIRST 32 BITS of the payload, so a single flipped bit
    anywhere in the key region sails straight past it and hands back a silently
    wrong key - and the symptom is "every serial is rejected", with nothing to
    point at the carrier.  The 4-byte tag is the first four bytes of
    SHA-256(magic + key), so ANY damage anywhere in the payload is caught and
    the failure is honest.
*/

#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>
#include <cstring>

#include "HmacSha256.h"   // Sha256, for the payload integrity tag

namespace Betel
{
    struct LicenceCarrier
    {
        static constexpr uint64_t kEntrySeed  = 0x9E3779B97F4A7C15ull;
        static constexpr size_t   kKeyBytes   = 32;
        static constexpr size_t   kMagicBytes = 4;
        static constexpr size_t   kCheckBytes = 4;
        static constexpr size_t   kPayload    = kMagicBytes + kKeyBytes + kCheckBytes;  // 40

        /** Pull the key out of a whole WAV file already read into memory.

            Returns true and fills `keyOut` (32 bytes) on success.  Returns false
            for anything at all wrong - not a RIFF/WAVE file, not 16-bit PCM, too
            short, or the magic does not match.  FAILING CLOSED IS THE POINT: a
            damaged carrier must mean "cannot verify", never "verify against
            whatever bytes happened to come out". */
        static bool extractKey (const uint8_t* fileData, size_t fileSize,
                                uint8_t* keyOut) noexcept
        {
            if (fileData == nullptr || fileSize < 44) return false;

            if (std::memcmp (fileData, "RIFF", 4) != 0
             || std::memcmp (fileData + 8, "WAVE", 4) != 0)
                return false;

            // ── Walk the chunks.  Do not assume fmt/data sit at fixed offsets:
            // real writers insert LIST, fact and other chunks ahead of them.
            const uint8_t* data = nullptr;
            uint32_t       dataSize = 0;
            uint16_t       bitsPerSample = 0, formatTag = 0;

            size_t p = 12;
            while (p + 8 <= fileSize)
            {
                const uint32_t sz = rd32 (fileData + p + 4);

                if (std::memcmp (fileData + p, "fmt ", 4) == 0 && sz >= 16
                    && p + 8 + 16 <= fileSize)
                {
                    formatTag     = rd16 (fileData + p + 8);
                    bitsPerSample = rd16 (fileData + p + 8 + 14);
                }
                else if (std::memcmp (fileData + p, "data", 4) == 0)
                {
                    data = fileData + p + 8;
                    // Trust the smaller of the declared size and what is really
                    // there, so a truncated download cannot read past the end.
                    const size_t avail = fileSize - (p + 8);
                    dataSize = (uint32_t) (sz < avail ? sz : avail);
                }

                p += 8 + sz + (sz & 1);        // chunks are word-aligned
            }

            if (data == nullptr || formatTag != 1 || bitsPerSample != 16) return false;

            const size_t numSamples = dataSize / 2;
            if (numSamples < kPayload * 8) return false;

            const auto idx = buildIndices (kEntrySeed, kPayload * 8, numSamples);

            uint8_t payload[kPayload] = {};
            for (size_t b = 0; b < kPayload * 8; ++b)
            {
                const size_t   s   = idx[b];
                const int16_t  raw = (int16_t) rd16 (data + s * 2);
                const uint32_t mag = (uint32_t) (raw < 0 ? -(int32_t) raw : (int32_t) raw);

                if (mag & 1u)
                    payload[b >> 3] |= (uint8_t) (1u << (7 - (b & 7)));
            }

            if (payload[0] != 'G' || payload[1] != 'R'
             || payload[2] != 'X' || payload[3] != 'K')
                return false;

            // Integrity tag over magic+key.  Covers the whole payload, unlike
            // the magic, so a single flipped bit in the key cannot pass.
            uint8_t digest[32];
            Sha256::hash (payload, kMagicBytes + kKeyBytes, digest);

            for (size_t i = 0; i < kCheckBytes; ++i)
                if (digest[i] != payload[kMagicBytes + kKeyBytes + i])
                    return false;

            std::memcpy (keyOut, payload + kMagicBytes, kKeyBytes);
            return true;
        }

    private:
        static uint16_t rd16 (const uint8_t* p) noexcept
        {
            return (uint16_t) (p[0] | ((uint16_t) p[1] << 8));           // little-endian
        }

        static uint32_t rd32 (const uint8_t* p) noexcept
        {
            return (uint32_t) p[0] | ((uint32_t) p[1] << 8)
                 | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24);
        }

        /** splitmix64.  Must match the generator exactly - same constants, same
            order, same "skip duplicates" rule. */
        static std::vector<size_t> buildIndices (uint64_t seed, size_t count,
                                                 size_t total)
        {
            std::vector<size_t>   out;
            std::vector<uint8_t>  used (total, 0);
            out.reserve (count);

            uint64_t st = seed;
            while (out.size() < count)
            {
                st += 0x9E3779B97F4A7C15ull;
                uint64_t z = st;
                z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
                z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
                z =  z ^ (z >> 31);

                const size_t i = (size_t) (z % (uint64_t) total);
                if (! used[i]) { used[i] = 1; out.push_back (i); }
            }
            return out;
        }
    };
}




