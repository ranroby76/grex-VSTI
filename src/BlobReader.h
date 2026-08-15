#pragma once
//==============================================================================
// BlobReader.h  —  memory-mapped .frb blob reader
//
// HOW IT WORKS
//   open()             maps the entire file into virtual address space instantly.
//                      The OS pages data in from disk only as bytes are touched —
//                      no upfront fread, no giant pool buffer.
//   getSampleDataFloat() decrypts only the requested region using seekable
//                      ChaCha20 (seeks to region.sampleOffset in the keystream
//                      without processing any preceding bytes).
//   ~BlobReader        unmaps the file automatically.
//
// MEMORY COST AFTER open()
//   Header + TOC + region metadata  ← always resident (small, < 1 MB)
//   Sample pool                     ← paged in on demand by the OS
//   No persistent decrypted pool buffer.
//
// VERSION COMPATIBILITY
//   v1, v2: read in legacy region layout (no elementRoleId trailer).
//           The region's elementRoleId stays 0 (DrumElementRole::Unset).
//   v3+:    reads `elementRoleId` + 7 reserved bytes AFTER `modulatorCount`
//           and BEFORE the modulator array. This is how drum-kit blobs tag
//           each region with its kick/snare/hat/etc. role so the DrumsPopup
//           UI can let the user swap elements between kits.
//
// USAGE
//   BlobReader reader;
//   if (reader.open("MyLibrary.frb", "accessCode"))
//       for (auto& preset : reader.getPresets())
//           for (auto& region : preset.regions)
//               auto fs = reader.getSampleDataFloat(region); // decrypt + convert
//==============================================================================

#include "BlobFormat.h"
#include "AccessCode.h"
#include <JuceHeader.h>
#include <cstring>
#include <unordered_map>

class BlobReader
{
public:
    BlobReader()  = default;
    ~BlobReader() = default;

    //==========================================================================
    struct FloatSample
    {
        std::vector<float> data; // interleaved: s0ch0 s0ch1 s1ch0 s1ch1 …
        uint32_t sampleRate  = 44100;
        uint16_t channels    = 1;
        int      numFrames   = 0;
    };

    //==========================================================================
    // open() — maps the file and parses header / TOC / regions.
    // Returns false and sets getError() on any failure.
    //==========================================================================
    bool open(const std::string& filePath, const std::string& accessCode)
    {
        close();
        errorMsg.clear();
        currentAccessCode = accessCode;

        // ---- Memory-map the file ----------------------------------------
        juce::File juceFile(filePath);
        if (!juceFile.existsAsFile())
        { errorMsg = "File not found: " + filePath; return false; }

        mappedFile = std::make_unique<juce::MemoryMappedFile>(
            juceFile, juce::MemoryMappedFile::readOnly, false);

        if (mappedFile->getData() == nullptr)
        { errorMsg = "Cannot memory-map: " + filePath; mappedFile.reset(); return false; }

        mappedData = static_cast<const uint8_t*>(mappedFile->getData());
        mappedSize = static_cast<size_t>(mappedFile->getSize());

        // ---- Header ---------------------------------------------------------
        if (mappedSize < sizeof(BlobFormat::FileHeader))
        { errorMsg = "File too small to be a valid .frb"; return false; }

        std::memcpy(&storedHeader, mappedData, sizeof(BlobFormat::FileHeader));

        if (storedHeader.magic != BlobFormat::kMagic)
        { errorMsg = "Not a Fanan .frb file (bad magic)"; return false; }
        if (storedHeader.version < 1 || storedHeader.version > BlobFormat::kVersion)
        { errorMsg = "Unsupported .frb version"; return false; }
        if (!accessCode.empty() && !AccessCode::verify(storedHeader.accessHash, accessCode))
        { errorMsg = "Invalid access code"; return false; }

        // ---- TOC ------------------------------------------------------------
        size_t cursor = static_cast<size_t>(storedHeader.tocOffset);
        tocEntries.resize(storedHeader.presetCount);

        for (uint32_t i = 0; i < storedHeader.presetCount; ++i)
        {
            if (cursor + sizeof(BlobFormat::TocEntry) > mappedSize)
            { errorMsg = "TOC extends past end of file"; return false; }

            std::memcpy(&tocEntries[i], mappedData + cursor, sizeof(BlobFormat::TocEntry));
            cursor += sizeof(BlobFormat::TocEntry);
            nameIndex[std::string(tocEntries[i].name)] = i;
        }

        // ---- Regions --------------------------------------------------------
        presets.resize(storedHeader.presetCount);
        const bool v3OrLater = (storedHeader.version >= 3);

        for (uint32_t i = 0; i < storedHeader.presetCount; ++i)
        {
            auto& toc    = tocEntries[i];
            auto& preset = presets[i];
            std::strncpy(preset.name, toc.name, 127);
            preset.name[127]    = '\0';
            preset.bank         = toc.bank;
            preset.program      = toc.program;
            preset.sourceFormat = toc.sourceFormat;
            preset.regions.resize(toc.regionCount);

            size_t rc = static_cast<size_t>(toc.regionOffset);

            auto readBytes = [&](void* dst, size_t n) -> bool {
                if (rc + n > mappedSize) return false;
                std::memcpy(dst, mappedData + rc, n);
                rc += n;
                return true;
            };
            auto readVal = [&](auto& val) { return readBytes(&val, sizeof(val)); };

            for (uint32_t r = 0; r < toc.regionCount; ++r)
            {
                auto& reg = preset.regions[r];
                bool ok = true;
                ok &= readVal(reg.keyRangeLow);
                ok &= readVal(reg.keyRangeHigh);
                ok &= readVal(reg.velRangeLow);
                ok &= readVal(reg.velRangeHigh);
                ok &= readVal(reg.rootKey);
                ok &= readVal(reg.fineTune);
                ok &= readVal(reg.coarseTune);
                ok &= readVal(reg.sampleOffset);
                ok &= readVal(reg.sampleSize);
                ok &= readVal(reg.sampleRate);
                ok &= readVal(reg.channels);
                ok &= readVal(reg.bitDepth);
                ok &= readVal(reg.loopEnabled);
                ok &= readVal(reg.loopStart);
                ok &= readVal(reg.loopEnd);
                ok &= readVal(reg.attenuation);
                ok &= readVal(reg.pan);
                ok &= readVal(reg.volumeEnv);
                ok &= readVal(reg.modulationEnv);
                ok &= readVal(reg.filter);
                ok &= readVal(reg.vibLFO);
                ok &= readVal(reg.modLFO);
                ok &= readVal(reg.exclusiveClass);
                ok &= readVal(reg.sampleType);
                ok &= readVal(reg.modulatorCount);

                if (!ok) { errorMsg = "Region data truncated"; return false; }

                // ── v3+: read drum-element role tag + reserved padding ──────
                // For v1/v2 blobs, leave elementRoleId = 0 (= Unset / not a
                // drum-kit blob).  Engine treats Unset as "use defaults".
                if (v3OrLater)
                {
                    if (! readVal(reg.elementRoleId))
                    { errorMsg = "Region elementRoleId truncated"; return false; }
                    if (! readBytes(reg.reservedV3, sizeof(reg.reservedV3)))
                    { errorMsg = "Region v3 reserved bytes truncated"; return false; }
                }

                reg.modulators.resize(reg.modulatorCount);
                for (uint16_t m = 0; m < reg.modulatorCount; ++m)
                    if (!readVal(reg.modulators[m]))
                    { errorMsg = "Modulator data truncated"; return false; }
            }
        }

        isOpen = true;
        return true;
    }

    //==========================================================================
    void close()
    {
        isOpen    = false;
        mappedData = nullptr;
        mappedSize = 0;
        mappedFile.reset();
        presets.clear();
        tocEntries.clear();
        nameIndex.clear();
        currentAccessCode.clear();
        storedHeader = {};
    }

    //==========================================================================
    // getSampleDataFloat
    //
    // Reads region.sampleSize bytes directly from mapped memory (OS pages them
    // in from disk on demand), decrypts with seekable ChaCha20 into a small
    // temporary buffer, then converts int16 → float.
    //
    // Memory allocated = 1× region size (decrypt buffer) + 2× float data.
    // No global pool buffer needed.
    //==========================================================================
    FloatSample getSampleDataFloat(const BlobFormat::Region& region) const
    {
        FloatSample out;
        if (!isOpen || region.sampleSize == 0) return out;

        const uint64_t absOffset = storedHeader.dataOffset + region.sampleOffset;
        if (absOffset + region.sampleSize > static_cast<uint64_t>(mappedSize))
        { return out; }

        const uint8_t* src    = mappedData + absOffset;
        const int      ch     = static_cast<int>(region.channels);
        const int      total  = static_cast<int>(region.sampleSize / sizeof(int16_t));
        const int      frames = (ch > 0) ? total / ch : 0;

        // Decrypt just this region (seekable — O(region) not O(whole pool))
        std::vector<uint8_t> decrypted(region.sampleSize);
        if (storedHeader.version >= 2 && !currentAccessCode.empty())
        {
            // region.sampleOffset is the byte offset within the pool,
            // which equals the position in the ChaCha20 keystream.
            AccessCode::Crypto::cipherRegion(
                decrypted.data(), src,
                static_cast<size_t>(region.sampleSize),
                region.sampleOffset,
                currentAccessCode);
        }
        else
        {
            std::memcpy(decrypted.data(), src, region.sampleSize);
        }

        // int16 → float
        out.sampleRate = region.sampleRate;
        out.channels   = region.channels;
        out.numFrames  = frames;
        out.data.resize(static_cast<size_t>(total));

        const auto*   raw = reinterpret_cast<const int16_t*>(decrypted.data());
        constexpr float k = 1.0f / 32768.0f;
        for (int i = 0; i < total; ++i)
            out.data[static_cast<size_t>(i)] = static_cast<float>(raw[i]) * k;

        return out;
    }

    // Raw int16 version (for tools / diagnostics)
    std::vector<int16_t> getSampleData(const BlobFormat::Region& region) const
    {
        if (!isOpen || region.sampleSize == 0) return {};
        const uint64_t abs = storedHeader.dataOffset + region.sampleOffset;
        if (abs + region.sampleSize > static_cast<uint64_t>(mappedSize)) return {};

        const uint8_t* src = mappedData + abs;
        std::vector<uint8_t> dec(region.sampleSize);

        if (storedHeader.version >= 2 && !currentAccessCode.empty())
            AccessCode::Crypto::cipherRegion(dec.data(), src,
                static_cast<size_t>(region.sampleSize),
                region.sampleOffset, currentAccessCode);
        else
            std::memcpy(dec.data(), src, region.sampleSize);

        const size_t count = region.sampleSize / sizeof(int16_t);
        std::vector<int16_t> out(count);
        std::memcpy(out.data(), dec.data(), region.sampleSize);
        return out;
    }

    //==========================================================================
    const BlobFormat::Preset* findPreset(const std::string& name) const
    {
        auto it = nameIndex.find(name);
        return it != nameIndex.end() ? &presets[it->second] : nullptr;
    }

    const BlobFormat::Preset* findPreset(uint16_t bank, uint16_t program) const
    {
        for (auto& p : presets)
            if (p.bank == bank && p.program == program) return &p;
        return nullptr;
    }

    const std::vector<BlobFormat::Preset>& getPresets()     const { return presets; }
    uint32_t                               getPresetCount() const { return storedHeader.presetCount; }
    uint32_t                               getFileVersion() const { return storedHeader.version; }

    std::vector<std::string> getPresetNames() const
    {
        std::vector<std::string> names;
        names.reserve(presets.size());
        for (auto& p : presets) names.emplace_back(p.name);
        return names;
    }

    bool               isLoaded()  const { return isOpen; }
    const std::string& getError()  const { return errorMsg; }

private:
    std::unique_ptr<juce::MemoryMappedFile>          mappedFile;
    const uint8_t*                                   mappedData = nullptr;
    size_t                                           mappedSize = 0;

    BlobFormat::FileHeader                           storedHeader {};
    std::vector<BlobFormat::TocEntry>                tocEntries;
    std::vector<BlobFormat::Preset>                  presets;
    std::unordered_map<std::string, uint32_t>        nameIndex;
    std::string                                      currentAccessCode;
    std::string                                      errorMsg;
    bool                                             isOpen = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BlobReader)
};
