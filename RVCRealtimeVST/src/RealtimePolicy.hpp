#pragma once

#include "SpscRing.hpp"
#include <chrono>
#include <cstddef>

namespace rvc {

inline constexpr float kDefaultMaxLatencyMs = 300.0f;
inline constexpr float kMinMaxLatencyMs = 100.0f;
inline constexpr float kMaxMaxLatencyMs = 2000.0f;

inline double realtimeSeconds() noexcept
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// Only catch up when audio actually expires. Within budget, preserve continuity.
// Consumer only. Recovery may deliberately change the input block boundary.
inline std::size_t trimStaleAudio(SpscFloatRing& ring, const std::size_t blockFrames,
                                 const double cutoff) noexcept
{
    const auto expired = ring.discardExpired(cutoff);
    if (expired == 0)
        return 0;
    const auto queued = ring.readable();
    return expired + (queued > blockFrames ? ring.discard(queued - blockFrames) : 0);
}

inline bool responseExpired(const double enteredAt, const double now, const double budgetSeconds) noexcept
{
    return enteredAt > 0.0 && now - enteredAt > budgetSeconds;
}

} // namespace rvc
