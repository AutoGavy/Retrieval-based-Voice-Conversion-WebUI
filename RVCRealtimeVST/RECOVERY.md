# 0.1.4: parameter auto-save, GPU priority and overload recovery

The real-time path preserves continuity within a user-selected age budget, then
prefers a gap to replaying expired speech. Python/PyTorch remains the inference
backend. Model/index settings, CPU process priority, other Python processes and
game settings are unchanged. Release x64 VST3 compilation has succeeded. Live
host behavior and GPU contention still need user validation; the development
workflow does not replace the installed plugin or run the user's audio chain.

## Parameter auto-save (0.1.4)

Edits made in the plugin UI are saved to the `[Parameters]` section of
`%LOCALAPPDATA%\RVCRealtime\settings.ini` about 200 ms after the last edit.
Closing the editor or unloading the instance flushes pending edits. Saving runs
on UI/idle callbacks, never in audio processing or the DSP parameter callback.
The existing `[Paths]` section is preserved. An unsuccessful write is retried
and reported in the UI.

All thirteen tuning controls are remembered, including Block, Crossfade,
Context, GPU priority and max latency. ENGINE remains in host state only; fresh
instances start with ENGINE off. Only edited keys are written, avoiding writes
of other instances' untouched values. Invalid or out-of-range saved numbers are
ignored. Decimal values use locale-independent round-trip formatting.

New instances load these defaults. A host project/preset subsequently loaded by
Cantabile still restores its own settings; save the host project to retain those
edits in that project. Automation, project recall and startup do not overwrite
the auto-saved defaults. Versions before 0.1.4 did not save these tuning values
to this file, so adjust them once after upgrading. The red ON styling from
0.1.3 is retained.

## Controls and compatibility

- **GPU PRIORITY**, default on: after the worker initializes CUDA, request the
  Windows GPU scheduling class `HIGH` for that worker's process handle only,
  using `D3DKMTSetProcessSchedulingPriorityClass`. Read the class back with
  `D3DKMTGetProcessSchedulingPriorityClass` and display accepted, unavailable,
  failed (NTSTATUS), or not confirmed. Acceptance is **not** a measured guarantee
  of priority over games, reserved compute/VRAM, or an audio deadline.
- Turning the switch off restores that worker's original class. Calls run on
  the bridge thread, not the audio callback; they neither elevate privileges nor
  use `REALTIME`, global executable-name rules, or a CUDA stream priority claim.
  Failure does not disable inference. Toggle off/on to retry a failed request.
- **MAX ACCEPTABLE LATENCY**, default **300 ms**, range **100–2000 ms**, takes
  effect without restarting the model. It is the age budget for the wet audio
  path **inside this plugin**, not a target delay and not a mic-to-OBS guarantee.
  Small budgets relative to BLOCK show a warning; the plugin does not silently
  raise the chosen limit. Sustained overload can still cause dropouts/silence.
- The UI shows inference duration and the oldest returned wet sample's `age`
  separately; `age` is zero when no wet audio is returned, not proof of zero
  system latency. Ages are sampled at the output callback.
- Host parameter IDs 0–11 are unchanged; GPU priority and max latency are
  appended at 12 and 13. Saved sessions store both controls. Legacy state with
  twelve parameters is read with the two new defaults appended.

## Policy

- Every input callback stamps its samples with a monotonic entry time. Metadata
  travels with the samples through input queue, inference and output queue;
  completion never renews that timestamp. A 300 ms setting is **one cumulative
  allowance**, not 300 ms for each stage. No audio-thread allocation or locks
  are introduced; timestamp storage is preallocated (16 MiB total per instance).
- Once input samples expire, the consumer discards the expired prefix and, if
  necessary, skips ahead to the newest complete block of remaining fresh audio.
  A partial fresh block is retained until enough new samples arrive. Within
  budget, even queues exceeding three blocks are retained.
- A response is discarded if the original input block's oldest entry time has
  exceeded the selected budget. At output, expired samples are discarded again,
  including when the user lowers the budget while audio is queued. This check
  also works when the host pauses input or output; age is not inferred solely
  from queue length. The former hard-coded three-block deadline is removed.
- Timestamps track input-block lineage, not phoneme-level alignment through the
  model/SOLA. Sound-card/host buffering before entry or after callback return,
  Broadcast, OBS, and model alignment are not measured. A callback already in
  flight cannot be recalled. This is an expiry policy, not a hard real-time SLA.
- Input overflow invalidates the retained input (recent samples were rejected).
  It is discarded so the next newly captured samples restart the stream.
- Input discontinuity / discarded response invalidates pending output and asks
  the next Python request to reset its history. Output invalidation publishes a
  monotonic write boundary; only the audio consumer changes the read cursor, and
  fresh samples written after the boundary survive. No active ring is reset from
  both threads. One callback already in progress cannot be recalled.
- Reset zeroes input, resampled input, RMS, SOLA, and pitch caches **in place**,
  preserving buffer identity, then invalidates cached parameter values.
- Output-only trimming does not reset inference history: the input timeline has
  remained continuous in that case.
- `drop` counts discarded block-equivalents, rounded up per discard operation.
  Input/output losses may both be counted; this is not a unique source-block
  counter or a latency measurement.
- The five-second IPC watchdog remains for an unresponsive worker. It is not a
  playback allowance. Until a blocked request finishes, the host can emit silence;
  no second request reuses the shared memory while the first is still in flight.
- Sequence mismatch is an error, not permission to reuse an in-flight request.

IPC version is **2**. Request offset **68**, uint32, bit 0 = reset stream before
processing. C++ overwrites it on every request. Always distribute the new binary
and `worker/rvc_worker.py` together; version 1 and version 2 are incompatible.

## CPU-only recovery tests (no audio devices, model, or GPU)

```powershell
cmake -S RVCRealtimeVST/tests -B RVCRealtimeVST/build-recovery-tests -G "Visual Studio 18 2026" -A x64
cmake --build RVCRealtimeVST/build-recovery-tests --config Release
ctest --test-dir RVCRealtimeVST/build-recovery-tests -C Release --output-on-failure
```

Regression test sources cover ring/timestamp wrap, partial discard, cumulative
age through both queues, live budget changes, retention beyond three blocks,
concurrent producer/consumer, flush boundaries, nine seconds of queued samples,
late inference with no backlog, Windows shared-memory/event IPC, reset ordering,
buffer identity and stop/start. The bridge fixture disables the GPU priority
request. These updated tests have **not been run**. The fake worker exists only
inside the separate test build. Never copy it into a plugin package.

The injected nine-second backlog is a deterministic simulation, not a nine-second
GPU stress test. Manual validation should include old/new session recall, UI
layout, priority toggle/readback (including access denied), live latency-limit
changes, a brief GPU spike followed by recovery without host restart, sustained
GPU contention, audio quality after reset and actual mic-to-CABLE latency.

## Deployment

Keep the existing installation until the new build has been checked. Exit
Cantabile and its RVC worker before deployment, back up the complete existing
VST3 bundle, and replace the binary and bundled worker as one unit. Preserve the
backup for rollback. No installation or audio-device changes are done by the
recovery tests.
