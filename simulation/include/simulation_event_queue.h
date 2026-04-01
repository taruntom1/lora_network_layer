#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <queue>
#include <vector>

/**
 * @file simulation_event_queue.h
 * @ingroup sim_runtime
 * @brief Deterministic time-ordered event queue for host simulation runtime.
 */

/**
 * @ingroup sim_runtime
 * @brief Event categories reserved for simulation runtime scheduling.
 */
enum class SimEventType : uint8_t {
    TxStart = 0,
    TxEnd = 1,
    RetryBackoff = 2,
    RxDelivery = 3,
};

/**
 * @ingroup sim_runtime
 * @brief Generic deterministic event record stored in @ref SimulationEventQueue.
 */
struct SimEvent {
    /** Event timestamp in virtual microseconds. */
    uint64_t time_us{0};
    /** Secondary ordering key at equal timestamp; lower value executes first. */
    uint8_t priority{0};
    /** Monotonic tie-breaker to keep ordering stable across runs. */
    uint64_t sequence_id{0};
    /** Event kind. */
    SimEventType type{SimEventType::RxDelivery};

    /** Optional source node context. */
    uint16_t src_node_id{0};
    /** Optional destination node context. */
    uint16_t dst_id{0};
    /** Transmission identity linking start/end/delivery events. */
    uint64_t tx_id{0};
    /** Retry/defer attempt count for MAC scheduling flows. */
    uint8_t retry_count{0};
    /** Payload bytes for queued delivery-like events. */
    std::vector<uint8_t> payload;
};

/**
 * @ingroup sim_runtime
 * @brief Thread-safe deterministic min-heap event queue.
 */
class SimulationEventQueue {
public:
    /** @brief Insert an event into the queue. */
    void push(const SimEvent& event);
    /** @brief Insert an event into the queue (move overload). */
    void push(SimEvent&& event);

    /** @brief Pop next event with time <= @p horizon_us, returns false if none are due. */
    bool popDue(uint64_t horizon_us, SimEvent& out_event);

    /** @brief Number of pending events in the queue. */
    size_t size() const;
    /** @brief Remove all events from the queue. */
    void clear();

private:
    struct EventCompare {
        bool operator()(const SimEvent& lhs, const SimEvent& rhs) const;
    };

    mutable std::mutex mutex_;
    std::priority_queue<SimEvent, std::vector<SimEvent>, EventCompare> queue_;
};
