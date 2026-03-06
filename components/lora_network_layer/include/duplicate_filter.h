#pragma once

#include <cstdint>
#include <cstddef>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/**
 * Fixed-size LRU duplicate cache.
 *
 * Keyed on message_id (uint32_t).  Oldest entry is evicted on overflow.
 * All public methods are mutex-protected.
 */
class DuplicateFilter {
public:
    explicit DuplicateFilter(size_t capacity);
    ~DuplicateFilter();

    DuplicateFilter(const DuplicateFilter&) = delete;
    DuplicateFilter& operator=(const DuplicateFilter&) = delete;

    /**
     * Return true if @p id has been seen before.
     * If not seen, the id is inserted into the cache (marks it as seen).
     */
    bool isDuplicate(uint32_t id);

    /** Insert @p id without checking (used when originating a message). */
    void markSeen(uint32_t id);

    /** Number of entries currently stored. */
    size_t size() const;

private:
    struct Entry {
        uint32_t id;
        uint32_t timestamp;  // insertion tick for LRU ordering
        bool     used;
    };

    Entry*          entries_;
    size_t          capacity_;
    uint32_t        tick_;          // monotonic counter for LRU
    SemaphoreHandle_t mutex_;

    /** Find the index of the oldest (smallest tick) used entry. */
    size_t findOldest() const;
};
