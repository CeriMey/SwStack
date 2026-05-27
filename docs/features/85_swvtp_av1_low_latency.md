# SwVTP AV1 low-latency transport

## 1) But

SwLive uses SwVTP, a low-latency binary video transport protocol for H264, H265 and AV1.

The v1 implementation started with AV1 and now validates AV1, H264 and H265 transport paths while
targeting sub-100 ms glass-to-glass latency on real networks. The protocol prefers a recent
decodable frame over a perfect late frame.

## 2) Perimetre v1

Included:
- fixed binary datagram header (`SWVT`, version, message type, stream/track ids),
- AV1, H264 and H265 frame fragmentation under a conservative UDP MTU,
- AV1 OBU scanning for sequence header and temporal layer metadata,
- deadline-based frame reassembly,
- short-window NACK generation,
- native receiver stats and adaptive bitrate decisions,
- native clock sync and frame latency estimation without synchronized clocks,
- native delivery modes: unicast, broadcast, and 239.x.x.x multicast,
- native track typing for video, audio, KLV metadata, and control,
- reusable `swvtp://` video source for `SwMediaSourceFactory`,
- low-latency AV1 GUI player example with live receive/decode/display metrics.

Not included yet:
- generalized `SwVtpSession` class shared by all senders/receivers,
- AV1 hardware encoder backend,
- FEC parity packets,
- full congestion controller loop.

## 3) API & concepts

Core protocol:
- `src/media/swvtp/SwVtpProtocol.h`
- `SwVtpHeader`
- `SwVtpDatagram`
- `SwVtpUdpEndpoint`
- `SwVtpStreamConfig`
- `SwVtpClientAnnouncement`
- `SwVtpReceiverStats`
- `SwVtpNackRequest`
- `SwVtpClockSyncPing`
- `SwVtpClockSyncPong`
- `SwVtpClockEstimate`
- `SwVtpFrameLatencySample`
- `SwVtpAdaptiveBitrateController`

AV1 profile:
- `src/media/swvtp/SwVtpAv1.h`
- `SwVtpAv1Parser`
- `SwVtpAv1Packetizer`
- `SwVtpAv1Reassembler`

Generic video bitstream receiver:
- `src/media/swvtp/SwVtpFrameReassembler.h`
- automatic `FrameFragment` reassembly for AV1, H264 and H265,
- automatic `SwVideoPacket` codec and source track codec publication.

KLV metadata profile:
- `src/media/swvtp/SwVtpKlv.h`
- `SwVtpKlvPacketizer`
- `SwVtpKlvReassembler`

Self-test:
- `exemples/62-SwVtpAv1SelfTest/SwVtpAv1SelfTest.cpp`

UDP server/client validation:
- `exemples/63-SwVtpUdpLoopback/SwVtpUdpLoopback.cpp`

Low-latency GUI player:
- `src/media/source/SwVtpVideoSource.h`
- `exemples/64-SwVtpPlayer/SwVtpPlayer.cpp`

Server-side media architecture:
- `src/media/server/SwMediaServer.h`
- `src/media/server/SwMediaServerFactory.h`
- `src/media/server/SwVideoTransportServer.h`
- `src/media/server/SwVideoPublishStream.h`
- `src/media/server/SwVideoPublishSource.h`
- `src/media/server/SwVideoServerMetrics.h`
- `src/media/encoder/SwAv1LiveEncoder.h`
- `src/media/encoder/SwVideoEncoder.h`
- `src/media/transport/SwTransportEndpoint.h`
- `src/media/transport/SwUdpServerTransport.h`
- `src/media/rtp/SwRtpServerTransport.h`
- `src/media/rtsp/SwRtspServerTransport.h`
- `src/media/swvtp/SwVtpServerTransport.h`
- `src/media/swvtp/SwVtpFeedbackController.h`

## 4) Flux

Server side:

```text
SwVideoPublishSource -> SwAv1LiveEncoder -> SwMediaServer -> SwVtpServerTransport -> UDP datagrams
```

Client side:

```text
swvtp:// URL -> SwVtpVideoSource -> SwVtpAv1Reassembler -> SwVideoPacket(AV1) -> decoder -> SwVideoWidget
```

Server side uses the same media-server interface as the other transports:

```text
SwVideoPublishSource -> SwVideoEncoder -> SwMediaServer -> SwVideoTransportServer
```

Transport implementations are interchangeable behind `SwVideoTransportServer`:

```text
udp://   -> SwUdpServerTransport
rtp://   -> SwRtpServerTransport
rtsp://  -> SwRtspServerTransport
swvtp:// -> SwVtpServerTransport
```

`udp://` sends the encoded packet payload as UDP datagrams, while `rtp://` and the current RTSP
media plane emit RTP datagrams through the same `SwUdpSocket` network primitive. SwVTP adds the
client announcement, clock sync, frame-latency timestamps, AV1 packetization and ABR feedback.

The scheme-based server factory is `SwMediaServerFactory`; SwVTP is intentionally not special at
the media-server boundary.

## 5) Delivery modes

SwVTP stream config carries a native UDP delivery mode:
- `Unicast`
- `Broadcast`
- `Multicast239`

Unicast uses the configured IPv4 address as a client authorization mask, not as a fixed destination.
The server starts sending to the announced client address only after a valid client announcement:
- `0.0.0.0` accepts any unicast client address,
- `192.0.0.0` accepts any `192.x.x.x` client,
- `192.168.0.0` accepts any `192.168.x.x` client,
- `192.168.1.20` accepts only `192.168.1.20`.

The mask must be prefix-like at octet level: once an octet is `0`, all following octets must also
be `0`. For example, `192.0.1.0` is rejected.

Broadcast is explicit and only accepts `255.255.255.255`.

Multicast is explicit and intentionally restricted to `239.x.x.x` groups for administratively
scoped multicast. Other multicast ranges are rejected by the protocol model.

## 6) Latence native

Each frame fragment carries two server-side timestamps:
- `captureTimeUs`: capture timestamp of the original video frame,
- `sendTimeUs`: timestamp when the fragment leaves the sender.

Client and server clocks do not need to be synchronized. SwVTP uses a compact ping/pong exchange:

```text
clientSendTimeUs -> serverReceiveTimeUs -> serverSendTimeUs -> clientReceiveTimeUs
```

From this sample, `swVtpEstimateClock()` estimates the server-to-client clock offset, network RTT,
server processing time, one-way uncertainty, and a confidence value. The receiver then calls
`swVtpMeasureFrameLatency()` for each frame:
- transfer latency = client receive time minus corrected server send time,
- capture latency = client receive time minus corrected capture time,
- uncertainty = half of the network RTT for that sync sample.

The receiver can report these values back through `SwVtpReceiverStats`:
- `transferLatencyMs`,
- `captureLatencyMs`,
- `clockUncertaintyMs`.

This gives the server a native signal for glass-to-glass pressure while still exposing the important
limitation: without true clock sync, asymmetric network paths produce an uncertainty window.

## 7) Erreurs et pertes

- A fragment that arrives after `deadlineUs` is rejected as stale.
- An incomplete frame is expired by deadline and removed.
- NACK is generated only when `now + retransmitBudget < deadline`.
- The server keeps a short recent-fragment cache and retransmits requested video fragments only to
  the client that emitted the NACK.
- Datagram pacing is bounded and disabled below 1 ms intervals so Windows/Linux scheduler jitter
  cannot consume the sub-100 ms frame budget on heavily fragmented frames.
- Adaptive bitrate downshifts immediately on hard loss/queue pressure and upshifts only after
  a cooldown on clean receiver stats.
- Estimated bandwidth is treated as a native pressure signal: target bitrate is capped to a
  safety margin below the receiver bandwidth estimate, and this also arms the upshift cooldown.
- Upshift requires both clean loss/NACK stats and enough bandwidth headroom, so the encoder target
  does not probe above the receiver's useful capacity.
- Duplicate fragments are counted but do not rebuild state.
- A completed frame clears its buffered fragments immediately.

## 8) Perf

Default design targets:
- datagrams <= 1200 bytes once UDP transport is wired,
- no JSON in the hot path,
- fixed 64-byte binary header,
- no global reliable stream and no head-of-line blocking,
- drop old incomplete frames instead of growing latency,
- fast ABR downshift, slow ABR upshift.

## 9) Validation

The `SwVtpAv1SelfTest` executable validates the protocol layer without requiring real network
sockets or hardware encoders. It covers:
- fixed binary header roundtrip and malformed datagram rejection,
- exact datagram length validation,
- unicast client masks, client announcements, broadcast, and 239.x.x.x multicast,
- AV1 packetizer success/failure cases and timestamp fallback,
- out-of-order frame reassembly,
- duplicate, stale, inconsistent, and oversized fragment rejection,
- NACK payload roundtrip and invalid control payload rejection,
- receiver stats payload roundtrip, including latency fields,
- clock sync ping/pong payloads,
- transfer latency and capture latency with synchronized and unsynchronized clocks,
- invalid clock samples, overflow/underflow guards, and confidence floor,
- adaptive bitrate downshift, upshift, bandwidth headroom, queue pressure, and min/max clamp.

Run it directly:

```powershell
cmake --build build-test --target SwVtpAv1SelfTest --config Release
.\build-test\exemples\62-SwVtpAv1SelfTest\Release\SwVtpAv1SelfTest.exe
```

Or through CTest:

```powershell
ctest --test-dir build-test -C Release --output-on-failure -R SwVtpAv1SelfTest
```

The media server architecture self-test validates the server transport abstraction and factory:
- SwVTP server-side video publishing through `SwMediaServer`,
- native KLV metadata track declaration, packetization, UDP delivery, and bit-exact client
  reassembly,
- receiver stats feedback into the ABR target bitrate,
- UDP, RTP, and RTSP server media-plane datagram emission through the shared transport interface.

```powershell
cmake --build build-test --target MediaServerArchitectureSelfTest --config Release
ctest --test-dir build-test -C Release --output-on-failure -R MediaServerArchitectureSelfTest
```

The media transport end-to-end self-test launches a sender and a headless player for every generic
video URL path currently exposed through `SwMediaSourceFactory`:
- `udp://` with Annex-B H264 datagrams through `SwMediaServer` + `SwUdpServerTransport`,
- `rtp://` with H264 RTP packets through `SwMediaServer` + `SwRtpServerTransport`,
- `rtsp://` with a local RTSP DESCRIBE/SETUP/PLAY control server and UDP/RTP H264 media,
- `swvtp://` with AV1, H264 and H265 over `SwMediaServer` + `SwVtpServerTransport`.

The SwVTP section validates more than packet arrival:
- automatic bitstream/codec propagation from server stream config to player track and packet codec,
- H264/H265 frame fragmentation and reassembly under a small MTU,
- AV1 fragment-loss recovery through client NACK, server fragment cache lookup, and targeted
  retransmission,
- receiver pressure feedback sent automatically as `ReceiverStats`,
- server-side automatic target bitrate downshift and `BitrateControl` propagation back to the player,
- transfer and capture latency samples under the 100 ms local validation budget.

```powershell
cmake --build build-test --target MediaTransportEndToEndSelfTest --config Release
ctest --test-dir build-test -C Release --output-on-failure -R MediaTransportEndToEndSelfTest
```

The SwVTP case intentionally uses `127.0.0.0` as the unicast client mask. This matches SwVTP's
mask semantics: `0.0.0.0` accepts any announced client, `192.0.0.0` accepts any `192.x.x.x`
client, and `127.0.0.0` accepts local loopback clients.

For a real locally encoded AV1 stream, generate a short IVF file with FFmpeg/libaom and pass it to
the same validation executable:

```powershell
ffmpeg -y -hide_banner -loglevel error -f lavfi -i "testsrc2=size=160x90:rate=30:duration=1" -frames:v 30 -pix_fmt yuv420p -c:v libaom-av1 -cpu-used 8 -deadline realtime -lag-in-frames 0 -g 30 -b:v 300k -f ivf "build-test\swvtp_av1_real_stream.ivf"
ffmpeg -hide_banner -loglevel error -i "build-test\swvtp_av1_real_stream.ivf" -f null -
.\build-test\exemples\62-SwVtpAv1SelfTest\Release\SwVtpAv1SelfTest.exe "build-test\swvtp_av1_real_stream.ivf"
```

The IVF mode validates that each encoded AV1 frame is packetized into SwVTP datagrams, reassembled,
and compared bit-exact with the original encoded frame payload.

For a real local UDP server/client validation with metrics:

```powershell
cmake --build build-test --target SwVtpUdpLoopback --config Release

# Terminal 1
.\build-test\exemples\63-SwVtpUdpLoopback\Release\SwVtpUdpLoopback.exe server 0.0.0.0 55245 build-test\swvtp_av1_real_stream.ivf 0.0.0.0 30 512

# Terminal 2
.\build-test\exemples\63-SwVtpUdpLoopback\Release\SwVtpUdpLoopback.exe client 127.0.0.1 55245 127.0.0.1
```

The server waits for a unicast client announcement accepted by the `0.0.0.0` mask, then sends the
AV1 IVF frames and a synthetic KLV metadata packet per frame as SwVTP UDP datagrams. The client
prints completed frames, KLV packet count, datagram count, video/KLV bytes, throughput, clock sync
uncertainty, transfer latency, capture latency, duplicates, stale fragments, and dropped frames.

The same non-GUI SwVTP validation is expected to work under Linux/WSL:

```bash
cmake -S . -B build-wsl -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-wsl --target SwVtpAv1SelfTest SwVtpUdpLoopback MediaServerArchitectureSelfTest MediaTransportEndToEndSelfTest
ctest --test-dir build-wsl --output-on-failure -R 'SwVtpAv1SelfTest|MediaServerArchitectureSelfTest|MediaTransportEndToEndSelfTest'

# Terminal 1
./build-wsl/exemples/63-SwVtpUdpLoopback/SwVtpUdpLoopback server 0.0.0.0 55245 build-test/swvtp_av1_fullhd_stream.ivf 0.0.0.0 5 1200

# Terminal 2
./build-wsl/exemples/63-SwVtpUdpLoopback/SwVtpUdpLoopback client 127.0.0.1 55245 127.0.0.1 0 5
```

`SwVtpPlayer` is still a Win32 GUI example in the current CMake setup; use `SwVtpUdpLoopback` for
headless WSL validation until the Linux/X11 video-widget build is enabled.

For a real low-latency GUI player using the reusable `swvtp://` source:

```powershell
cmake --build build-test --target SwVtpPlayer --config Release

# Terminal 1: server sends AV1 over SwVTP after a client announcement.
.\build-test\exemples\63-SwVtpUdpLoopback\Release\SwVtpUdpLoopback.exe server 0.0.0.0 55245 build-test\swvtp_av1_real_stream.ivf 0.0.0.0 120 512

# Terminal 2: player receives, decodes, displays, and shows live metrics.
.\build-test\exemples\64-SwVtpPlayer\Release\SwVtpPlayer.exe "swvtp://127.0.0.1:55245?announce=127.0.0.1&localport=0"
```

The URL format is:

```text
swvtp://server-ip:server-port?announce=client-ip&localport=0
```

- `server-ip:server-port` points to the SwVTP sender control/data socket.
- `announce` is the client IPv4 address advertised to the server.
- `localport=0` lets the OS pick the receive port; use a fixed port when firewall rules require it.

The player can also be used as an automated validation target:

```powershell
.\build-test\exemples\64-SwVtpPlayer\Release\SwVtpPlayer.exe "swvtp://127.0.0.1:55245?announce=127.0.0.1&localport=0" --auto-exit-frames=30 --timeout-ms=20000
```

It prints final metrics including completed SwVTP frames, presented frames, decoder FPS, live
video/UDP receive bitrate, received datagrams, video/UDP bytes, clock RTT/uncertainty, transfer
latency, capture latency, duplicates, stale fragments, and dropped frames. Receiver feedback is
sent back to `SwVtpServerTransport`; when the server is hosted through `SwMediaServer` with a
`SwVideoEncoder`, the negotiated target bitrate is applied to the encoder through
`setTargetBitrateKbps()`.

## 10) TODO

- Extract the current example sender/receiver logic into reusable `SwVtpSession` and
  `SwVtpUdpTransport` components.
- Add AV1 hardware encoder API and dynamic bitrate updates.
- Add light FEC for important/keyframe fragments.
