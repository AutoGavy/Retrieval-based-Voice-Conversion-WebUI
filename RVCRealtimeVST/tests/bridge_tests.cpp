#include "WorkerClient.hpp"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <vector>

using namespace std::chrono_literals;
void check(bool ok, const char* message)
{
    if (!ok) { std::cerr << message << '\n'; std::exit(1); }
}
template<class Predicate> void waitUntil(Predicate predicate, const char* message)
{
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (!predicate()) {
        check(std::chrono::steady_clock::now() < deadline, message);
        std::this_thread::sleep_for(2ms);
    }
}
int main(int argc, char** argv)
{
    check(argc == 3, "expected Python and production worker source path");
    rvc::WorkerClient worker;
    worker.setPath(rvc::kStatePythonPath, argv[1]);
    worker.setPath(rvc::kStateRvcRoot, argv[2]);
    worker.setPath(rvc::kStateModelPath, "CPU-test-only");
    worker.setParameter(rvc::kParamBlockMs, 30);
    worker.setParameter(rvc::kParamGpuPriority, 0); // CPU fixture must not request elevated GPU priority.
    worker.setParameter(rvc::kParamMaxLatencyMs, 100);
    worker.setEnabled(true);
    waitUntil([&] { return worker.isReady(); }, "worker not ready");
    const auto frames = worker.blockFrames();
    std::vector<float> in(frames, 1), out(frames * 4);
    worker.popOutput(out.data(), out.size()); // Initial silence.
    const auto send = [&](float marker) {
        std::fill(in.begin(), in.end(), marker);
        check(worker.pushInput(in.data(), frames) == frames, "input rejected");
    };
    const auto receive = [&]() {
        std::size_t received = 0;
        waitUntil([&] { received = worker.popOutput(out.data(), out.size()); return received != 0; }, "no audio response");
        return received;
    };
    send(1);
    check(receive() == frames && out.front() == 1 && out[frames - 1] == 1, "normal IPC/reset");

    // A stalled request with NO queued input must itself be rejected by age.
    send(-1);
    waitUntil([&] { return worker.droppedBlocks() >= 1; }, "late request was not dropped");
    check(worker.popOutput(out.data(), out.size()) == 0, "stale inference was played");
    send(2);
    check(receive() == frames && out.front() == 2 && out[frames - 1] >= 2, "reset after stalled request");

    // Queue nine seconds of samples during a stall. Their entry time ages out;
    // new audio after recovery must be accepted without restarting the worker.
    const auto dropsBefore = worker.droppedBlocks();
    send(-2);
    std::this_thread::sleep_for(80ms);
    std::vector<float> burst(frames * 300);
    for (std::size_t i = 0; i < burst.size(); ++i) burst[i] = static_cast<float>(100 + i / frames);
    check(worker.pushInput(burst.data(), burst.size()) == burst.size(), "burst rejected");
    waitUntil([&] { return worker.droppedBlocks() >= dropsBefore + 301; }, "expired input backlog not discarded");
    check(worker.popOutput(out.data(), out.size()) == 0, "expired backlog replayed");
    send(400);
    check(receive() == frames && out.front() == 400 && out[frames - 1] >= 3, "new audio/reset after recovery");

    // Pause only the output consumer: input continues at normal cadence.
    for (int i = 0; i < 8; ++i) { send(static_cast<float>(500 + i)); std::this_thread::sleep_for(40ms); }
    const auto freshFrames = receive();
    check(freshFrames <= 2 * frames && out.front() >= 506
          && out[freshFrames - frames] == 507, "expired output retained or latest output lost");
    send(508);
    check(receive() == frames && out.front() == 508, "output remains behind");

    // Changing the limit does not restart the worker. 400ms is now acceptable.
    worker.setParameter(rvc::kParamMaxLatencyMs, 600);
    send(-3);
    check(receive() == frames && out.front() == -3, "configurable budget still acts like old 3-block limit");

    worker.setEnabled(false);
    waitUntil([&] { return worker.status() == rvc::kStatusOff; }, "stop failed");
    worker.setEnabled(true);
    waitUntil([&] { return worker.isReady(); }, "restart failed");
    worker.popOutput(out.data(), out.size());
    send(600);
    check(receive() == frames && out.front() == 600, "restart retained stale buffers");
    std::cout << "real IPC: expired response, 9s input backlog, output backlog, reset and restart passed\n";
}
