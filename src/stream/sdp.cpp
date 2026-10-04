// Protocol helpers adapted from OpenNOW-Switch, MIT.
#include "sdp.hpp"
#include "audio_format.hpp"
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <cctype>
#include <set>
#include <vector>
namespace opennow::sdp {
std::string ReplaceAll(std::string value, const std::string& from, const std::string& to)
{
    size_t pos = 0;
    while ((pos = value.find(from, pos)) != std::string::npos) {
        value.replace(pos, from.size(), to);
        pos += to.size();
    }
    return value;
}

std::vector<std::string> SplitSdpLines(const std::string& sdp)
{
    std::vector<std::string> lines;
    size_t start = 0;

    while (start < sdp.size()) {
        size_t end = sdp.find("\r\n", start);
        if (end == std::string::npos)
            end = sdp.find('\n', start);

        if (end == std::string::npos) {
            lines.push_back(sdp.substr(start));
            break;
        }

        lines.push_back(sdp.substr(start, end - start));
        start = end + (sdp[end] == '\r' && end + 1 < sdp.size() && sdp[end + 1] == '\n' ? 2 : 1);
    }

    return lines;
}

std::string JoinSdpLines(const std::vector<std::string>& lines)
{
    std::string result;
    for (const auto& line : lines) {
        result += line;
        result += "\r\n";
    }
    return result;
}

bool StartsWithString(const std::string& value, const std::string& prefix)
{
    return value.rfind(prefix, 0) == 0;
}


int ParseIntegerAttribute(const std::string& sdp, const std::string& attribute, int fallback)
{
    const std::string prefix = "a=" + attribute + ":";
    for (const auto& line : SplitSdpLines(sdp)) {
        if (!StartsWithString(line, prefix))
            continue;

        const std::string raw = line.substr(prefix.size());
        char* end = nullptr;
        long value = 0;
        if (raw.rfind("0x", 0) == 0 || raw.rfind("0X", 0) == 0)
            value = std::strtol(raw.c_str() + 2, &end, 16);
        else
            value = std::strtol(raw.c_str(), &end, 10);

        if (end != raw.c_str())
            return static_cast<int>(value);
    }
    return fallback;
}

opennow::webrtc::RiInputCapabilities ParseRiInputCapabilities(const std::string& offer_sdp)
{
    opennow::webrtc::RiInputCapabilities caps;
    const int threshold = ParseIntegerAttribute(offer_sdp, "ri.partialReliableThresholdMs", caps.partial_reliable_threshold_ms);
    if (threshold > 0)
        caps.partial_reliable_threshold_ms = std::max(1, std::min(5000, threshold));
    caps.hid_device_mask = static_cast<uint32_t>(ParseIntegerAttribute(
        offer_sdp, "ri.hidDeviceMask", static_cast<int>(caps.hid_device_mask)));
    caps.partial_reliable_gamepad_mask = static_cast<uint32_t>(ParseIntegerAttribute(
        offer_sdp, "ri.enablePartiallyReliableTransferGamepad", static_cast<int>(caps.partial_reliable_gamepad_mask)));
    caps.partial_reliable_hid_mask = static_cast<uint32_t>(ParseIntegerAttribute(
        offer_sdp, "ri.enablePartiallyReliableTransferHid", static_cast<int>(caps.partial_reliable_hid_mask)));
    return caps;
}

int ExtractRtpmapPayloadType(const std::string& line, const char* codec)
{
    constexpr const char* prefix = "a=rtpmap:";
    if (!StartsWithString(line, prefix))
        return 0;

    const size_t pt_start = std::strlen(prefix);
    const size_t space = line.find(' ', pt_start);
    if (space == std::string::npos)
        return 0;

    const std::string codec_prefix = std::string(codec) + "/";
    const std::string codec_value = line.substr(space + 1);
    if (!StartsWithString(codec_value, codec_prefix))
        return 0;

    char* end = nullptr;
    long pt = std::strtol(line.c_str() + pt_start, &end, 10);
    if (pt <= 0 || pt > 127)
        return 0;

    return static_cast<int>(pt);
}

std::string FindFmtpForPayload(const std::vector<std::string>& lines, int payload_type)
{
    const std::string prefix = "a=fmtp:" + std::to_string(payload_type) + " ";
    for (const auto& line : lines) {
        if (StartsWithString(line, prefix))
            return line;
    }
    return "";
}

std::vector<std::string> FindRtcpFbForPayload(const std::vector<std::string>& lines, int payload_type)
{
    std::vector<std::string> feedback;
    const std::string prefix = "a=rtcp-fb:" + std::to_string(payload_type) + " ";
    for (const auto& line : lines) {
        if (line == prefix + "nack" || line == prefix + "nack pli")
            feedback.push_back(line);
    }
    return feedback;
}

int SelectOfferH264PayloadType(const std::string& offer_sdp)
{
    const std::vector<std::string> lines = SplitSdpLines(offer_sdp);
    std::vector<int> h264_payloads;
    bool in_video = false;

    for (const auto& line : lines) {
        if (StartsWithString(line, "m="))
            in_video = StartsWithString(line, "m=video");

        if (!in_video)
            continue;

        int pt = ExtractRtpmapPayloadType(line, "H264");
        if (pt > 0)
            h264_payloads.push_back(pt);
    }

    for (int pt : h264_payloads) {
        const std::string fmtp = FindFmtpForPayload(lines, pt);
        if (fmtp.find("packetization-mode=1") != std::string::npos)
            return pt;
    }

    return h264_payloads.empty() ? 96 : h264_payloads.front();
}

int SelectOfferHevcPayloadType(const std::string& offer_sdp,bool hdr)
{
    const auto lines=SplitSdpLines(offer_sdp);bool video=false;
    for(const auto& line:lines){
        if(StartsWithString(line,"m="))video=StartsWithString(line,"m=video");
        if(!video)continue;
        int pt=ExtractRtpmapPayloadType(line,"H265");
        if(!pt)pt=ExtractRtpmapPayloadType(line,"HEVC");
        if(!pt)continue;
        const auto fmtp=FindFmtpForPayload(lines,pt);
        const auto don=fmtp.find("sprop-max-don-diff=");
        if(don!=std::string::npos&&std::strtol(fmtp.c_str()+don+19,nullptr,10)!=0)continue;
        const auto profile=fmtp.find("profile-id=");
        if(hdr&&profile!=std::string::npos&&std::strtol(fmtp.c_str()+profile+11,nullptr,10)!=2)continue;
        return pt;
    }
    return 0;
}

int ExtractOfferMediaPort(const std::string& offer_sdp, const char* media_name)
{
    const std::string prefix = std::string("m=") + media_name + " ";
    for (const auto& line : SplitSdpLines(offer_sdp)) {
        if (!StartsWithString(line, prefix))
            continue;

        const size_t port_start = prefix.size();
        const size_t port_end = line.find(' ', port_start);
        const std::string port_text = line.substr(
            port_start,
            port_end == std::string::npos ? std::string::npos : port_end - port_start);

        char* end = nullptr;
        const long port = std::strtol(port_text.c_str(), &end, 10);
        if (end != port_text.c_str() && port > 0 && port <= 65535)
            return static_cast<int>(port);
    }

    return 0;
}

int CountSdpLinesWithPrefix(const std::string& sdp, const std::string& prefix)
{
    int count = 0;
    for (const auto& line : SplitSdpLines(sdp)) {
        if (StartsWithString(line, prefix))
            ++count;
    }
    return count;
}


std::string MediaKindFromMLine(const std::string& line)
{
    if (StartsWithString(line, "m=audio "))
        return "audio";
    if (StartsWithString(line, "m=video "))
        return "video";
    if (StartsWithString(line, "m=application "))
        return "application";
    return "";
}

std::vector<std::string> ExtractOfferMediaOrder(const std::string& offer_sdp)
{
    std::vector<std::string> order;
    for (const auto& line : SplitSdpLines(offer_sdp)) {
        const std::string media = MediaKindFromMLine(line);
        if (!media.empty())
            order.push_back(media);
    }
    return order;
}

std::string ExtractOfferMid(const std::string& offer_sdp, const std::string& media)
{
    bool in_media = false;
    for (const auto& line : SplitSdpLines(offer_sdp)) {
        if (StartsWithString(line, "m="))
            in_media = MediaKindFromMLine(line) == media;
        if (in_media && StartsWithString(line, "a=mid:"))
            return line.substr(std::strlen("a=mid:"));
    }
    return "";
}

std::string ExtractOfferBundleGroup(const std::string& offer_sdp)
{
    for (const auto& line : SplitSdpLines(offer_sdp)) {
        if (StartsWithString(line, "a=group:BUNDLE "))
            return line;
    }
    return "";
}

struct SdpMediaSection {
    std::string media;
    std::vector<std::string> lines;
};

std::string AlignAnswerSdpToOffer(const std::string& answer_sdp, const std::string& offer_sdp)
{
    std::vector<std::string> session_lines;
    std::vector<SdpMediaSection> sections;
    std::vector<std::string> candidates;
    std::set<std::string> seen_candidates;

    for (const auto& line : SplitSdpLines(answer_sdp)) {
        if (StartsWithString(line, "m=")) {
            sections.push_back({MediaKindFromMLine(line), {line}});
            continue;
        }

        if (StartsWithString(line, "a=candidate:")) {
            if (seen_candidates.insert(line).second)
                candidates.push_back(line);
            continue;
        }

        if (sections.empty())
            session_lines.push_back(line);
        else
            sections.back().lines.push_back(line);
    }

    const std::string offer_bundle = ExtractOfferBundleGroup(offer_sdp);
    bool bundle_replaced = false;
    for (auto& line : session_lines) {
        if (!offer_bundle.empty() && StartsWithString(line, "a=group:BUNDLE ")) {
            line = offer_bundle;
            bundle_replaced = true;
        }
    }
    if (!offer_bundle.empty() && !bundle_replaced)
        session_lines.push_back(offer_bundle);

    for (auto& section : sections) {
        const std::string offer_mid = ExtractOfferMid(offer_sdp, section.media);
        if (offer_mid.empty())
            continue;

        bool mid_replaced = false;
        for (auto& line : section.lines) {
            if (StartsWithString(line, "a=mid:")) {
                line = "a=mid:" + offer_mid;
                mid_replaced = true;
                break;
            }
        }
        if (!mid_replaced && section.lines.size() > 1)
            section.lines.insert(section.lines.begin() + 1, "a=mid:" + offer_mid);
    }

    std::vector<SdpMediaSection> ordered;
    std::vector<bool> used(sections.size(), false);
    for (const auto& media : ExtractOfferMediaOrder(offer_sdp)) {
        for (size_t i = 0; i < sections.size(); ++i) {
            if (!used[i] && sections[i].media == media) {
                ordered.push_back(sections[i]);
                used[i] = true;
                break;
            }
        }
    }
    for (size_t i = 0; i < sections.size(); ++i) {
        if (!used[i])
            ordered.push_back(sections[i]);
    }

    if (!ordered.empty() && !candidates.empty()) {
        auto& first_section = ordered.front().lines;
        first_section.insert(first_section.end(), candidates.begin(), candidates.end());
    }

    std::vector<std::string> out = session_lines;
    for (const auto& section : ordered)
        out.insert(out.end(), section.lines.begin(), section.lines.end());
    return JoinSdpLines(out);
}

std::string AdaptAnswerSdpToOffer(
    const std::string& answer_sdp,
    const std::string& offer_sdp,
    const opennow::StreamSettings& settings)
{
    const bool hevc=settings.codec==VideoCodec::hevc;
    const int h264_payload_type = hevc?SelectOfferHevcPayloadType(offer_sdp,settings.hdr):SelectOfferH264PayloadType(offer_sdp);
    if(!h264_payload_type)return {};
    const std::vector<std::string> offer_lines = SplitSdpLines(offer_sdp);
    const std::string offer_h264_fmtp = FindFmtpForPayload(offer_lines, h264_payload_type);
    const std::vector<std::string> offer_h264_feedback = FindRtcpFbForPayload(offer_lines, h264_payload_type);
    const auto audioFormat=audio::selectFormat(offer_sdp,settings.audio_channels);
    if(!audioFormat.channels)return {};
    std::vector<std::string> lines = SplitSdpLines(answer_sdp);
    std::vector<std::string> out;
    bool in_video = false;
    bool in_audio = false;
    bool video_bitrate_added = false;
    bool video_feedback_added = false;
    bool audio_red_added = false;

    const std::string payload = std::to_string(h264_payload_type);

    for (const auto& line : lines) {
        if (StartsWithString(line, "m=")) {
            in_video = StartsWithString(line, "m=video");
            in_audio = StartsWithString(line, "m=audio");
            if (in_video) {
                out.push_back("m=video 9 UDP/TLS/RTP/SAVPF " + payload);
                continue;
            } else if (in_audio) {
                out.push_back("m=audio 9 UDP/TLS/RTP/SAVPF "+std::to_string(audioFormat.payload)+
                    (audioFormat.redPayload<0?"":" "+std::to_string(audioFormat.redPayload)));
                continue;
            } else if (StartsWithString(line, "m=application")) {
                out.push_back("m=application 9 UDP/DTLS/SCTP webrtc-datachannel");
                continue;
            }
        }

        if(in_audio&&(StartsWithString(line,"a=rtpmap:")||StartsWithString(line,"a=fmtp:"))) {
            if(!audio_red_added) {
                out.push_back(audioFormat.rtpmap);
                out.push_back("a=fmtp:"+std::to_string(audioFormat.payload)+" "+audioFormat.fmtp);
                if(audioFormat.redPayload>=0) {
                    out.push_back(audioFormat.redRtpmap);out.push_back(audioFormat.redFmtp);
                }
                audio_red_added=true;
            }
            continue;
        }
        if(in_audio&&StartsWithString(line,"a=rtcp-fb:"))continue;

        if (in_video && StartsWithString(line, "c=IN ")) {
            out.push_back(line);
            out.push_back("b=AS:" + std::to_string(settings.bitrate_kbps));
            video_bitrate_added = true;
            continue;
        }

        if (in_video && StartsWithString(line, "a=rtcp-fb:96")) {
            if (!video_feedback_added) {
                if (!offer_h264_feedback.empty()) {
                    for (const auto& feedback : offer_h264_feedback)
                        out.push_back(feedback);
                } else {
                    out.push_back("a=rtcp-fb:" + payload + " nack");
                    out.push_back("a=rtcp-fb:" + payload + " nack pli");
                }
                video_feedback_added = true;
            }
            continue;
        }

        if (in_video && StartsWithString(line, "a=fmtp:96")) {
            if (!offer_h264_fmtp.empty())
                out.push_back(offer_h264_fmtp);
            else
                out.push_back("a=fmtp:" + payload + (hevc?" profile-id=2;level-id=156;sprop-max-don-diff=0":" level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42e01f"));
            continue;
        }

        if (in_video && StartsWithString(line, "a=rtpmap:96")) {
            out.push_back("a=rtpmap:" + payload + (hevc?" H265/90000":" H264/90000"));
            continue;
        }

        if (in_video && StartsWithString(line, "a=ssrc:"))
            continue;

        if (in_video && StartsWithString(line, "a=sendrecv")) {
            out.push_back("a=recvonly");
            continue;
        }

        out.push_back(line);
    }

    if (!video_bitrate_added) {
        for (size_t i = 0; i < out.size(); ++i) {
            if (StartsWithString(out[i], "m=video")) {
                out.insert(out.begin() + static_cast<long>(i + 1), "b=AS:" + std::to_string(settings.bitrate_kbps));
                break;
            }
        }
    }

    return AlignAnswerSdpToOffer(JoinSdpLines(out), offer_sdp);
}

std::string ExtractSdpValue(const std::string& sdp, const std::string& prefix)
{
    for (const auto& line : SplitSdpLines(sdp)) {
        if (StartsWithString(line, prefix))
            return line.substr(prefix.size());
    }
    return "";
}

int ExtractNvstIntValue(const std::string& nvst_sdp, const std::string& prefix)
{
    for (const auto& line : SplitSdpLines(nvst_sdp)) {
        if (!StartsWithString(line, prefix))
            continue;

        const std::string value = line.substr(prefix.size());
        char* end = nullptr;
        const long parsed = std::strtol(value.c_str(), &end, 10);
        if (end != value.c_str() && parsed > 0 && parsed <= 65535)
            return static_cast<int>(parsed);
    }
    return 0;
}

std::string ExtractSignalingHost(const std::string& url)
{
    size_t start = url.find("://");
    start = start == std::string::npos ? 0 : start + 3;

    size_t end = url.find('/', start);
    std::string host_port = url.substr(start, end == std::string::npos ? std::string::npos : end - start);

    const size_t at = host_port.rfind('@');
    if (at != std::string::npos)
        host_port.erase(0, at + 1);

    if (!host_port.empty() && host_port.front() == '[') {
        const size_t close = host_port.find(']');
        return close == std::string::npos ? host_port : host_port.substr(1, close - 1);
    }

    const size_t colon = host_port.find(':');
    return colon == std::string::npos ? host_port : host_port.substr(0, colon);
}

std::string DottedIpv4FromGfnHost(const std::string& host)
{
    int octets[4] = {-1, -1, -1, -1};
    size_t pos = 0;

    for (int i = 0; i < 4; ++i) {
        if (pos >= host.size() || !std::isdigit(static_cast<unsigned char>(host[pos])))
            return "";

        int value = 0;
        while (pos < host.size() && std::isdigit(static_cast<unsigned char>(host[pos]))) {
            value = value * 10 + (host[pos] - '0');
            ++pos;
        }

        if (value < 0 || value > 255)
            return "";

        octets[i] = value;
        if (i < 3) {
            if (pos >= host.size() || host[pos] != '-')
                return "";
            ++pos;
        }
    }

    return std::to_string(octets[0]) + "." + std::to_string(octets[1]) + "." +
           std::to_string(octets[2]) + "." + std::to_string(octets[3]);
}

std::string NormalizeGfnMediaIp(const std::string& host)
{
    std::string media_ip = DottedIpv4FromGfnHost(host);
    return media_ip.empty() ? host : media_ip;
}

std::string PrepareGfnOfferSdp(
    std::string sdp,
    const std::string& signaling_url,
    const std::string& media_ip_hint,
    [[maybe_unused]] int media_port_hint)
{
    std::string media_ip = NormalizeGfnMediaIp(media_ip_hint);

    if (media_ip.empty()) {
        const std::string host = ExtractSignalingHost(signaling_url);
        media_ip = NormalizeGfnMediaIp(host);
    }

    if (!media_ip.empty())
        sdp = ReplaceAll(std::move(sdp), "0.0.0.0", media_ip);

    return sdp;
}

std::string BuildManualMediaCandidate(
    const std::string& signaling_url,
    const std::string& media_ip_hint,
    int media_port_hint,
    int foundation)
{
    std::string media_ip = NormalizeGfnMediaIp(media_ip_hint);
    if (media_ip.empty()) {
        const std::string host = ExtractSignalingHost(signaling_url);
        media_ip = NormalizeGfnMediaIp(host);
    }

    if (media_ip.empty() || media_port_hint <= 0)
        return "";

    return "a=candidate:" + std::to_string(foundation) + " 1 UDP 2130706431 " + media_ip + " " +
        std::to_string(media_port_hint) + " typ host";
}



}
