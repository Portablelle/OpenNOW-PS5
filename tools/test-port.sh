#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/host-tests
${CC:-cc} -std=c11 -O1 -g -fsanitize=address,undefined -c src/vendor/cJSON.c -o build/host-tests/cJSON.o
${CXX:-c++} -std=c++20 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -Isrc tests/gfn_test.cpp src/gfn.cpp build/host-tests/cJSON.o -o build/host-tests/gfn-test
build/host-tests/gfn-test
${CXX:-c++} -std=c++20 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -Isrc tests/gfn_persistence_test.cpp src/gfn.cpp build/host-tests/cJSON.o -o build/host-tests/gfn-persistence-test
build/host-tests/gfn-persistence-test

${CXX:-c++} -std=c++20 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -Isrc tests/random_test.cpp src/random.cpp -o build/host-tests/random-test
build/host-tests/random-test

python3 tests/socket_test.py

${CC:-cc} -Wall -Wextra -Werror -fsanitize=address,undefined tests/inet_pton_test.c src/platform/inet_pton.c -o build/host-tests/inet-pton-test
build/host-tests/inet-pton-test

${CXX:-c++} -std=c++20 -Wall -Wextra -Werror -fsanitize=address,undefined -Isrc tests/cloud_test.cpp src/cloud.cpp src/gfn.cpp build/host-tests/cJSON.o -o build/host-tests/cloud-test

${CXX:-c++} -std=c++20 -Wall -Wextra -Werror -fsanitize=address,undefined -Isrc tests/catalog_search_test.cpp -o build/host-tests/catalog-search-test
build/host-tests/catalog-search-test
build/host-tests/cloud-test

${CXX:-c++} -std=c++20 -Wall -Wextra -Werror -fsanitize=address,undefined -Isrc tests/nvst_sdp_test.cpp src/stream/nvst_sdp.cpp src/stream/sdp.cpp -o build/host-tests/nvst-sdp-test
build/host-tests/nvst-sdp-test
${CXX:-c++} -std=c++20 -Wall -Wextra -Werror -fsanitize=address,undefined -Isrc/stream tests/websocket_write_queue_test.cpp -o build/host-tests/websocket-queue-test
build/host-tests/websocket-queue-test

${CXX:-c++} -std=c++20 -Wall -Wextra -Werror -fsanitize=address,undefined -Isrc tests/audio_rtp_utils_test.cpp -o build/host-tests/audio-rtp-test
build/host-tests/audio-rtp-test
${CC:-cc} -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -Ivendor/libpeer/src tests/rtp_layout_test.c -o build/host-tests/rtp-layout-test
build/host-tests/rtp-layout-test
for test in rtp_h264_assembly_test rtp_hevc_assembly_test rtp_reorder_test; do
 ${CC:-cc} -std=c11 -D_DEFAULT_SOURCE -DLOG_LEVEL=-1 -fsanitize=address,undefined -Ivendor/libpeer/src "tests/$test.c" vendor/libpeer/src/rtp.c -o "build/host-tests/$test"
 "build/host-tests/$test"
done
${CC:-cc} -std=c11 -D_DEFAULT_SOURCE -DOPENNOW_PS5=1 -DLOG_LEVEL=-1 -fsanitize=address,undefined -Ivendor/libpeer/src tests/rtp_arena_test.c vendor/libpeer/src/rtp.c -o build/host-tests/rtp-arena-test
build/host-tests/rtp-arena-test
if [[ $(uname -s) == Darwin ]]; then
 ${CC:-cc} -std=c11 -D_DARWIN_C_SOURCE -fsanitize=address,undefined tests/app_heap_test.c -o build/host-tests/app-heap-test
 build/host-tests/app-heap-test
fi

${CXX:-c++} -std=c++20 -Wall -Wextra -Werror -fsanitize=address,undefined -Isrc tests/video_recovery_test.cpp -o build/host-tests/video-recovery-test
build/host-tests/video-recovery-test

${CXX:-c++} -std=c++20 -Wall -Wextra -Werror -fsanitize=address,undefined -Isrc tests/hardware_video_contract_test.cpp -o build/host-tests/hardware-video-contract-test
build/host-tests/hardware-video-contract-test
${CXX:-c++} -std=c++20 -Wall -Wextra -Werror -fsanitize=address,undefined -Isrc tests/hardware_decoder_test.cpp src/stream/native/hardware_decoder.cpp -o build/host-tests/hardware-decoder-test
build/host-tests/hardware-decoder-test

${CXX:-c++} -std=c++20 -Wall -Wextra -Werror -fsanitize=address,undefined -Isrc tests/hevc_headers_test.cpp -o build/host-tests/hevc-headers-test
build/host-tests/hevc-headers-test

for test in nvst_qos_test video_capture_test compressed_queue_test surface_fingerprint_test; do
 ${CXX:-c++} -std=c++20 -Wall -Wextra -Werror -fsanitize=address,undefined -Isrc "tests/$test.cpp" -o "build/host-tests/$test"
 "build/host-tests/$test"
done

${CXX:-c++} -std=c++20 -Wall -Wextra -Werror -fsanitize=address,undefined -Isrc tests/remote_input_test.cpp -o build/host-tests/remote-input-test
build/host-tests/remote-input-test
