#include "RealtimePolicy.hpp"
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <vector>

void check(bool ok, const char* message)
{
    if (!ok) { std::cerr << message << '\n'; std::exit(1); }
}

int main()
{
    rvc::SpscFloatRing ring(16);
    float input[16], output[16] {};
    for (int i = 0; i < 16; ++i) input[i] = static_cast<float>(i);
    check(ring.discard(10) == 0, "empty discard");
    ring.push(input, 12);
    check(ring.discard(10) == 10, "partial discard");
    ring.push(input, 10); // Wrap the write cursor.
    check(ring.pop(output, 12) == 12 && output[0] == 10 && output[1] == 11
          && output[2] == 0 && output[11] == 9, "wrap contents");
    ring.push(input, 8);
    const auto boundary = ring.writePosition();
    ring.push(input + 8, 8);
    check(ring.discardBefore(boundary) == 8, "flush boundary");
    check(ring.pop(output, 4) == 4 && output[0] == 8, "fresh data survives flush");
    check(ring.discardBefore(boundary) == 0, "old boundary cannot rewind consumer");
    check(ring.discard(999) == 4, "discard clamps to available");

    // Nine seconds of timestamped 30ms blocks: only the latest remains.
    rvc::SpscFloatRing backlog(1u << 20, true);
    std::vector<float> burst(1440 * 300);
    std::vector<double> times(burst.size());
    for (std::size_t i = 0; i < burst.size(); ++i) {
        burst[i] = static_cast<float>(i / 1440);
        times[i] = 1.0 + static_cast<double>(i) / 48000.0;
    }
    backlog.push(burst.data(), burst.size(), times.data());
    check(rvc::trimStaleAudio(backlog, 1440, 9.7) == 1440 * 299, "nine-second catch-up");
    std::vector<float> block(1440);
    backlog.pop(block.data(), block.size());
    check(block.front() == 299 && block.back() == 299, "latest block selected");
    backlog.push(burst.data(), 3 * 1440, nullptr, 20.0);
    backlog.push(burst.data(), 257, nullptr, 20.03); // Arbitrary host callback.
    check(rvc::trimStaleAudio(backlog, 1440, 19.9) == 0, "more than three blocks retained within budget");
    check(rvc::trimStaleAudio(backlog, 1440, 20.01) == 3 * 1440, "expired prefix discarded, fresh partial retained");
    check(backlog.readable() == 257, "partial callback survives");
    check(rvc::responseExpired(10.0, 10.4, 0.3), "late response rejected");
    check(!rvc::responseExpired(10.0, 10.4, 0.5), "larger budget accepts same inference");
    check(!rvc::responseExpired(10.0, 10.25, 0.25), "exact deadline retained");

    // Queue + inference + output use ONE clock: completion must not refresh age.
    rvc::SpscFloatRing timedInput(16, true), timedOutput(16, true);
    double copiedTimes[16] {};
    timedInput.push(input, 12, nullptr, 30.0);
    check(timedInput.discardExpired(30.04 - 0.3) == 0, "input queue within budget");
    timedInput.pop(output, 12, copiedTimes);
    check(!rvc::responseExpired(copiedTimes[0], 30.22, 0.3), "inference within cumulative budget");
    timedOutput.push(output, 12, copiedTimes);
    check(timedOutput.discardExpired(30.32 - 0.3) == 12, "output expires at cumulative 320ms, not per-stage 300ms");
    timedInput.push(input, 12, nullptr, 31.0); // Wrap metadata along with audio.
    timedInput.pop(output, 12, copiedTimes);
    check(copiedTimes[0] == 31.0 && copiedTimes[11] == 31.0, "timestamp wrap and alignment");
    timedOutput.push(output, 12, copiedTimes);
    check(timedOutput.discardExpired(31.2 - 0.5) == 0, "500ms budget retains audio");
    check(timedOutput.discardExpired(31.2 - 0.1) == 12, "live budget reduction expires existing output");

    // Producer continues writing while the sole consumer alternates pop/discard.
    rvc::SpscFloatRing concurrent(1024);
    std::atomic<bool> done {false};
    std::thread producer([&] {
        for (int i = 1; i <= 300000; ++i) {
            const float sample = static_cast<float>(i);
            while (!concurrent.push(&sample, 1)) std::this_thread::yield();
        }
        done.store(true, std::memory_order_release);
    });
    float previous = 0;
    while (!done.load(std::memory_order_acquire) || concurrent.readable()) {
        if (concurrent.readable() > 512) concurrent.discard(128);
        float value = 0;
        if (concurrent.pop(&value, 1)) {
            check(value > previous, "concurrent samples must stay strictly ordered");
            previous = value;
        }
    }
    producer.join();
    std::cout << "ring discard, wrap, 9s catch-up, deadlines and SPSC stress passed\n";
}
