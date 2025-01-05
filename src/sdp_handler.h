#ifndef SDP_HANDLER_H
#define SDP_HANDLER_H

#include "wifi/WFBReceiver.h"
#include <filesystem>
#include <fstream>
#include <future>
#include <nlohmann/json.hpp>
#include <util/mini.h>

using namespace toolkit;

#define CONFIG "config."
#define CONFIG_FILE "config.ini"
#define CONFIG_CHANNEL CONFIG "channel"
#define CONFIG_CHANNEL_WIDTH CONFIG "channelWidth"
#define CONFIG_CHANNEL_KEY CONFIG "key"
#define CONFIG_CHANNEL_CODEC CONFIG "codec"

class SdpHandler {
public:
    static SdpHandler &Instance() {
        static SdpHandler handler;
        return handler;
    }

    explicit SdpHandler() {
        // Load config
        try {
            mINI::Instance().parseFile(CONFIG_FILE);
        } catch (...) {
        }
    }

    // Get config
    nlohmann::json GetConfig() {
        nlohmann::json config;
        for (const auto &item : mINI::Instance()) {
            config[std::string(item.first.c_str())] = std::string(item.second.c_str());
        }
        return config;
    }

    static std::vector<std::string> GetDongleList() { return WFBReceiver::Instance().GetDongleList(); }

    static bool Start(
        const std::string &vidPid, int channel, int channelWidth, const std::string &keyPath,
        const std::string &codec) {

        // Save config.
        mINI::Instance()[CONFIG_CHANNEL] = channel;
        mINI::Instance()[CONFIG_CHANNEL_WIDTH] = channelWidth;
        mINI::Instance()[CONFIG_CHANNEL_KEY] = keyPath;
        mINI::Instance()[CONFIG_CHANNEL_CODEC] = codec;
        mINI::Instance().dumpFile(CONFIG_FILE);

        // Allocate port
        Instance().playerPort = Instance().GetFreePort();
        Instance().playerCodec = codec;
        return WFBReceiver::Instance().Start(vidPid, channel, channelWidth, keyPath);
    }

    static bool Stop() {
        std::future f = std::async([]() { WFBReceiver::Instance().Stop(); });
        return true;
    }

    static void BuildSdp(const std::string &filePath, const std::string &codec, int payloadType, int port) {
        std::string dirPath = std::filesystem::absolute(filePath).string();

        try {
            if (std::filesystem::create_directory(dirPath))
                std::cout << "Created a directory\n";
            else
                std::cerr << "Failed to create a directory\n";
        } catch (const std::exception &e) {
            std::cerr << e.what() << '\n';
        }

        std::ofstream sdpFos(filePath);
        sdpFos << "v=0\n";
        sdpFos << "o=- 0 0 IN IP4 127.0.0.1\n";
        sdpFos << "s=No Name\n";
        sdpFos << "c=IN IP4 127.0.0.1\n";
        sdpFos << "t=0 0\n";
        sdpFos << "m=video " << port << " RTP/AVP " << payloadType << "\n";
        sdpFos << "a=rtpmap:" << payloadType << " " << codec << "/90000\n";
        sdpFos.flush();
        sdpFos.close();

        Instance().PutLog(
            "debug",
            "Build Player SDP: Codec:" + codec + " PT:" + std::to_string(payloadType)
                + " Port:" + std::to_string(port));
    }

    void PutLog(const std::string &level, const std::string &msg) {
        // emit onLog(std::string(level.c_str()), std::string(msg.c_str()));
    }

    void NotifyWifiStop() {
        // emit onWifiStop();
    }

    int NotifyRtpStream(int pt, uint16_t ssrc) {
        // get free port
        const std::string sdpFile = "sdp/sdp.sdp";
        BuildSdp(sdpFile, playerCodec, pt, playerPort);
        // emit onRtpStream(sdpFile);
        return Instance().playerPort;
    }

    void UpdateCount() {
        // emit onWifiFrameCount(wifiFrameCount_);
        // emit onWfbFrameCount(wfbFrameCount_);
        // emit onRtpPktCount(rtpPktCount_);
    }

    long long wfbFrameCount() { return wfbFrameCount_; }
    long long rtpPktCount() { return rtpPktCount_; }
    long long wifiFrameCount() { return wifiFrameCount_; }
    int GetPlayerPort() { return playerPort; }
    std::string GetPlayerCodec() const { return playerCodec; }
    int GetFreePort() { return 52356; }
    long long wfbFrameCount_ = 0;
    long long wifiFrameCount_ = 0;
    long long rtpPktCount_ = 0;
    int playerPort = 0;
    std::string playerCodec;

    // signals:
    void onLog(std::string level, std::string msg);
    void onWifiStop();
    void onWifiFrameCount(long long count);
    void onWfbFrameCount(long long count);
    void onRtpPktCount(long long count);
    void onRtpStream(std::string sdp);
};

#endif // SDP_HANDLER_H
