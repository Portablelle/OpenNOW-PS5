# Native stream quality — 00.002.037

Press **L1** in the catalog before launching a game to cycle targets. Selection is kept for this app run. Stop an active session with Options + Touchpad before changing quality.

| Profile | Stream target | Maximum requested video bitrate |
| --- | --- | --- |
| Qualified 4K HDR | 3840 × 2160 at 120 FPS, HEVC Main10 | 100 Mb/s |
| Qualified 4K90 HDR | 3840 × 2160 at 90 FPS, HEVC Main10 | 100 Mb/s |
| Qualified 4K SDR | 3840 × 2160 at 120 FPS, H.264 hardware | 100 Mb/s |
| Qualified 4K90 SDR | 3840 × 2160 at 90 FPS, H.264 hardware | 100 Mb/s |
| Qualified 1080p hardware | 1920 × 1080 at 60 FPS, H.264 hardware | 75 Mb/s |
| Quality | 1920 × 1080 at 30 FPS | 25 Mb/s |
| Smooth | 1280 × 720 at 60 FPS | 20 Mb/s |
| Experimental | 1920 × 1080 at 60 FPS | 75 Mb/s |
| Compatibility | 1280 × 720 at 30 FPS | 10 Mb/s |

Startup selects the highest profile that passes native decoder/GPU/display qualification. The four software profiles remain selectable, and audio selection is independent of the video profile. Native HDR requests HEVC Main10/PQ; other profiles request H.264 SDR. Cloud allocation, network-test profile, peer resolution, SDP bandwidth and NVST viewport/FPS/bitrate use the same selection. These are requests, not guarantees from the server. GFN can reduce bitrate/resolution; raising the bitrate does not increase the game's render FPS. Network bandwidth to the GFN server, subscription capabilities and software decoding throughput still matter even on gigabit Ethernet.

The software path fills a 1920 × 1080 canvas; the GPU build presents it through its selected native scanout. 1080p frames retain their resolution instead of being reduced to 720p. Lower-resolution frames are bilinearly scaled to 1080p. YUV-to-RGB conversion respects limited/full range and the BT.709/BT.601 matrix. Upscaling a software 1080p picture does not make its source 4K. Qualified native profiles import decoder surfaces directly into the GPU at their requested resolution.

## Audio

Press **R2** in the catalog to cycle **Auto / Stereo / 5.1 / 7.1** before launch. Selection lasts for this app run. All modes use Opus at 48 kHz and signed 16-bit PCM output.

| Negotiated format | Requested Opus bitrate ceiling | AudioOut PCM channels |
| --- | --- | --- |
| Stereo | 256 kb/s | 2, or FL/FR in an already opened 8-channel port |
| 5.1 | 384 kb/s | 8, with unused channels silent |
| 7.1 | 510 kb/s | 8 |

Auto requests the widest layout the PS5 output port can accept. Startup probes the eight-channel AudioOut format and closes the probe handle; if unavailable, requests are capped at stereo. **This probes the API port, not the physical speaker count or HDMI receiver capabilities.** Output routing/downmix remains under the PS5 system configuration. Stereo can be explicitly selected for headphones or two-speaker systems.

CloudMatch requests the selected channel count and audio format. WebRTC selects only an Opus stereo or explicitly described `multiopus/48000/6` / `multiopus/48000/8` layout actually listed in the server's audio section. Stream counts, coupled streams and a complete channel mapping must be valid. The widest supported offer at or below the request wins; a stereo-only or malformed surround offer falls back to an offered stereo codec. Missing supported audio fails negotiation rather than decoding an unknown layout. Opus multistream output uses the server's mapping, then converts family-1 speaker order to AudioOut format 2 (FL, FR, FC, LFE, BL, BR, SL, SR). Dynamic audio/RED RTP payload numbers follow the negotiated offer. If a seat describes surround but initially sends eight consecutive valid elementary stereo packets with no successful surround decode, playback switches to stereo for that session. An established surround stream is not downgraded by isolated packet damage.

Missing audio up to 60 ms uses a matching redundant Opus block when offered. For one missing packet, the decoder can attempt in-band FEC from the next packet; Opus supplies PLC if FEC is absent, and diagnostics count FEC attempts separately from confirmed RED recovery. Larger or invalid timestamp gaps clear and restart the audio epoch. The jitter reserve starts at 20 ms, in addition to one packet (40 ms startup buffering for 20 ms packets), and rises to 40 ms after repeated starvation. Queue excess is bounded to twice the reserve or one complete Opus packet, capped at 120 ms; overflow keeps the newest continuous samples. Output uses 256-frame grains with silent tails on underrun.

Private logs and `live-video.status` record requested/negotiated channels, primary encoded bytes and decoded sample counts, current/peak queue depth, dropped audio frames, output errors, RED recovery, FEC attempts and underruns. The byte/sample ratio estimates the received primary codec bitrate; it excludes RED and transport overhead. Queue depth is not end-to-end latency. A full audio/video synchronization clock, physical speaker detection, lossless audio and Atmos are not implemented.

The audio changes have host ASan/UBSan coverage for allocation, SDP fallback, malformed layouts, payload parsing, channel placement, queue overflow/wrap, re-priming and sustained 20 ms packets at 256-frame output grains. Native compilation/linking/import validation and live console validation are separate. **No live 5.1/7.1 GFN or speaker-placement result is established by synthetic tests.** Servers may still offer only stereo on this WebRTC transport; this change does not add the separate native RTSP/NVST transport.

Protocol references: [OpenNOW-Mac session audio request](https://github.com/OpenCloudGaming/OpenNOW-Mac/blob/d9fc3d6b7ea78b7bb1e331789daa25740b8794de/OPN/GameServices/OPNSessionPayloads.swift), [Opus multistream layout](https://github.com/OpenCloudGaming/OpenNOW-Mac/blob/d9fc3d6b7ea78b7bb1e331789daa25740b8794de/GFN/NVST/BifrostFree/NvstOpusMultistreamLayout.swift), and the already pinned PS5 hardware-video research AudioOut eight-channel path. Existing pins remain unchanged.

## Console testing

Start with Quality. Check motion, text sharpness, input latency, sound continuity and private media counters. Compare Smooth if the software decoder drops frames. Try Experimental at 75 Mb/s only after lower targets behave well; return to Compatibility if the higher target stutters or fails. The frame rate requested is not an on-console performance measurement. Counter logs include decoded frames, dropped access units, audio recoveries/concealments/underruns and the selected target. No credentials or media payloads are logged.

The host suite validates profile consistency across allocation and negotiation, audio payload parsing and bounded recovery/wraparound. The VPS build checks native compilation, linking, packaging and imports. New audio modes, actual multichannel negotiation/speaker placement, color reproduction, CPU/heap load and end-to-end latency still require live console testing.

## Native video qualification

00.002.014 integrates Videodec2, GPU presentation, native 2160p120 mode selection, 10-bit PQ/BT.2020 scanout and HEVC negotiation. Startup qualification on the user’s PS5 passed the original 4K120 Main10 picture, GPU import and flip, with actual VideoOut 3840 × 2160 at 119.88 Hz and HDR active. Native H.264 test pictures also passed. Actual GFN negotiation, sustained game performance and flicker acceptance remain separate checks. See [native hardware video](NATIVE_HARDWARE_VIDEO.md).

If a selected HDR profile is unsupported by the server, stop the session and choose a qualified H.264 profile with L1. Version .016 renders Main10 BT.709 SDR as SDR if the server sends it despite an HDR request, and accepts resolution reductions inside the 4K allocation. Actual PQ/HDR is established from stream SPS metadata; ten-bit depth or HDR HDMI output alone cannot establish it. Unsupported transfer functions are rejected. Stereo audio remains unchanged.

## Reference recovery in 00.002.013

When a complete H.264 picture is lost, an overflowing queue discards a reference, or FFmpeg reports corrupt/concealed output, the renderer keeps the last good image until an IDR arrives. The decoder flushes old references before resuming and suppresses stale in-flight output. This prevents intentionally continuing with an incomplete reference chain. The user reports that periodic pixelation persists after this update; its cause is not established. Diagnostics now include actual decoded dimensions and reference-loss counters. Work toward a hardware HEVC Main10/GPU path is documented in [native hardware video](NATIVE_HARDWARE_VIDEO.md); it is now integrated and installed, with game acceptance still pending.


Version .016 confirms Main10 SDR decoding/presentation at 2880 × 1620 after initial 4K API rejection; the user still observes flicker. Version .017 increases the HEVC DPB budget from four to six because the live SPS signals five buffered pictures. Startup and live 4K acceptance of the DPB correction pass, but the user still reports severe periodic pixelation. Version .018 adds source-pinned NVST QoS feedback; the server acknowledges the control channel, but the user still reports the defect after about two minutes. Version .019 supports a bounded capture requested during the live session and late counter snapshots. The actual server downscale trigger is still unproven.

The .019 late private source capture reproduces periodic compression outside PS5 presentation: base slice QP reaches 50 on key pictures and improves toward 29–30 between them. The observed source is 2880 × 1620 at about 2.8 Mbps. Version .020 requests fixed native resolution and a 75% bitrate floor/initialization; the user subsequently reports that the periodic quality degradation is gone, but sustained-cadence tests show native decoder overload and queue recovery storms.

The user confirms .020 removed the quality pulse, but reports severe low FPS. Live counters identify hundreds of local two-AU queue overflows and decoder resets despite zero RTP loss/API error. Version .021 preserves quality negotiation and changes native compressed buffering/handoff, with decode/presentation timing snapshots. Sustained cadence remains unverified.

Version .022 adds user-requested 4K90 HDR and SDR profiles. The 90 FPS value is propagated through CloudMatch monitor/net-test, peer resolution, NVST negotiation and native envelope qualification; HDMI output stays at the verified 120 Hz mode. Requested FPS, measured presentation cadence and actual HDR metadata remain separate. Native timing snapshots split input copying, cache publication, API decode, optional API flush and surface-pool blocking.

Installed version .023 expands the decoder worker mask from three to at most five physical cores within the title's observed CPU availability, with a fallback to the previous configuration. It retains depth one and the .022 quality/codec/profile settings. The console accepts the wider mask, but the comparable loaded HEVC scene still presents about 31 FPS with roughly 21 ms per native decode call; no useful speedup is demonstrated. GPU import/draw averages about 0.47 ms in that sample.

Version .024 requests a single prediction reference for native streams, keeping the existing resolution, FPS, HDR request and quality floor. A private source capture showed one HEVC slice per picture, no wavefront processing and up to four active prediction references. The new request aims to simplify that source structure; the server may not enforce it and performance remains unverified. Software compatibility requests remain unchanged. Depth-two/depth-three memory queries collect capability information only; active decoding stays at depth one.

The .024 native build/import audit passes. Installed on 2026-10-04 with all 29 files read back and hash-verified in raw SELF mode, after confirming the app sandbox was absent. A complete .023 rollback tree is retained. Live acceptance remains pending.

Live .024 capture confirms that GFN reduces active prediction references to one, but a comparable loaded ten-second interval after two minutes still presents 31.08 FPS with 20.268 ms native decode calls. Source metadata remains SDR. Depth-two/depth-three memory queries succeed; this does not prove decoder creation or output. The next .025 startup probe tests allocation/creation/reset/cleanup without submitting input, while live decoding remains at depth one.

Version .025 passes host ASan/UBSan tests and the native build/import audit. Installed on 2026-10-04 after confirming the sandbox was absent, with all 29 activated files read back and hash-verified in raw SELF mode. The complete .024 rollback tree is retained. Deeper configuration creation remains unverified until startup on the console; this version does not claim a throughput improvement.

Console .025 accepts and closes both deeper Main10 configurations. Installed .026 adds separate owned inputs, FIFO output metadata and complete idle drain before resuming. Startup compares 32 fixture outputs across resets and drain/resume cycles against the classic image, requiring GPU import, a final flip and successful cleanup before selecting a deeper live pipeline. If qualification or subsequent decode/drain fails, it retains or reopens depth one. This is controlled safety qualification, not a 120 FPS or HDR claim. Active depth and pending inputs are recorded privately. Host ASan/UBSan tests and the native build/import audit pass; all 29 installed files were read back and hash-verified, with the complete .025 rollback tree retained. Live qualification/performance remain pending.

Console startup passes the 32-picture depth-three qualification, including sampled visible Y/UV equality, FIFO timestamps, both reset cycles, complete drain/resume, GPU flip and cleanup. Live UHD Main10 can therefore use depth three. Representative game throughput and actual source HDR remain unverified.

The loaded .026 interval after two minutes receives, decodes and presents 92.13 pictures/s in 4K at 52.15 Mbps, with no queue overflow/reset, RTP/AU loss or API error. The user reports fluid motion. The pipeline keeps pace with the received source; the 1.573 ms average native API call measures occupancy while pictures overlap, not total decode latency. GFN still supplies about 92 pictures/s and SDR color metadata despite the 4K120 HDR request, so neither actual 120 FPS nor source HDR is established.

With the game's VSync disabled and FPS limit raised to 240, a subsequent ten-second sample remains at 92.43 received/decoded/application-drawn pictures/s and 51.13 Mbps in 4K SDR. No loss, queue drop, reset or API error is recorded cumulatively at 19 minutes 30 seconds. No in-game HDR option is visible according to the user; this alone does not determine the game's HDR capability. Application draw counts precede the buffer swap and do not independently measure physical screen refresh. The next comparison is the same game's actual stream FPS/HDR on the official Mac client, before attributing the remaining limitation to game rendering or native negotiation.
