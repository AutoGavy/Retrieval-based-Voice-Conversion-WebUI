#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <memory>

namespace rvc {

class SpscFloatRing {
public:
    explicit SpscFloatRing(const std::size_t requestedCapacity, const bool timestamped = false)
    {
        std::size_t capacity = 1;
        while (capacity < requestedCapacity)
            capacity <<= 1;
        capacity_ = capacity;
        mask_ = capacity - 1;
        data_.reset(new float[capacity]);
        std::memset(data_.get(), 0, capacity * sizeof(float));
        if (timestamped)
            times_.reset(new double[capacity] {});
    }

    std::size_t readable() const noexcept
    {
        const auto write = write_.load(std::memory_order_acquire);
        const auto read = read_.load(std::memory_order_acquire);
        return write - read;
    }

    std::size_t writable() const noexcept
    {
        return capacity_ - readable();
    }

    // Metadata is published by the same cursor as the samples. A timestamp of
    // zero is reserved for synthetic silence; real audio uses steady-clock seconds.
    std::size_t push(const float* src, std::size_t count,
                     const double* timestamps = nullptr, double enteredAt = 0.0) noexcept
    {
        const auto write = write_.load(std::memory_order_relaxed);
        const auto read = read_.load(std::memory_order_acquire);
        count = std::min(count, capacity_ - (write - read));
        if (count == 0)
            return 0;

        const std::size_t pos = write & mask_;
        const std::size_t first = std::min(count, capacity_ - pos);
        std::memcpy(data_.get() + pos, src, first * sizeof(float));
        if (count > first)
            std::memcpy(data_.get(), src + first, (count - first) * sizeof(float));
        if (times_) {
            for (std::size_t i = 0; i < count; ++i)
                times_[(write + i) & mask_] = timestamps ? timestamps[i] : enteredAt;
        }
        write_.store(write + count, std::memory_order_release);
        return count;
    }

    std::size_t pushZeros(std::size_t count, const double enteredAt = 0.0) noexcept
    {
        static constexpr float zeros[1024] = {};
        std::size_t total = 0;
        while (total < count) {
            const std::size_t chunk = std::min<std::size_t>(1024, count - total);
            const std::size_t written = push(zeros, chunk, nullptr, enteredAt);
            total += written;
            if (written != chunk)
                break;
        }
        return total;
    }

    std::size_t pop(float* dst, std::size_t count, double* timestamps = nullptr) noexcept
    {
        const auto read = read_.load(std::memory_order_relaxed);
        const auto write = write_.load(std::memory_order_acquire);
        count = std::min(count, write - read);
        if (count == 0)
            return 0;

        const std::size_t pos = read & mask_;
        const std::size_t first = std::min(count, capacity_ - pos);
        std::memcpy(dst, data_.get() + pos, first * sizeof(float));
        if (count > first)
            std::memcpy(dst + first, data_.get(), (count - first) * sizeof(float));
        if (timestamps && times_) {
            for (std::size_t i = 0; i < count; ++i)
                timestamps[i] = times_[(read + i) & mask_];
        }
        read_.store(read + count, std::memory_order_release);
        return count;
    }

    // Consumer only. Times follow the original input through both queues;
    // processing a block never renews its age. Scan is bounded by ring capacity.
    std::size_t discardExpired(const double cutoff) noexcept
    {
        if (!times_)
            return 0;
        const auto read = read_.load(std::memory_order_relaxed);
        const auto write = write_.load(std::memory_order_acquire);
        std::size_t count = 0;
        while (count < write - read) {
            const double timestamp = times_[(read + count) & mask_];
            if (timestamp == 0.0 || timestamp >= cutoff)
                break;
            ++count;
        }
        read_.store(read + count, std::memory_order_release);
        return count;
    }

    // Consumer only, before pop. Zero also denotes an empty queue.
    double oldestTimestamp() const noexcept
    {
        const auto read = read_.load(std::memory_order_relaxed);
        const auto write = write_.load(std::memory_order_acquire);
        return times_ && write != read ? times_[read & mask_] : 0.0;
    }

    // Producer may publish this boundary to the consumer for a safe flush.
    std::size_t writePosition() const noexcept
    {
        return write_.load(std::memory_order_acquire);
    }

    // Consumer only. Never rewind the producer or race a producer-side reset.
    std::size_t discard(std::size_t count) noexcept
    {
        const auto read = read_.load(std::memory_order_relaxed);
        const auto write = write_.load(std::memory_order_acquire);
        count = std::min(count, write - read);
        read_.store(read + count, std::memory_order_release);
        return count;
    }

    // Consumer only. A boundary already passed by this consumer is a no-op.
    // Unsigned subtraction also handles index wrap, provided capacity < SIZE_MAX/2.
    std::size_t discardBefore(const std::size_t position) noexcept
    {
        const auto read = read_.load(std::memory_order_relaxed);
        const auto write = write_.load(std::memory_order_acquire);
        const auto distance = position - read;
        if (distance > write - read)
            return 0;
        read_.store(position, std::memory_order_release);
        return distance;
    }

    // Call only when both producer and consumer have stopped.
    void resetUnsafe() noexcept
    {
        read_.store(0, std::memory_order_relaxed);
        write_.store(0, std::memory_order_relaxed);
    }

private:
    std::unique_ptr<float[]> data_;
    std::unique_ptr<double[]> times_;
    std::size_t capacity_ = 0;
    std::size_t mask_ = 0;
    alignas(64) std::atomic<std::size_t> write_ {0};
    alignas(64) std::atomic<std::size_t> read_ {0};
};

} // namespace rvc
