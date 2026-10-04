# Third-party notices

## Credits and acknowledgements

OpenNOW PS5 is an independent native adaptation made possible by **OpenCloudGaming's OpenNOW and OpenNOW-Switch authors and contributors**. OpenNOW supplies the upstream desktop/protocol reference and the GPL-3.0-or-later NVST QoS work; OpenNOW-Switch supplies the MIT-licensed authentication, transport and negotiation foundations. Project links: [OpenNOW](https://github.com/OpenCloudGaming/OpenNOW), [OpenNOW-Switch](https://github.com/OpenCloudGaming/OpenNOW-Switch). Exact revisions, local adaptations and retained licenses are detailed below and in `upstream-lock.json`.

| Project | Role |
| --- | --- |
| [ps5-payload-dev/sdk](https://github.com/ps5-payload-dev/sdk) | Public PS5 headers, libc++ headers, sysroot, and Clang target support |
| [ps5-payload-dev/pacbrew-repo](https://github.com/ps5-payload-dev/pacbrew-repo) | Optional prebuilt PS5 ports and static libraries |
| [SvenGDK/SharpProspero](https://github.com/SvenGDK/SharpProspero) | Source of the ELF converter and FSELF writer in `tooling/native/` (GPL-3.0) |
| [SvenGDK/UFS2Tool](https://github.com/SvenGDK/UFS2Tool) | Optional UFS2 `.ffpkg` generation |
| [PSBrew/MkPFS](https://github.com/PSBrew/MkPFS) | Optional compressed `.ffpfsc` generation |
| [sinajet/PSFFPKG](https://github.com/sinajet/PSFFPKG) | Public `.ffpkg` procedure used as a format reference |
| [LLVM/Clang](https://github.com/llvm/llvm-project) | Native compiler |
| [GoogleTest](https://github.com/google/googletest) | Pinned host-only C++ unit-test framework |
| [zlib](https://zlib.net/) | Pinned source-built compression library used by the host FSELF tool |
| [Microsoft DirectXTex](https://github.com/microsoft/DirectXTex) | `texconv` presentation-image preparation |
| [FFmpeg](https://ffmpeg.org/) | Developer-supplied selection-audio preparation |
| [ShadowMountPlus](https://github.com/drakmor/ShadowMountPlus) | Directory-style deployment and hardware validation |

## Native build dependencies

The application build uses LLVM/Clang/lld, zlib 1.3.2, and the public
[PS5 payload SDK](https://github.com/ps5-payload-dev/sdk). The bootstrapper
downloads SDK v0.42 after verifying SHA-256
`8cfbc7cd5811e719eb4f0c47eea668d3dc7b40bc8ab11c4a5031d40c23ec02da`.
It downloads zlib 1.3.2 from the upstream source archive after verifying
SHA-256 `bb329a0a2cd0274d05519d61c667c062e06990d72e125ee2dfa8de64f0119d16`
and compiles its static archive locally. Both dependencies remain under ignored
`.deps/native/`, retain their upstream licenses, and are not distributed by
this repository. No Sony SDK file is included.

Target C++ compilation uses the LLVM libc++ headers distributed by the public
SDK. Those headers retain the Apache-2.0 WITH LLVM-exception license recorded
upstream. The 0.2 application statically links libc++, libc++abi and libunwind from this SDK; their upstream LLVM notices apply.

The PS5 ELF converter and FSELF writer in `tooling/native/` are derived from
[SharpProspero](https://github.com/SvenGDK/SharpProspero), Copyright (C) 2026
SvenGDK, GPL-3.0, and were translated to C++ and modified by BlackBearReloaded.

## Host test dependency

The host unit-test target downloads
[GoogleTest](https://github.com/google/googletest) 1.17.0 after verifying
SHA-256 `65fab701d9829d38cb77c14acdc431d2108bfdbf8979e40eb8ae567edf10b27c`.
It remains under ignored `.deps/test/`, retains its BSD-3-Clause license, and
is not linked into any PS5 application, runtime, or package artifact.

## Optional PacBrew dependencies

When selected through `PACBREW_*` build variables, the build downloads the prebuilt ports image
from [ps5-payload-dev/pacbrew-repo](https://github.com/ps5-payload-dev/pacbrew-repo)
release `v0.40.2`, verifies its published SHA-256, and extracts only the
`target/user/homebrew` prefix under ignored `.deps/pacbrew/`. It does not
replace the pinned SDK or install files globally. PacBrew recipes and every
linked third-party library retain their upstream licenses; applications must
review those terms before redistribution.

## Optional UFS2Tool dependency

When `.ffpkg` output is requested, the platform bootstrapper fetches
[SvenGDK/UFS2Tool](https://github.com/SvenGDK/UFS2Tool) at commit
`b5307a60d5b4e3a68ba680e0e33cfadf05017c77` into the ignored
`.deps/UFS2Tool` cache and builds it with the host .NET SDK. UFS2Tool is
BSD-2-Clause software and is not distributed by this repository.

## Optional MkPFS dependency

When `.ffpfsc` output is requested, the platform bootstrapper fetches
[PSBrew/MkPFS](https://github.com/PSBrew/MkPFS) at commit
`6cb8313dfe0c988ac52617794553f343243d3a56` into the ignored `.deps/MkPFS`
cache and installs its Python dependencies into an ignored virtual environment
there. MkPFS and its dependencies retain their own licenses and are not
distributed by this repository.

## Independently authored runtime shim

`tooling/native/libc_builder.cpp` and the manifests under
`tooling/native/runtime/` are independently authored for this project and
licensed under GPL-3.0-or-later. The generated `runtime/libc.prx` contains
project-authored compatibility stubs, startup code, and semantic loader
metadata. It contains no Sony runtime implementation.

Original ps5-native-app-boilerplate code is Copyright (C) 2026
BlackBearReloaded and licensed under GPL-3.0-or-later. Source and script files
carry matching SPDX identifiers.

## Original presentation assets

The BlackBear icon, selection artwork, and default selection track
`sce_sys/snd0.at9` are original assets supplied by BlackBearReloaded, Copyright
(C) 2026 BlackBearReloaded, and distributed under GPL-3.0-or-later. The track
is titled `Night Drive`.

The OpenNOW PS5 launcher icon, matching background and social preview card under `docs/branding/`
are new artwork generated with the built-in image generation tool. They are
distributed with this project's GPL-3.0-or-later licensing. Prompts and operational
export details are recorded in `docs/branding/README.md`. These are adaptation
artwork, not official OpenNOW, Sony or NVIDIA brand assets. The earlier prototype
icon is retained in that directory.

No proprietary runtime module, encryption key, or game file is included.

## OpenNOW PS5 port additions

- Device authorization behavior adapted from **OpenCloudGaming/OpenNOW-Switch**, `app/src/gfn/authentication.cpp`, MIT. Exact source revision: `upstream-lock.json`; full notice: `licenses/OpenNOW-Switch.txt`. This project does not contain the Qt/Rust desktop client.
- **ProsperoLight**, BlackBearReloaded, GPL-3.0-or-later: `src/platform/ps5_sockets.c`, `ps5_network_metrics.h`, `console_curl.c`, `console_curl.h`, and the pthread-once compatibility approach. Local adaptations fix no-argument `fcntl` handling, use this application's identity, and integrate the bundled CA file. Revision: `upstream-lock.json`.
- **cJSON** v1.7.19, Dave Gamble and contributors, MIT. Source: `src/vendor/cJSON.*`; license: `licenses/cJSON.txt`.

## Experimental hardware video backend

- Public Videodec2 interface declarations in `src/stream/native/videodec2_api.hpp` adapted from **VivaLaVent/kodi-ps5**, `overlay/xbmc/platform/ps5/video/VideoDec2.cpp`, Copyright (C) 2026 Team Kodi, GPL-2.0-or-later, used here under GPL-3.0-or-later. Revision: `upstream-lock.json`. The local backend separately implements bounded memory allocation, native mode validation, surface leases and shutdown checks. Kodi’s foreign-memory/HDR driver patch is retained in `tools/gpu/kodi-additions.py` and applied to the linked public GPU runtime. The HDR scanout packing approach in `gpu_presenter.cpp` is adapted from Kodi’s HdrOutputPS5 shader, with its GPL-2.0-or-later attribution; the resulting application is GPL-3.0-or-later.
- **ProsperoLight** and **ps5-hardware-video-decoding-research**, BlackBearReloaded, GPL-3.0-or-later: references for decoder configuration, native surface layouts, 10-bit sample alignment and buffer ownership. Separate research pins in `upstream-lock.json` retain the earlier app compatibility pin.
- `src/stream/native/decoder_worker_policy.hpp` adapts the bounded physical-core selection policy from ProsperoLight `include/moonlight_pipeline.hpp` (Copyright (C) 2026 BlackBearReloaded, GPL-3.0-or-later). The current-title read-only affinity query follows its public eight-byte cpuset interface. The hardware-reference pin in `upstream-lock.json` applies; failed wider decoder configurations fall back to the previous mask.
- **ps5-opengl** SDK 1.0.0 is an experimental GPU dependency, downloaded on the VPS and verified against the pinned archive SHA-256. It is now statically linked in the GPU build after the documented native runtime rebuild. Upstream license notices are preserved in `licenses/ps5-opengl` and the app’s `assets/licenses/ps5-opengl`. SDK source snapshots and public patch revisions in `upstream-lock.json` identify the corresponding source.
- **QR Code generator**, Project Nayuki, MIT, copied from the pinned OpenNOW-Switch tree. Full notice retained in `src/vendor/qrcodegen.*`.
- **Mozilla CA bundle** distributed by curl, `assets/cacert.pem`; included license/source information is in the PEM header. SHA-256: `a41b5d356aea97a529fe27e0f7316d2f9d946d75927476cf9cf1b90637d00505`.
- **PacBrew** libraries (libcurl, OpenSSL, zlib, zstd, libpsl) are fetched at the version/hash pinned by `tools/setup-pacbrew-dependencies.sh`. Their upstream notices and license metadata accompany the isolated sysroot; preserve those when redistributing a production build.

The original runtime boilerplate source archive used here has SHA-256 `133b4ec9d21d1f49148c823131d7fb73f93fe653d35407f71702546a88a12831`.

- `src/random.cpp` adapts ProsperoLight `platform/ps5/ps5_entropy.c` (GPL-3.0-or-later, pinned revision in `upstream-lock.json`) to checked native startup entropy.

- `src/platform/inet_pton.c`: Internet Software Consortium / BIND parser, adapted from curl 8.18.0 `lib/curlx/inet_pton.c` (ISC); full notice retained in source.

## Streaming additions (0.2)

- OpenNOW-Switch (MIT, pinned in `upstream-lock.json`): WebSocket transport/handshake/queue, SDP/NVST negotiation helpers and associated protocol tests; retained notice in `licenses/OpenNOW-Switch.txt`.
- libpeer, copied from that pinned tree with native PS5 socket, address and entropy adaptations: `vendor/libpeer/LICENSE` and `licenses/libpeer.txt`.
- Mbed TLS, libsrtp and usrsctp are source-built from the pinned Switch archive. Notices are retained in `licenses/mbedtls.txt`, `licenses/libsrtp.txt` and `licenses/usrsctp.txt`. Source retrieval and verification are in `tools/setup-stream-sources.sh`.
- PacBrew FFmpeg libavcodec/libswscale provide software H.264 decode and color conversion; Opus provides stereo audio decode. Their upstream licenses and PacBrew build configuration apply to the linked archives. Preserve the corresponding source/build metadata and notices with redistribution.

- `src/stream/AudioRtpUtils.hpp` and `tests/audio_rtp_utils_test.cpp`: RTP audio redundancy parsing and regressions from the pinned OpenNOW-Switch tree (MIT; `licenses/OpenNOW-Switch.txt`).

- `src/platform/app_heap.c`: process-lifetime native allocation arena from pinned ProsperoLight `src/runtime/app_heap.c` (GPL-3.0-or-later). Local title name adaptation; 128 MiB default arena and malloc-family ownership routing retained. Native process heap size follows its 256 MiB parameter configuration.

- `src/platform/gpu_compat.c`: local constant-initializer TLS thunk and optional diagnostics shim. The GPU build extracts the public SDK’s LLVM emulated-TLS object from its libc archive; LLVM’s Apache-2.0 with LLVM exceptions applies. No vendor module is redistributed.
- `assets/*-check.h264` and `assets/hdr-check.hevc`: original FFmpeg test-pattern qualification fixtures, generated by `tools/gpu/make-fixtures.sh`; no game imagery is included.

- NVST QoS wire layout and successful-send accounting in `src/stream/nvst_qos.hpp` are adapted from OpenCloudGaming/OpenNOW’s `nvst_control.rs` and PR #908, under GPL-3.0-or-later. Exact revision is pinned in `upstream-lock.json`. No Qt/Rust runtime is distributed.

## Multichannel audio protocol references

Audio format requests and surround speaker-order knowledge reference
[OpenNOW-Mac](https://github.com/OpenCloudGaming/OpenNOW-Mac) revision
`d9fc3d6b7ea78b7bb1e331789daa25740b8794de`, recorded in `upstream-lock.json`.
The WebRTC offer parser, AudioOut conversion and bounded playout queue are local
GPL-3.0-or-later implementations; no Swift source or binary is distributed.
The AudioOut eight-channel format also follows the already pinned
PS5 hardware-video research Moonlight audio path. Opus multistream decoding uses
the existing BSD-licensed libopus dependency and its retained license.
