#pragma once
#include <cstdint>
#include <vector>
#include <string>

namespace BlobFormat
{
    static constexpr uint32_t kMagic   = 0x424C4253; // "SBLB"
    // v3 = adds per-region `elementRoleId` byte for drum-kit blobs.
    //      Field is read only for files whose header.version >= 3; v1/v2
    //      blobs are still openable (the field is implicitly 0 = Unset).
    static constexpr uint32_t kVersion = 3;
    static constexpr size_t   kAccessCodeHashSize = 32;

    //==========================================================================
    // Envelope (DAHDSR)
    //==========================================================================
    #pragma pack(push, 1)
    struct Envelope
    {
        float delaySeconds    = 0.0f;
        float attackSeconds   = 0.001f;
        float holdSeconds     = 0.0f;
        float decaySeconds    = 0.001f;
        float sustainLevel    = 1.0f;
        float releaseSeconds  = 0.001f;
        int16_t keynumToHold  = 0;
        int16_t keynumToDecay = 0;
    };
    #pragma pack(pop)

    //==========================================================================
    // Filter
    //==========================================================================
    #pragma pack(push, 1)
    struct Filter
    {
        uint8_t type       = 0;       // 0=lowpass, 1=highpass, 2=bandpass
        float   cutoffHz   = 20000.0f;
        float   resonanceQ = 0.0f;    // dB
        float   envAmount  = 0.0f;    // cents
    };
    #pragma pack(pop)

    //==========================================================================
    // LFO
    //==========================================================================
    #pragma pack(push, 1)
    struct LFO
    {
        float delaySeconds = 0.0f;
        float frequencyHz  = 0.0f;
        float toPitch      = 0.0f;    // cents
        float toFilter     = 0.0f;    // cents
        float toVolume     = 0.0f;    // centibels
    };
    #pragma pack(pop)

    //==========================================================================
    // Modulator (SF2-style)
    //==========================================================================
    #pragma pack(push, 1)
    struct Modulator
    {
        uint16_t srcOper    = 0;
        uint16_t destOper   = 0;
        int16_t  amount     = 0;
        uint16_t amtSrcOper = 0;
        uint16_t transOper  = 0;
    };
    #pragma pack(pop)

    //==========================================================================
    // RegionPOD — all fixed-size fields, safe inside #pragma pack
    // (Do NOT add std::vector or any non-POD here)
    //
    // VERSION COMPATIBILITY:
    //   v1, v2: read every field EXCEPT `elementRoleId`. The reader leaves
    //           `elementRoleId = 0` (DrumElementRole::Unset) for those blobs.
    //   v3+:    `elementRoleId` is appended to the on-disk stream AFTER
    //           `modulatorCount` and BEFORE the variable-length modulator
    //           array.  Writers MUST emit it; readers MUST consume it for v3+.
    //
    // The field tags a region with its drum-kit role (kick / snare / closed
    // hi-hat / etc.) so the DrumsPopup UI can let the user replace any region
    // with another region of the same role from any other loaded kit-blob.
    // For non-drum (melodic) presets, writers leave it at 0 (Unset) and the
    // engine ignores it.
    //==========================================================================
    #pragma pack(push, 1)
    struct RegionPOD
    {
        uint8_t  keyRangeLow   = 0;
        uint8_t  keyRangeHigh  = 127;
        uint8_t  velRangeLow   = 0;
        uint8_t  velRangeHigh  = 127;
        uint8_t  rootKey       = 60;
        int8_t   fineTune      = 0;    // cents
        int8_t   coarseTune    = 0;    // semitones

        uint64_t sampleOffset  = 0;    // bytes from start of sample pool
        uint64_t sampleSize    = 0;    // bytes
        uint32_t sampleRate    = 44100;
        uint16_t channels      = 1;
        uint16_t bitDepth      = 16;

        uint8_t  loopEnabled   = 0;    // 0=none, 1=loop_continuous, 2=loop_sustain
        uint32_t loopStart     = 0;    // sample frames
        uint32_t loopEnd       = 0;

        float    attenuation   = 0.0f; // dB (positive = quieter, like SF2 convention)
        int16_t  pan           = 0;    // -500..+500 (SF2 units; divide by 10 for percent)

        Envelope volumeEnv;
        Envelope modulationEnv;
        Filter   filter;
        LFO      vibLFO;
        LFO      modLFO;

        uint16_t exclusiveClass = 0;
        uint16_t sampleType     = 0;
        uint16_t modulatorCount = 0;   // number of Modulator records that follow in the stream

        // ── v3+ fields ────────────────────────────────────────────────────────
        // Drum element role (DrumElementRole enum value). 0 = Unset.
        // Stored on disk only for v3+ blobs; v1/v2 readers default it to 0.
        uint8_t  elementRoleId  = 0;
        uint8_t  reservedV3[7]  = {};  // pad to keep future v4 additions simple
    };
    #pragma pack(pop)

    //==========================================================================
    // Region — extends RegionPOD with the variable-length modulator list.
    // NOT packed as a block; modulators are serialised separately after the POD.
    //==========================================================================
    struct Region : RegionPOD
    {
        std::vector<Modulator> modulators; // size == modulatorCount
    };

    //==========================================================================
    // Preset (in-memory, one per SFZ/SF2 preset)
    //==========================================================================
    struct Preset
    {
        char     name[128]    = {};
        uint16_t bank         = 0;
        uint16_t program      = 0;
        uint8_t  sourceFormat = 0;   // 0=SF2, 1=SFZ
        std::vector<Region> regions;
    };

    //==========================================================================
    // File Header (on-disk, always at offset 0)
    //==========================================================================
    #pragma pack(push, 1)
    struct FileHeader
    {
        uint32_t magic       = kMagic;
        uint32_t version     = kVersion;
        uint8_t  accessHash[kAccessCodeHashSize] = {};
        uint32_t presetCount = 0;
        uint64_t tocOffset   = 0;    // offset to table of contents
        uint64_t dataOffset  = 0;    // offset to sample data pool
        uint64_t dataSize    = 0;    // total byte size of sample data pool
        uint8_t  reserved[32] = {};
    };
    #pragma pack(pop)

    //==========================================================================
    // TOC Entry (on-disk, one per preset, fixed size for fast seeking)
    //==========================================================================
    #pragma pack(push, 1)
    struct TocEntry
    {
        char     name[128]    = {};
        uint16_t bank         = 0;
        uint16_t program      = 0;
        uint8_t  sourceFormat = 0;
        uint32_t regionCount  = 0;
        uint64_t regionOffset = 0;   // byte offset to this preset's RegionPOD array
        uint8_t  reserved[16] = {};
    };
    #pragma pack(pop)
}
