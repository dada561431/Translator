# Audio Input Foundation - Phase 8A

## Scope and Evidence

Baseline: `e5f5259166859f613cee78a9fdd1bf095c056ba9`. History includes the Phase
7B.2 implementation `5461574ac6e196a96895f868c63e50697452db95`, Phase 6 and 6.1A.
Original reference `D:/workspace/Project/Usst/LunaTranslator` is read-only.

Only independent audio capture, conversion, lifecycle, tests and QA tools are added.
No ASR, Whisper, VAD, noise suppression, echo cancellation, OCR/audio coupling,
translation, subtitle display, audio Settings UI, model downloads or packaging.
The Translator executable does not link TranslatorAudio or construct an audio input.
No startup microphone/loopback acquisition, persistent settings or recordings.

Implementation commit: `3656ea0b0cda6023ca40651e790f5f03b21a3caa`.

**Phase 8A Core Acceptance: PASS.** The owner subsequently completed a real Windows
microphone speaking/quiet test with a clear level difference, recorded below.
This is owner-reported manual evidence, not a new agent-run capture or ASR claim.
The closeout changes only this document and README; production code is unchanged.
Existing Release/Debug PASS and CTest 19/19 PASS in each configuration are reused
without rerunning builds/tests. Physical microphone unplug, real default-output
switching and Windows microphone privacy denial remain non-blocking PENDING / NOT TESTED.

| Acceptance | Status | Evidence boundary |
| --- | --- | --- |
| Independent module, converter and coordinator | PASS | Release/Debug and device-independent tests |
| Microphone API capture / PCM / repeated Start-Stop | PASS | Actual Windows QAudioSource, 20 cycles |
| Microphone voice versus quiet | PASS (owner-reported) | Subsequent real Realtek microphone speaking/quiet test has a clear peak difference; full data below |
| Microphone Capture (full real voice acceptance) | PASS (owner-reported) | Real voice enters microphone path; independent of loopback evidence |
| Windows System Loopback Capture | PASS | Actual render endpoint, explicit silence/tone/silence fixture; no microphone backend in loopback Probe |
| Physical microphone unplug | PENDING / NOT TESTED | Fake errors/state recovery pass; no real unplug reported; non-blocking |
| Real default-output switching | PENDING / NOT TESTED | No real output switch reported; non-blocking |
| Windows microphone privacy-denial scenario | PENDING / NOT TESTED | No real permission-denial test reported; non-blocking |
| Five-minute stability | PASS | Each real backend completed its own 300-second run |
| Phase 7 regressions | PASS (automated) | All existing suites retained; no new full desktop manual Phase 7 claim |

## Architecture and Ownership

```text
AudioInputCoordinator (owner QObject thread, initially Stopped)
  factory -> IAudioInput
              + MicrophoneAudioInput -> QAudioSource/QIODevice
              + WindowsLoopbackAudioInput -> WASAPI worker
                            |
                       PcmConverter
                            |
              AudioPcmChunk: 640-byte fixed PCM
                            |
      bounded mailbox -> coordinator timer -> Probe/future consumer
```

`TranslatorAudio` is a separate static C++ library with dynamically linked Qt Core
and Multimedia. Qt Multimedia's own transitive Gui/Network dependencies are not
application networking or translation integration. Only the audio tests and Probe
link this library in Phase 8A. No audio source is added to main.cpp, TranslationWindow,
SettingsManager, RealtimePipelineCoordinator or existing controllers.

`IAudioInput` describes devices, Start/Stop, running, actual selected device, native
format and dropped chunks; signals are started, stopped, errorOccurred and pcmReady.
`InputKind` distinguishes Microphone capture endpoints from SystemLoopback render
endpoints. Empty device ID resolves default at Start; explicit IDs remain pinned.
The owner QObject thread invokes all API methods and delivers all public signals.
Factories are injectable; enumeration never starts capture and display names are
not device identities. No audio preference persistence was needed in this phase.

Coordinator states: Stopped -> Starting -> Running -> Stopping -> Stopped; failure
cleans the backend and enters Error. Start from Stopped/Error creates a fresh backend;
Start in Starting/Running/Stopping returns false without switching/reopening.
Changing kind/device requires explicit Stop. Stop is synchronous and idempotent.
An unexpected stopped signal safely restores Stopped. Error/started/stopped delivery
is queued and tagged with a generation to reject callbacks from previous sessions.
PCM delivery occurs outside the backend's read/conversion stack, so a consumer can
call Stop safely while receiving a chunk. Destructor and Probe aboutToQuit stop capture.

## Native Capture

### Microphone

Uses QMediaDevices::audioInputs/defaultAudioInput, QAudioDevice stable byte ID,
description/isDefault/preferredFormat/isFormatSupported and QAudioSource::start().
Native preferred format is explicitly described and converted; it is not assumed
to be 16 kHz mono. This is the Qt source format, not a claim to inspect physical
ADC hardware. A 100 ms Qt source buffer is read via readyRead in at-most-32768-byte
blocks on the owner thread. Qt owns its underlying capture thread/ring buffer;
no extra microphone std::thread or hand-written WASAPI microphone is introduced.
The [Qt QAudioSource reference](https://doc.qt.io/qt-6/qaudiosource.html) describes
the QIODevice interface; APIs were also checked against the installed Qt 6.11.2 headers.

audioInputsChanged checks selected-ID existence and, for default-following sessions,
default-ID change. Disappearance/default replacement stops and emits DeviceUnavailable;
there is no automatic migration or retry loop. QAudioSource stopped/error is also
handled. QMicrophonePermission denial is separately reported when Qt can identify
it. Windows Qt may report an open failure without distinguishing privacy denial;
OpenFailed explicitly advises checking device access and privacy permissions instead
of falsely asserting PermissionDenied. No runtime permission prompt is silently issued.

### Windows System Output Loopback

IMMDeviceEnumerator enumerates active eRender endpoints with stable WASAPI IDs and
friendly names; default uses eConsole. Worker resolves the requested endpoint,
activates IAudioClient, obtains GetMixFormat, initializes shared mode with
AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK, sets an audio event
and gets IAudioCaptureClient. GetNextPacketSize/GetBuffer/ReleaseBuffer drains actual
render mix packets. This is not microphone acoustical pickup; loopback never checks
microphone permission or opens QAudioSource.
See Microsoft's [loopback recording reference](https://learn.microsoft.com/en-us/windows/win32/coreaudio/loopback-recording)
and [GetBuffer flags/packet contract](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudiocaptureclient-getbuffer).

An independent std::thread initializes COM MTA and releases all COM interfaces,
CoTaskMemFree mix memory and audio event on the same thread before CoUninitialize.
The client stops before interface destruction. A separate stop event wakes
WaitForMultipleObjects and Stop joins before closing it. No detached worker or
external process. No busy polling: audio readiness is event-driven; a 250 ms timeout
is a device-lifecycle watchdog, not a 1 ms capture poll. GetState and default render
ID are checked at most every 250 ms even with ongoing events. Invalidation/HRESULT
failures safely terminate and reach the coordinator. Explicit endpoints remain pinned
when only the default changes. Windows 11 is the verified target; older Windows
event-loopback compatibility is not claimed. Non-Windows reports unsupported.

The device APIs themselves are synchronous Windows/driver calls: Stop can wake the
event wait immediately, but cannot forcibly interrupt a stalled COM activation or
broken driver call. No unsafe thread termination is used. Measured teardown results
below apply to this device/driver, not a universal hard deadline.

AUDCLNT_BUFFERFLAGS_SILENT produces zero PCM; DATA_DISCONTINUITY marks the next
chunk. A truly idle output endpoint may supply no packets; no-packet intervals are
not errors and no VAD suppresses received silent packets. This foundation does not
synthesize a wall-clock silence stream for absent packets. Future ASR must account
for idle/discontinuous input rather than assume exactly 50 callbacks every second.

## Unified PCM and Conversion

Output: signed int16 little-endian, mono, 16000 Hz. 20 ms = 320 samples = 640 bytes.
Native formats supported: uint8, int16, packed int24, int32 and float32, 8-192 kHz,
1-8 channels. WASAPI ordinary/extensible PCM and float are validated against their
container size and block alignment; unsupported encodings return an explicit error.
Signed 24-in-32 extensible PCM uses its left-aligned 32-bit container scaling.

All channels are averaged in double precision (stereo is (L+R)/2; 3-8 channels use
an explicitly unweighted average, not surround-speaker weighting). Float NaN/Inf
becomes zero, mixing occurs before clamping to [-1,1], then output is quantized.
Positive/negative full scale maps to 32767/-32768; 16 kHz mono int16 is bit-exact.
No native float or stereo buffer is reinterpreted as output int16 mono.

PcmConverter retains the preceding mono frame and integer rational input positions
for streaming linear interpolation. Phase survives arbitrary read/chunk boundaries;
48 kHz and 44.1 kHz to 16 kHz sample counts do not accumulate drift. Input retains
at most one incomplete native frame; output retains at most 639 bytes. Each full
chunk is handed off immediately, not accumulated as an unbounded converted vector.
Stop intentionally discards the final incomplete 20 ms chunk; it never emits stale
partial audio into the next session. At upsampling end-of-stream, interpolation
may await the next native frame; tests permit <=2 sample boundary rounding before
fixed chunking. No third-party resampler, FFmpeg DSP or model runtime is introduced.

This basic linear converter is not band-limited and has no anti-alias low-pass;
ASR accuracy/high-frequency aliasing is a documented Phase 8B evaluation concern,
not a claim of studio-quality or proven recognition accuracy.

PcmChunk holds samples, session generation, per-session sequence, steady-clock
origin plus sequence*20000 microseconds, and discontinuity. It is a monotonic
sample timeline, not an ADC/QPC hardware timestamp or wall-clock measurement.
An idle endpoint or native discontinuity can break its relationship to wall time;
consumers should combine session/sequence/discontinuity with arrival timing for
future latency/stale protection. Stop invalidates the old session and clears queues.

## Backpressure, Errors and Privacy

WASAPI worker mailbox and coordinator mailbox each retain at most 50 chunks (one
second / 32000 sample bytes, plus metadata). Worker holds the queue mutex only to
copy/enqueue; it never calls consumer code or queued-per-chunk UI invocations.
Owner timers drain at most 10 chunks per tick. Overflow drops oldest, counts the
drop and marks the retained gap; sequence is never renumbered. Consumers must do
minimal work or use their own bounded mailbox, not an unlimited Qt queued connection
to a slow future ASR worker. Qt's microphone ring buffer remains bounded; owner-thread
stalls may cause native capture gaps. No lossless unlimited buffering is promised.

ErrorCode: None, NoDevice, PermissionDenied, DeviceUnavailable, OpenFailed,
UnsupportedFormat, BackendFailure, Overflow. User-readable message is separate
from diagnostic API/HRESULT hex/endpoint detail. No crash, automatic retry loop,
ASR, network, disk audio write or UI operation occurs in the capture worker.
Independent logging category translator.audio logs lifecycle/device/format/errors,
not each chunk. Probe prints metadata, counts, timing, peak/RMS only. No PCM,
recorded content or automatic transcript enters logs. Probe does not implement WAV
or PCM file saving; *.wav/*.pcm and .cache/ are ignored for future QA safety.

## Builds and Automated Tests

No hard-coded machine Qt path is added to CMakeLists.txt. New dependency:
Qt6::Multimedia on TranslatorAudio only; Windows boundary links ole32/uuid.
MinGW KS GUID storage is instantiated from SDK constants rather than requiring
MSVC-only libraries. Existing 16 suites remain; three new suites make **19 suites**.

```powershell
$env:PATH = 'D:/QT/Tools/mingw1310_64/bin;D:/QT/6.11.2/mingw_64/bin;D:/QT/Tools/Ninja;' + $env:PATH
& D:/QT/Tools/CMake_64/bin/cmake.exe -S . -B build/phase61b -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=D:/QT/6.11.2/mingw_64 -DCMAKE_CXX_COMPILER=D:/QT/Tools/mingw1310_64/bin/g++.exe
& D:/QT/Tools/CMake_64/bin/cmake.exe --build build/phase61b -j 4
& D:/QT/Tools/CMake_64/bin/ctest.exe --test-dir build/phase61b --output-on-failure
# Repeat configure/build/ctest for build/phase61b-debug with CMAKE_BUILD_TYPE=Debug.
```

AudioConverterTest covers int16 passthrough, stereo/float/multichannel downmix,
uint8/int24/int32, full scale/clipping/nonfinite input, silence/SILENT equivalence,
48/44.1 kHz conversion, arbitrarily split frames, phase continuity, one-minute
no-drift sample counts, bounded residuals and slow-consumer overflow/gap markers.
AudioCoordinatorTest injects device/backend factory: 0/1/multiple/duplicate-name
devices, both kinds, defaults/removal/errors, 20 repeated cycles per kind, duplicate
Start/Stop, explicit source-change rule, old queued callback rejection, clean
destruction and Stop from PCM consumer. CTest never opens a real microphone/output.
Fake disconnect/default change evidence is not physical unplug acceptance.
AudioBackendTest exercises the real Windows adapter's invalid-endpoint failure,
ensures fatal Error is not preceded by ordinary Stopped, preserves coordinator
diagnostics and repeats Stop during startup 20 times. The invalid ID never opens
hardware; a microphone/render device or playback is not required by this suite.
Existing implementation Release and Debug build/CTest results: **PASS, 19/19 suites in each**.
These results are not rerun for this documentation-only manual acceptance closeout.
All previous 16 suites remain and pass. The initial MinGW GUID linker errors and
ambiguous Probe CLI initializer were fixed before final builds. The real invalid-ID
negative probe exposed a fatal stopped-before-error ordering bug; teardown now
suppresses ordinary stopped during fatal handling and preserves error delivery.
The native failure suite guards this regression without requiring audio hardware.

## Real Windows Probe and Manual Procedure

```powershell
& ./build/phase61b/TranslatorAudioCaptureProbe.exe --list
& ./build/phase61b/TranslatorAudioCaptureProbe.exe --kind microphone --seconds 20
& ./build/phase61b/TranslatorAudioCaptureProbe.exe --kind microphone --seconds 1 --cycles 20
& ./build/phase61b/TranslatorAudioCaptureProbe.exe --kind microphone --seconds 300
& ./build/phase61b/TranslatorAudioCaptureProbe.exe --kind loopback --seconds 8 --test-tone
& ./build/phase61b/TranslatorAudioCaptureProbe.exe --kind loopback --seconds 1 --cycles 20 --test-tone
& ./build/phase61b/TranslatorAudioCaptureProbe.exe --kind loopback --seconds 300 --test-tone
& ./build/phase61b/TranslatorAudioCaptureProbe.exe --kind loopback --seconds 2 --exit-while-running
```

Use --device with an enumerated stable ID to choose a specific source: hex Qt ID
for microphone, UTF-8 WASAPI endpoint string for loopback. No --kind or --list
only enumerates and exits. --test-tone is explicit loopback-only QA playback:
default output alternates 2 seconds silence / 3 seconds 440 Hz tone / 2 seconds
silence. It creates no file and is not a production recording/playback feature.
When testing a nondefault endpoint, use a player routed to that endpoint instead.

Microphone manual: choose the microphone actually used for speaking, Start, speak
and remain quiet in separate intervals, observe nonzero PCM and a clear level
change, Stop and restart, then repeat 20 cycles. Physically unplug/disallow the
device separately, observe clear error and restart after restoration. API open
success/noise alone does not complete this check.

Loopback manual: select render endpoint, keep microphone backend off, play/pause
a browser/local player or explicit QA tone, check level rise/fall and zero PCM.
Repeat with headphones and 20 cycles; switch default output during a default-bound
capture and verify safe stop/error. No microphone permissions should be required.
Exit during capture, then verify no Probe/audio process remains. Real physical
default switch/unplug is pending until actually performed; no settings are changed
programmatically to manufacture a PASS.

### Observed Device Evidence

Actual Windows enumeration found two capture endpoints and three active render
endpoints. Default microphone: External microphone / Realtek Audio, endpoint
`{0.0.1.00000000}.{d0a7929c-db5e-4cd1-91a9-94c4ef6e78ac}` (Qt byte-ID hex in Probe).
Other microphone: Realtek microphone, `{155802de-b3ec-4385-b097-1a01c518ac12}`.
Default output: Headphones / Realtek Audio,
`{0.0.0.00000000}.{5d620256-31cf-42ba-bc2b-eaa85ad7d836}`.
Other render endpoints: Realtek Speakers and G24H1 / NVIDIA High Definition Audio.
Both actual input formats observed: **48000 Hz / stereo / float32 LE**.
Unified output: **16000 Hz / mono / int16 LE / 640 bytes per chunk**.

Initial 12-second mic check: 599 chunks, 191680 samples, 383360 bytes, 184230 nonzero,
7450 zero, peak 0.00604248, RMS 0.00122200, start 124 ms, Stop 17.661 ms; valid chunks.
Historical agent-run default external-mic checks remained near noise level despite
the owner's two earlier speaking/quiet attempts. Those checks alone did not establish
voice/quiet acceptance and are not retroactively relabeled. The later independent
owner-reported Realtek microphone test below supplies the missing manual evidence.

### Owner-Reported Microphone Speaking / Quiet Acceptance

The owner completed this test in a real Windows environment using **Realtek(R)
Audio microphone**, speaking first, then remaining quiet. No specific endpoint ID
is supplied for this later test; it is not assumed to be the earlier default external mic.

Native format: **48000 Hz / 2 channels / float32 LE**.
Unified PCM: **16000 Hz / mono / int16 LE / 20 ms / 640 bytes**.
Reported Probe level peaks, in order:

```text
0.0050354
0.0154114
0.0012207
0.0000305
```

| Reported summary field | Value |
| --- | --- |
| peak | 0.015411376953125 |
| rms | 0.00036135544829396954 |
| chunks | 249 |
| samples | 79680 |
| dropped_chunks | 0 |
| invalid_chunk | false |
| stopped | true |

The owner confirms that the speaking-to-quiet peak difference is clear enough to
establish real voice input through the microphone capture path. **Microphone voice
versus quiet: PASS. Microphone Capture: PASS.** This is capture acceptance, not a
claim of speech recognition, intelligibility measurement or ASR accuracy.
Windows System Loopback Capture remains independently PASS on its existing evidence;
combined with the existing build/test/lifecycle results, **Phase 8A Core Acceptance
is PASS**. Physical microphone unplug, real default-output switching and the Windows
microphone privacy-denial scenario remain PENDING / NOT TESTED and non-blocking.

### Earlier Loopback and Repeated-Cycle Evidence

Initial loopback silence/tone/silence: 399 chunks, 127680 samples, 255360 bytes,
55107 nonzero, 72573 zero, peak 0.234833, RMS 0.0867078, start 20 ms, Stop 12.052 ms.
Quiet window peaks approximately 0.0000305-0.0000610; tone windows approximately
0.2000-0.2348. One native discontinuity was marked rather than hidden. This proves
real system output capture on the headphones endpoint, independent of microphone.

Both real paths completed **20/20 Start-Stop cycles**; summary records report
Stopped=true, invalid_chunk=false, dropped_chunks=0 and no post-Stop PCM. Each
cycle includes a 200 ms stopped observation. Local metadata reports reside in
ignored .cache/phase8a/, never in Git. No PCM/WAV or raw audio is stored.

### Five-Minute and Exit Results

| Real 300-second Probe | Microphone | System Loopback |
| --- | --- | --- |
| Chunks | 14996 | 15000 |
| Samples | 4798720 | 4800000 |
| Bytes | 9597440 | 9600000 |
| Nonzero / zero | 4745196 / 53524 | 4797454 / 2546 |
| Peak / RMS | 0.00836182 / 0.00122856 | 0.990051 / 0.137089 |
| Start / Stop milliseconds | 85 / 20.247 | 20 / 10.608 |
| Dropped chunks | 0 | 0 |
| Native discontinuity markers | 0 | 1 |
| Invalid chunks | false | false |
| Final Stopped | true | true |

Five-minute stability PASS per backend. Microphone delivered approximately 50
chunks/second; endpoint/native/owner buffers pending at Stop are intentionally
discarded, so a wall-clock 300-second window is not an exact converter-count test.
Loopback delivered 15000 chunks. Its long run captures the actual system mix,
including output beyond the controlled tone; it is not a calibrated pure-tone
quality benchmark. Converter sample-count correctness is separately unit tested.

Process snapshots (whole Probe, not isolated backend or ASR): microphone working
set 24817664 -> 24764416 bytes, private 27938816 -> 27815936 bytes. CPU accumulated
0.69 seconds near 268 output lines (roughly 0.3% of one logical core). Loopback
working set 25051136 -> 25145344 bytes, private 28057600 -> 28119040 bytes; CPU
4.75 seconds near 260 output lines (roughly 1.8% of one core, including the Qt tone
fixture). These finite snapshots show no observed unbounded growth; they are not
a leak-proof or performance guarantee. Both processes exited normally with code 0.

Real --exit-while-running checks independently captured 98 microphone chunks and
99 loopback chunks, then quit normally (code 0); aboutToQuit and destructors released
capture, and neither short-check process remained. No helper/audio external process
is created. Nonexistent microphone/loopback endpoint probes return code 1 with an
explicit error; the loopback retains API/HRESULT detail rather than timing out.
Physical unplug/default switching and denied Windows privacy permission remain
PENDING, not inferred from these invalid-ID cases.

Translator startup smoke test: own launched process PID 17144 had a visible native
window handle 2687086 and Responding=true; normal CloseMainWindow succeeded and
exit code was 0. Existing Region, OCR, realtime, global shortcuts/tray/appearance
suites pass. No fresh full manual Phase 7 acceptance is claimed. PE imports of
Translator remain Qt Core/Gui/Network/Widgets (no Qt Multimedia), confirming this
foundation is not prematurely wired into the production overlay.

## Remaining Limitations and Phase 8B Boundary

- Physical microphone unplug, real default-output switching and Windows microphone
  privacy-denial scenarios remain PENDING / NOT TESTED; non-blocking for Core Acceptance.
- Non-Windows microphone build/runtime not validated; loopback explicitly unsupported.
- Linear resampling is not anti-aliased; multichannel downmix is unweighted.
- Endpoint idle packets may stop arriving; no wall-clock silence synthesis.
- Sample timestamps are monotonic stream positions, not hardware timestamps.
- Slow consumers can drop bounded queued chunks; gaps/counters are explicit.
- Blocking driver initialization cannot be forcibly cancelled; no thread termination.
- Qt Multimedia/runtime redistribution remains outside this task; no new ZIP/license work.

Future Phase 8B consumes only PcmChunk plus session/sequence/discontinuity/error
signals through a bounded consumer. ASR owns model loading/cancellation/results;
it must not run in capture callbacks. No such backend or interface implementation
is included here. Do not begin Phase 8B without a separately scoped request.

## Requested Report Ledger

1. Phase 8A Core Acceptance PASS, with subsequent owner-reported voice/quiet evidence.
2. Initial commit e5f5259166859f613cee78a9fdd1bf095c056ba9.
3. Modified CMakeLists.txt, .gitignore and README.md only outside new audio files.
4. Added src/audio/{AudioInputTypes,IAudioInput,AudioChunkBuffer,PcmConverter,
   AudioInputCoordinator,MicrophoneAudioInput,WindowsLoopbackAudioInput}; tests/
   {AudioConverterTest,AudioCoordinatorTest,AudioBackendTest,AudioCaptureProbe}.cpp;
   docs/audio-input-phase8a.md. Implementation classes have .h/.cpp pairs.
5. Independent TranslatorAudio library -> test/Probe, not main application wiring.
6. IAudioInput enumeration/start/stop/running/native format/selected device/signals.
7. Coordinator explicit states, factory injection, bounded delivery, session guard.
8. InputKind Microphone and SystemLoopback only.
9. Unified signed int16 little-endian / mono / 16000 Hz.
10. 20 ms / 320 samples / 640 bytes.
11. Steady-clock sample timeline + sequence + generation + discontinuity.
12. Qt asynchronous QIODevice microphone implementation, no extra worker.
13. QMediaDevices, QAudioDevice, QAudioSource, QAudioFormat, QMicrophonePermission.
14. Two actual microphones enumerated with byte IDs/default flags.
15. Actual source format 48 kHz stereo float32 LE.
16. Actual opening/streaming PASS; full microphone voice acceptance PASS in later owner report.
17. Nonzero microphone PCM YES; later controlled speaking/quiet test confirms real voice input.
18. Speaking/quiet distinction PASS in owner report; earlier low external-mic evidence retained separately.
19. Microphone 20/20 real Start-Stop cycles PASS, plus fake repeated cycles.
20. Physical microphone disconnect PENDING; fake loss/restart PASS.
21. Windows shared-mode event-driven render-endpoint loopback.
22. IMMDeviceEnumerator/IMMDevice/IAudioClient/IAudioCaptureClient APIs above.
23. Dedicated worker, stop event, bounded owner-thread delivery, join.
24. Worker COM MTA initialized/uninitialized on same thread, interfaces released first.
25. Three actual active render endpoints; default eConsole identified.
26. Realtek headphones endpoint selected, stable ID recorded above.
27. Native mix 48 kHz stereo float32 LE.
28. Real system-output silence/tone/silence capture PASS.
29. Nonzero loopback PCM YES; no microphone backend used by that Probe.
30. Silence YES: quiet levels and zero samples observed, SILENT conversion tested.
31. Loopback 20/20 real Start-Stop cycles PASS, plus fake repeated cycles.
32. Physical default-output change PENDING; worker monitoring implemented.
33. Float downmix, finite-value sanitation, clamp, signed int16 quantization tested.
34. Stereo arithmetic mean tested without integer overflow.
35. 48 kHz -> 16 kHz tested, one-second count and minute continuity.
36. 44.1 kHz -> 16 kHz tested, one-second count and minute continuity.
37. Arbitrary read boundaries give identical converted output.
38. Minute counts within <=2 samples before final fixed-chunk tail; no accumulating drift.
39. Actual five-minute measurements documented above; no lifetime guarantee inferred.
40. Whole-Probe CPU measurements documented above, not isolated backend CPU.
41. Converter bounded residual; mailbox max 50; explicit overflow count/gap tested.
42. Device notifications/watchdog, invalidation, fatal teardown; no infinite retry.
43. Structured ErrorCode/user message/native diagnostic.
44. Explicit Qt denial if identifiable; Windows open denial may remain ambiguous.
45. Audio initially Stopped; main executable constructs no audio coordinator.
46. No automatic recording/capture on application launch.
47. Local CLI QA Probe with kind/device/duration/cycles/tone/active-exit options.
48. No WAV/PCM recorded or committed; metadata reports only in ignored .cache/.
49. Three new suites: converter, coordinator and native failure cleanup.
50. Existing CTest Release 19/19 PASS and Debug 19/19 PASS; no rerun in this closeout.
51. Existing Windows MinGW Release build PASS; no rerun.
52. Existing Windows MinGW Debug build PASS; no rerun.
53. Existing OCR source unchanged.
54. Paddle/helper/protocol source unchanged.
55. Realtime OCR scheduler/source unchanged.
56. Translation backend source unchanged; no DeepL probe/network changes required.
57. TranslationCoordinator not integrated.
58. TranslationWindow not integrated.
59. ASR not implemented.
60. Phase 8B not started.
61. Packaging unchanged; no ZIP/runtime binary generated for release.
62. License files and pre-existing license script untouched and unstaged.
63. README has a short foundation-only status, no speech-recognition claim.
64. This document records architecture, tests, real evidence and limitations.
65. Final Git state supplied after explicit staging/push; pre-existing untracked items remain.
66. Commit hash supplied in final response (not self-embedded in its own commit).
67. Actual non-force origin/main push outcome supplied in final response.
68. Original LunaTranslator read-only; status checked before commit.
69. Microphone Capture final status PASS based on owner's real speaking/quiet test.
70. Windows System Loopback Capture PASS independently.
71. Phase 8A Core Acceptance PASS; physical unplug/output switching/privacy denial remain non-blocking pending.
72. Remaining limitations enumerated above; real device/voice checks not invented.
73. Phase 8B only consumes bounded PCM and lifecycle/session metadata; no ASR work begun.
