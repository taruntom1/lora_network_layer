#include "duplicate_filter.h"
#include <cstring>

DuplicateFilter::DuplicateFilter(size_t capacity)
    : capacity_(capacity)
    , tick_(0)
{
    entries_ = new Entry[capacity_];
    std::memset(entries_, 0, sizeof(Entry) * capacity_);
    mutex_ = xSemaphoreCreateMutex();
    configASSERT(mutex_);
}

DuplicateFilter::~DuplicateFilter()
{
    vSemaphoreDelete(mutex_);
    delete[] entries_;
}

bool DuplicateFilter::isDuplicate(uint32_t id)
{
    xSemaphoreTake(mutex_, portMAX_DELAY);

    // Search for an existing entry with this id.
    for (size_t i = 0; i < capacity_; ++i) {
        if (entries_[i].used && entries_[i].id == id) {
            // Refresh LRU tick
            entries_[i].timestamp = ++tick_;
            xSemaphoreGive(mutex_);
            return true;
        }
    }

    // Not found — insert it.
    // Find an empty slot first.
    for (size_t i = 0; i < capacity_; ++i) {
        if (!entries_[i].used) {
            entries_[i] = {id, ++tick_, true};
            xSemaphoreGive(mutex_);
            return false;
        }
    }

    // No empty slot — evict the oldest entry.
    size_t oldest = findOldest();
    entries_[oldest] = {id, ++tick_, true};
    xSemaphoreGive(mutex_);
    return false;
}

void DuplicateFilter::markSeen(uint32_t id)
{
    xSemaphoreTake(mutex_, portMAX_DELAY);

    for (size_t i = 0; i < capacity_; ++i) {
        if (entries_[i].used && entries_[i].id == id) {
            entries_[i].timestamp = ++tick_;
            xSemaphoreGive(mutex_);
            return;
        }
    }

    // Not present — insert.
    for (size_t i = 0; i < capacity_; ++i) {
        if (!entries_[i].used) {
            entries_[i] = {id, ++tick_, true};
            xSemaphoreGive(mutex_);
            return;
        }
    }

    size_t oldest = findOldest();
    entries_[oldest] = {id, ++tick_, true};
    xSemaphoreGive(mutex_);
}

size_t DuplicateFilter::size() const
{
    xSemaphoreTake(mutex_, portMAX_DELAY);
    size_t count = 0;
    for (size_t i = 0; i < capacity_; ++i) {
        if (entries_[i].used) ++count;
    }
    xSemaphoreGive(mutex_);
    return count;
}

size_t DuplicateFilter::findOldest() const
{
    size_t oldest_idx = 0;
    uint32_t oldest_tick = UINT32_MAX;
    for (size_t i = 0; i < capacity_; ++i) {
        if (entries_[i].used && entries_[i].timestamp < oldest_tick) {
            oldest_tick = entries_[i].timestamp;
            oldest_idx = i;
        }
    }
    return oldest_idx;
}
