#include "settings_storage.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "pico/stdlib.h"
#include "pico/error.h"
#include "pico/flash.h"
#include "hardware/flash.h"
#include "hardware/regs/addressmap.h"

namespace
{
    constexpr uint32_t record_magic = 0x54554E52u;
    constexpr uint32_t record_version = 1;
    constexpr unsigned slot_count = 2;

    // These addresses match the reserved region in linker/memory_flash.incl
    static_assert(
        PICO_FLASH_SIZE_BYTES == 2u * 1024u * 1024u,
        "Settings storage is configured for a 2 MiB pico W");

    constexpr uint32_t storage_offset = PICO_FLASH_SIZE_BYTES - slot_count * FLASH_SECTOR_SIZE;

    // Each record only occupies 24 bytes, but erasing operates on 4 KiB 
    // sectors, which is why copies occupy separate sectors
    struct StoredRecord
    {
        uint32_t magic;             // Identifies data belonging to this settings format
        uint32_t version;           // Lets future firmware reject incompatible format safely
        uint32_t sequence;          // Identifies the newest valid copy
        uint32_t filter_hz;
        uint32_t a4_frequency_hz;
        uint32_t checksum;          // Checks the header and settings for corruption
    };

    static_assert(sizeof(StoredRecord) == 24);
    static_assert(offsetof(StoredRecord, checksum) == 20);
    static_assert(sizeof(StoredRecord) <= FLASH_PAGE_SIZE);

    // Prepared in RAM before entering the flash-safe callback
    struct WriteJob
    {
        uint32_t offset;
        uint8_t page[FLASH_PAGE_SIZE];
    };

    uint32_t slot_offset(unsigned slot)
    {
        return storage_offset + slot * FLASH_SECTOR_SIZE;
    }

    // CRC-32 detects damaged or incomplete record contents
    uint32_t crc32(const uint8_t* data, size_t count)
    {
        uint32_t crc = 0xFFFFFFFFu;

        for (size_t i = 0; i < count; ++i)
        {
            crc ^= data[i];

            for (unsigned bit = 0; bit < 8; ++bit)
            {
                const uint32_t mask = 0u - (crc & 1u);
                crc = (crc >> 1u) ^ (0xEDB88320u & mask);
            }
        }

        return ~crc;
    }

    // Computes the provided record's checksum using CRC32 and returns it
    uint32_t record_checksum(const StoredRecord& record)
    {
        return crc32(
            reinterpret_cast<const uint8_t*>(&record),
            offsetof(StoredRecord, checksum));
    }

    /* 
     * Converts the provided FilterMode into an unsigned 32-bit integer frequency value.
     * Returns true if successful; returns false if input is invalid.
     */
    bool encode_filter(FilterMode mode, uint32_t& frequency)
    {
        switch (mode)
        {
            case FilterMode::Off:
                frequency = 0;
                return true;

            case FilterMode::Hz50:
                frequency = 50;
                return true;

            case FilterMode::Hz60:
                frequency = 60;
                return true;
        }

        return false;
    }

    /* 
     * Decodes tuner settings information from the provided record and stores it in settings.
     * Returns true if successful; returns false if record was corrupted or contained invalid
     * settings.
     */
    bool decode_record(const StoredRecord& record, TunerSettings& settings)
    {
        if (record.magic != record_magic ||
            record.version != record_version ||
            record.checksum != record_checksum(record))
        {
            return false;
        }

        if (record.a4_frequency_hz < ReferencePitchLimits::min_hz ||
            record.a4_frequency_hz > ReferencePitchLimits::max_hz)
        {
            return false;
        }

        TunerSettings decoded{};

        switch (record.filter_hz)
        {
            case 0:
                decoded.filter_mode = FilterMode::Off;
                break;

            case 50:
                decoded.filter_mode = FilterMode::Hz50;
                break;

            case 60:
                decoded.filter_mode = FilterMode::Hz60;
                break;

            default:
                return false;
        }

        decoded.a4_frequency_hz = static_cast<uint16_t>(record.a4_frequency_hz);

        settings = decoded;
        return true;
    }

    // Returns true if provided TunerSettings are equal, otherwise false
    bool settings_equal(const TunerSettings& left, const TunerSettings& right)
    {
        return left.filter_mode == right.filter_mode &&
               left.a4_frequency_hz == right.a4_frequency_hz;
    }

    // Compare sequence numbers correctly across unsigned wraparound
    bool newer_sequence(uint32_t candidate, uint32_t previous)
    {
        const uint32_t distance = candidate - previous;
        return distance != 0 && distance < 0x80000000u;
    }

    /* 
     * Attempts to find the latest StoredRecord in flash memory.
     * If successful, updates latest StoredRecord, decoded settings, and latest_slot
     * then returns true. If no valid record is found, returns false.
     */
    bool find_latest(
        StoredRecord& latest,
        TunerSettings& settings,
        unsigned& latest_slot)
    {
        bool found = false;

        for (unsigned slot = 0; slot < slot_count; ++slot)
        {
            StoredRecord candidate{};

            // Get memory address of current slot
            const auto* source = reinterpret_cast<const uint8_t*>(
                XIP_BASE + slot_offset(slot));

            std::memcpy(&candidate, source, sizeof(candidate));

            TunerSettings decoded{};

            if (!decode_record(candidate, decoded))
            {
                continue;
            }

            if (!found || newer_sequence(candidate.sequence, latest.sequence))
            {
                latest = candidate;
                settings = decoded;
                latest_slot = slot;
                found = true;
            }
        }

        return found;
    }

    // Runs from RAM with interrupt safety provided by flash_safe_execute
    void __not_in_flash_func(write_record)(void* parameter)
    {
        const auto* job = static_cast<const WriteJob*>(parameter);

        flash_range_erase(job->offset, FLASH_SECTOR_SIZE);
        flash_range_program(job->offset, job->page, FLASH_PAGE_SIZE);
    }
}

// Loads the most recent valid TunerSettings from flash memory; returns true if successful, otherwise false
bool settings_storage::load(TunerSettings& settings)
{
    settings = {};

    StoredRecord record{};
    unsigned slot = 0;

    return find_latest(record, settings, slot);
}

settings_storage::SaveResult settings_storage::save(const TunerSettings& settings)
{
    uint32_t filter_hz = 0;

    if (!encode_filter(settings.filter_mode, filter_hz) ||
        settings.a4_frequency_hz < ReferencePitchLimits::min_hz ||
        settings.a4_frequency_hz > ReferencePitchLimits::max_hz)
    {
        return SaveResult::Failed;
    }

    StoredRecord latest{};
    TunerSettings previous{};
    unsigned latest_slot = 0;

    const bool have_previous = find_latest(latest, previous, latest_slot);

    if (have_previous && settings_equal(settings, previous))
    {
        return SaveResult::Unchanged;
    }

    // Erase the other sector so the newest valid record remains untouched
    const unsigned target_slot = have_previous ? (latest_slot ^ 1u) : 0u;

    StoredRecord record{};
    record.magic = record_magic;
    record.version = record_version;
    record.sequence = have_previous ? latest.sequence + 1u : 1u;
    record.filter_hz = filter_hz;
    record.a4_frequency_hz = settings.a4_frequency_hz;
    record.checksum = record_checksum(record);

    WriteJob job{};
    job.offset = slot_offset(target_slot);

    // Flash programming requires a complete 256-byte page
    std::memset(job.page, 0xFF, sizeof(job.page));
    std::memcpy(job.page, &record, sizeof(record));

    const int result = flash_safe_execute(write_record, &job, 1000);

    if (result != PICO_OK)
    {
        return SaveResult::Failed;
    }

    // Confirm the new record before reporting a successful save
    StoredRecord verified{};
    TunerSettings decoded{};

    const auto* source = reinterpret_cast<const uint8_t*>(
        XIP_BASE + job.offset);

    std::memcpy(&verified, source, sizeof(verified));

    if (!decode_record(verified, decoded) ||
        verified.sequence != record.sequence ||
        !settings_equal(decoded, settings))
    {
        return SaveResult::Failed;
    }

    return SaveResult::Saved;
}