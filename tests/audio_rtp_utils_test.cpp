#include "stream/AudioRtpUtils.hpp"

#include <cassert>
#include <cstdint>

static_assert(opennow::audio::RtpDeltaToUs(480, 0, 48000) == 10000);
static_assert(opennow::audio::RtpDeltaToUs(0x20u, 0xfffffff0u, 48000) == 1000);
static_assert(opennow::audio::AudioTimelineBase(9600, 1920) == 7680);
static_assert(opennow::audio::RtpTimestampAtNtp(9000, 1000000, 1100000, 90000) == 18000);
static_assert(opennow::audio::RtpTimestampAtNtp(0xfffffff0u, 1000000, 1001000, 48000) == 0x20u);
static_assert(!opennow::audio::ShouldResetEpoch(1000000, 1100000, true, 100, 103, 12));
static_assert(opennow::audio::ShouldResetEpoch(1000000, 1600000, true, 100, 101, 12));

int main() {
    using namespace opennow::audio;

    const uint8_t opus[] = {0x11, 0x22, 0x33};
    auto plain = ParseRedPrimary(opus, sizeof(opus), 111);
    assert(plain.data == opus && plain.size == 3 && !plain.red);

    // One 2-byte redundant block, followed by the primary Opus block.
    const uint8_t red[] = {0x80 | 111, 0x00, 0x00, 0x02, 111, 0xaa, 0xbb, 0x11, 0x22, 0x33};
    auto primary = ParseRedPrimary(red, sizeof(red), 63);
    assert(primary.red && primary.size == 3);
    assert(primary.data[0] == 0x11 && primary.data[2] == 0x33);
    auto redundant = ParseLatestRedundant(red, sizeof(red));
    assert(redundant.size == 2 && redundant.timestamp_offset == 0);
    assert(redundant.data[0] == 0xaa && redundant.data[1] == 0xbb);

    const uint8_t multiple[] = {
        0x80 | 111, 15, 0, 2, 0x80 | 111, 7, 128, 2,
        111, 0xaa, 0xbb, 0xcc, 0xdd, 0x11
    };
    redundant = ParseLatestRedundant(multiple, sizeof(multiple));
    assert(redundant.size == 2 && redundant.timestamp_offset == 480);
    assert(redundant.data[0] == 0xcc && redundant.data[1] == 0xdd);
    for (size_t size = 0; size < sizeof(multiple); ++size)
        assert(!ParseLatestRedundant(multiple, size).data);
    uint8_t wrong_codec[sizeof(multiple)];
    for (size_t i = 0; i < sizeof(multiple); ++i)
        wrong_codec[i] = multiple[i];
    wrong_codec[4] = 0x80 | 110;
    assert(!ParseLatestRedundant(wrong_codec, sizeof(wrong_codec)).data);
    const uint8_t short_frame[] = {0x80 | 111, 3, 192, 1, 111, 0xaa, 0xbb};
    redundant = ParseLatestRedundant(short_frame, sizeof(short_frame));
    assert(redundant.size == 1 && redundant.timestamp_offset == 240 && redundant.data[0] == 0xaa);

    const uint8_t malformed[] = {0x80 | 111, 0x00};
    assert(ParseRedPrimary(malformed, sizeof(malformed), 63).data == nullptr);

    assert(RtpDeltaToUs(480, 0, 48000) == 10000);
    assert(RtpDeltaToUs(9000, 0, 90000) == 100000);
    assert(RtpDeltaToUs(0x00000020u, 0xfffffff0u, 48000) == 1000);
    assert(RtpDeltaToUs(0xfffffff0u, 0x00000020u, 48000) == -1000);
    assert(AudioTimelineBase(9600, 1920) == 7680);
    assert(AudioTimelineBase(100, 480) == static_cast<uint32_t>(-380));
    assert(RtpTimestampAtNtp(9000, 1000000, 1100000, 90000) == 18000);
    assert(RtpTimestampAtNtp(9000, 1000000, 900000, 90000) == 0);
    assert(RtpTimestampAtNtp(0xfffffff0u, 1000000, 1001000, 48000) == 0x20u);
    assert(!ShouldResetEpoch(1000000, 1100000, true, 100, 103, 12));
    assert(ShouldResetEpoch(1000000, 1600000, true, 100, 101, 12));
    assert(ShouldResetEpoch(1000000, 1100000, true, 100, 180, 12));
    assert(!ShouldResetEpoch(1000000, 1100000, true, 65530, 2, 12));
    assert(RecoverySamples(1480,1000)==480);
    assert(RecoverySamples(3880,1000)==2880);
    assert(RecoverySamples(4000,1000)==0);
    assert(RecoverySamples(1001,1000)==0);
    assert(RecoverySamples(999,1000)==0);
    assert(RecoverySamples(0x100u,0xffffff20u)==480);
    assert(!ParseRedPrimary(wrong_codec,sizeof(wrong_codec),63).data);
    const uint8_t wrong_primary[]={110,0x11};
    assert(!ParseRedPrimary(wrong_primary,sizeof(wrong_primary),63).data);
    const uint8_t dynamic_red[]={0x80|112,0x07,0x80,0x02,112,0xaa,0xbb,0x11,0x22};
    const auto dynamic=ParseRedPrimary(dynamic_red,sizeof(dynamic_red),64,112,64);
    assert(dynamic.red&&dynamic.size==2&&dynamic.data[0]==0x11);
    const auto dynamic_recovery=ParseLatestRedundant(dynamic_red,sizeof(dynamic_red),112);
    assert(dynamic_recovery.data&&dynamic_recovery.size==2&&dynamic_recovery.timestamp_offset==480);
    assert(!ParseRedPrimary(dynamic_red,sizeof(dynamic_red),64,111,64).data);
    assert(!ParseLatestRedundant(dynamic_red,sizeof(dynamic_red),111).data);
    return 0;
}
