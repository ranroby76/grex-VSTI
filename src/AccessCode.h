#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <array>
#include <vector>
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace AccessCode
{
    // =========================================================================
    // SHA-256
    // =========================================================================
    namespace detail
    {
        static constexpr uint32_t k[64] = {
            0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
            0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
            0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
            0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
            0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
            0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
            0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
            0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
        };
        inline uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
        inline uint32_t ch(uint32_t x, uint32_t y, uint32_t z)  { return (x & y) ^ (~x & z); }
        inline uint32_t maj(uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (x & z) ^ (y & z); }
        inline uint32_t ep0(uint32_t x)  { return rotr(x,2)  ^ rotr(x,13) ^ rotr(x,22); }
        inline uint32_t ep1(uint32_t x)  { return rotr(x,6)  ^ rotr(x,11) ^ rotr(x,25); }
        inline uint32_t sig0(uint32_t x) { return rotr(x,7)  ^ rotr(x,18) ^ (x >> 3);  }
        inline uint32_t sig1(uint32_t x) { return rotr(x,17) ^ rotr(x,19) ^ (x >> 10); }
    }

    inline std::array<uint8_t, 32> sha256(const std::string& input)
    {
        using namespace detail;
        uint32_t h0=0x6a09e667, h1=0xbb67ae85, h2=0x3c6ef372, h3=0xa54ff53a;
        uint32_t h4=0x510e527f, h5=0x9b05688c, h6=0x1f83d9ab, h7=0x5be0cd19;

        std::vector<uint8_t> msg(input.begin(), input.end());
        uint64_t bitLen = msg.size() * 8;
        msg.push_back(0x80);
        while (msg.size() % 64 != 56) msg.push_back(0);
        for (int i = 7; i >= 0; --i)
            msg.push_back(static_cast<uint8_t>((bitLen >> (i * 8)) & 0xFF));

        for (size_t offset = 0; offset < msg.size(); offset += 64)
        {
            uint32_t w[64];
            for (int i = 0; i < 16; i++)
                w[i] = (uint32_t(msg[offset+i*4])<<24)|(uint32_t(msg[offset+i*4+1])<<16)|
                        (uint32_t(msg[offset+i*4+2])<<8)|uint32_t(msg[offset+i*4+3]);
            for (int i = 16; i < 64; i++)
                w[i] = sig1(w[i-2]) + w[i-7] + sig0(w[i-15]) + w[i-16];

            uint32_t a=h0,b=h1,c=h2,d=h3,e=h4,f=h5,g=h6,hh=h7;
            for (int i = 0; i < 64; i++)
            {
                uint32_t t1 = hh + ep1(e) + ch(e,f,g) + k[i] + w[i];
                uint32_t t2 = ep0(a) + maj(a,b,c);
                hh=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
            }
            h0+=a; h1+=b; h2+=c; h3+=d; h4+=e; h5+=f; h6+=g; h7+=hh;
        }

        std::array<uint8_t, 32> hash;
        auto store = [&](int idx, uint32_t val) {
            hash[idx*4]=(val>>24)&0xFF; hash[idx*4+1]=(val>>16)&0xFF;
            hash[idx*4+2]=(val>>8)&0xFF; hash[idx*4+3]=val&0xFF;
        };
        store(0,h0); store(1,h1); store(2,h2); store(3,h3);
        store(4,h4); store(5,h5); store(6,h6); store(7,h7);
        return hash;
    }

    inline std::string hashToHex(const std::array<uint8_t, 32>& hash)
    {
        std::ostringstream oss;
        for (auto b : hash)
            oss << std::hex << std::setfill('0') << std::setw(2) << (int)b;
        return oss.str();
    }

    inline bool verify(const uint8_t storedHash[32], const std::string& code)
    {
        auto computed = sha256(code);
        return std::memcmp(storedHash, computed.data(), 32) == 0;
    }

    // =========================================================================
    // ChaCha20 stream cipher
    //
    // Used to encrypt/decrypt the sample pool inside blob files.
    // Key is derived from the access code via SHA-256.
    // Nonce is fixed (embedded in the format) — the access code provides entropy.
    // Encryption == decryption (XOR with keystream).
    // =========================================================================
    namespace Crypto
    {
        // Fixed nonce baked into the blob format (do not change after shipping packs)
        static constexpr uint8_t kNonce[12] = {
            'F','a','n','a','n','B','l','o','b','1','0','0'
        };

        // Domain-separated key derivation: SHA-256(accessCode + fixed salt)
        inline std::array<uint8_t, 32> deriveKey(const std::string& accessCode)
        {
            return sha256(accessCode + "FananTeamBlobV1_2025");
        }

        struct ChaCha20
        {
            uint32_t state[16] = {};

            static uint32_t rotl32(uint32_t v, int n)
            { return (v << n) | (v >> (32 - n)); }

            void init(const uint8_t key[32], const uint8_t nonce[12], uint32_t counter)
            {
                // "expand 32-byte k" constant
                state[0] = 0x61707865u;
                state[1] = 0x3320646eu;
                state[2] = 0x79622d32u;
                state[3] = 0x6b206574u;
                // Key (little-endian)
                for (int i = 0; i < 8; ++i)
                    state[4+i] = static_cast<uint32_t>(key[i*4])
                               | static_cast<uint32_t>(key[i*4+1]) <<  8
                               | static_cast<uint32_t>(key[i*4+2]) << 16
                               | static_cast<uint32_t>(key[i*4+3]) << 24;
                // Counter
                state[12] = counter;
                // Nonce (little-endian)
                for (int i = 0; i < 3; ++i)
                    state[13+i] = static_cast<uint32_t>(nonce[i*4])
                                | static_cast<uint32_t>(nonce[i*4+1]) <<  8
                                | static_cast<uint32_t>(nonce[i*4+2]) << 16
                                | static_cast<uint32_t>(nonce[i*4+3]) << 24;
            }

            // Generate one 64-byte keystream block and advance the counter
            void block(uint8_t out[64])
            {
                uint32_t x[16];
                std::memcpy(x, state, sizeof(x));

#define QR(a,b,c,d) \
    x[a]+=x[b]; x[d]^=x[a]; x[d]=rotl32(x[d],16); \
    x[c]+=x[d]; x[b]^=x[c]; x[b]=rotl32(x[b],12); \
    x[a]+=x[b]; x[d]^=x[a]; x[d]=rotl32(x[d], 8); \
    x[c]+=x[d]; x[b]^=x[c]; x[b]=rotl32(x[b], 7)

                for (int i = 0; i < 10; ++i)
                {
                    QR(0,4, 8,12); QR(1,5, 9,13); QR(2,6,10,14); QR(3,7,11,15); // columns
                    QR(0,5,10,15); QR(1,6,11,12); QR(2,7, 8,13); QR(3,4, 9,14); // diagonals
                }
#undef QR

                for (int i = 0; i < 16; ++i)
                {
                    uint32_t v = x[i] + state[i];
                    out[i*4+0] =  v        & 0xFF;
                    out[i*4+1] = (v >>  8) & 0xFF;
                    out[i*4+2] = (v >> 16) & 0xFF;
                    out[i*4+3] = (v >> 24) & 0xFF;
                }
                ++state[12]; // increment block counter
            }

            // XOR data with keystream in-place (encrypt == decrypt)
            void process(uint8_t* data, size_t len)
            {
                uint8_t keystream[64];
                size_t pos = 0;
                while (pos < len)
                {
                    block(keystream);
                    size_t chunk = std::min(static_cast<size_t>(64), len - pos);
                    for (size_t i = 0; i < chunk; ++i)
                        data[pos + i] ^= keystream[i];
                    pos += chunk;
                }
            }
        };

        // Convenience: encrypt or decrypt a byte buffer (same operation)
        inline void cipherSamples(uint8_t* data, size_t size, const std::string& accessCode)
        {
            if (accessCode.empty() || size == 0) return;
            auto key = deriveKey(accessCode);
            ChaCha20 cc;
            cc.init(key.data(), kNonce, 0);
            cc.process(data, size);
        }


        // Seekable per-region decrypt
        // ChaCha20 is a stream cipher: the keystream at any byte offset can be
        // reached by setting counter = offset / 64 and skipping (offset % 64)
        // bytes within that first block.  This lets us decrypt one region
        // without processing the entire pool — essential for mmap loading.
        inline void cipherRegion(uint8_t*       dst,
                                  const uint8_t* src,
                                  size_t         size,
                                  uint64_t       streamOffset,  // byte offset within sample pool
                                  const std::string& accessCode)
        {
            if (size == 0) return;
            if (accessCode.empty()) { if (dst != src) std::memcpy(dst, src, size); return; }

            auto key = deriveKey(accessCode);
            ChaCha20 cc;

            // Seek to the right 64-byte block in the keystream
            const uint32_t blockIndex = static_cast<uint32_t>(streamOffset / 64);
            const uint32_t byteSkip   = static_cast<uint32_t>(streamOffset % 64);
            cc.init(key.data(), kNonce, blockIndex);

            uint8_t ks[64];
            size_t  pos = 0;

            // Partial first block (if stream offset is not block-aligned)
            if (byteSkip > 0)
            {
                cc.block(ks);
                const size_t use = std::min(static_cast<size_t>(64 - byteSkip), size);
                for (size_t i = 0; i < use; ++i)
                    dst[i] = src[i] ^ ks[byteSkip + i];
                pos = use;
            }

            // Full 64-byte blocks
            while (pos + 64 <= size)
            {
                cc.block(ks);
                for (int i = 0; i < 64; ++i)
                    dst[pos + i] = src[pos + i] ^ ks[i];
                pos += 64;
            }

            // Remaining bytes in last partial block
            if (pos < size)
            {
                cc.block(ks);
                for (size_t i = 0; i < size - pos; ++i)
                    dst[pos + i] = src[pos + i] ^ ks[i];
            }
        }

    } // namespace Crypto

} // namespace AccessCode
