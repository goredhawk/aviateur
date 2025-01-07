#ifndef SDP_HANDLER_H
#define SDP_HANDLER_H

#include "app.h"
#include "wifi/WFBReceiver.h"
#include <common/any_callable.h>
#include <filesystem>
#include <fstream>
#include <future>
#include <nlohmann/json.hpp>
#include <util/mini.h>

using namespace toolkit;

#define CONFIG "config."
#define CONFIG_FILE "config.ini"
#define CONFIG_DEVICE CONFIG "0000:0000"
#define CONFIG_CHANNEL CONFIG "channel"
#define CONFIG_CHANNEL_WIDTH_MODE CONFIG "channelWidth"
#define CONFIG_CHANNEL_KEY CONFIG "key"
#define CONFIG_CHANNEL_CODEC CONFIG "codec"

class GuiInterface {
public:
    static GuiInterface &Instance() {
        static GuiInterface interface;
        return interface;
    }

    explicit GuiInterface() {
        // Load config
        try {
            mINI::Instance().parseFile(CONFIG_FILE);
        } catch (...) {
            config_file_exists = false;
        }
    }

    // Get config.
    static nlohmann::json GetConfig() {
        nlohmann::json config;
        for (const auto &item : mINI::Instance()) {
            config[std::string(item.first)] = std::string(item.second.c_str());
        }
        return config;
    }

    static std::vector<std::string> GetDongleList() { return WFBReceiver::Instance().GetDongleList(); }

    static bool Start(
        const std::string &vidPid, int channel, int channelWidthMode, const std::string &keyPath,
        const std::string &codec) {

        // Save config.
        mINI::Instance()[CONFIG_DEVICE] = vidPid;
        mINI::Instance()[CONFIG_CHANNEL] = channel;
        mINI::Instance()[CONFIG_CHANNEL_WIDTH_MODE] = channelWidthMode;
        mINI::Instance()[CONFIG_CHANNEL_KEY] = keyPath;
        mINI::Instance()[CONFIG_CHANNEL_CODEC] = codec;
        mINI::Instance().dumpFile(CONFIG_FILE);

        // Allocate port.
        Instance().playerPort = Instance().GetFreePort();
        Instance().playerCodec = codec;

        return WFBReceiver::Instance().Start(vidPid, channel, channelWidthMode, keyPath);
    }

    static bool Stop() {
        std::future f = std::async([]() { WFBReceiver::Instance().Stop(); });
        return true;
    }

    static void BuildSdp(const std::string &filePath, const std::string &codec, int payloadType, int port) {
        std::string dirPath = std::filesystem::absolute(filePath).string();

        try {
            if (!std::filesystem::exists(dirPath)) {
                std::filesystem::create_directory(dirPath);
            }
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

    void PutLog(const std::string &level, const std::string &msg) { WhenLog(level, msg); }

    void NotifyWifiStop() { WhenWifiStopped(); }

    int NotifyRtpStream(int pt, uint16_t ssrc) {
        // Get free port.
        std::string sdpFile = "sdp/sdp.sdp";
        BuildSdp(sdpFile, playerCodec, pt, playerPort);
        // Emit signal.
        WhenRtpStream(sdpFile);
        return Instance().playerPort;
    }

    void UpdateCount() {
        WhenWifiFrameCountUpdated(wifiFrameCount_);
        WhenWfbFrameCountUpdated(wfbFrameCount_);
        WhenRtpPktCountUpdated(rtpPktCount_);
    }

    long long GetWfbFrameCount() const { return wfbFrameCount_; }
    long long GetRtpPktCount() const { return rtpPktCount_; }
    long long GetWifiFrameCount() const { return wifiFrameCount_; }
    int GetPlayerPort() const { return playerPort; }
    std::string GetPlayerCodec() const { return playerCodec; }
    static int GetFreePort() { return 52356; }

    long long wfbFrameCount_ = 0;
    long long wifiFrameCount_ = 0;
    long long rtpPktCount_ = 0;
    int playerPort = 0;
    std::string playerCodec;

    bool config_file_exists = true;

    // Signals.
    std::vector<Flint::AnyCallable<void>> onLog;
    std::vector<Flint::AnyCallable<void>> onWifiStop;
    std::vector<Flint::AnyCallable<void>> onWifiFrameCount;
    std::vector<Flint::AnyCallable<void>> onWfbFrameCount;
    std::vector<Flint::AnyCallable<void>> onRtpPktCount;
    std::vector<Flint::AnyCallable<void>> onRtpStream;

    void WhenLog(std::string level, std::string msg) {
        for (auto &callback : onLog) {
            try {
                callback.operator()<std::string, std::string>(std::move(level), std::move(msg));
            } catch (std::bad_any_cast &) {
                Flint::Logger::error("Mismatched signal argument types!");
            }
        }
    }

    void WhenWifiStopped() {
        for (auto &callback : onWifiStop) {
            try {
                callback();
            } catch (std::bad_any_cast &) {
                Flint::Logger::error("Mismatched signal argument types!");
            }
        }
    }

    void WhenWifiFrameCountUpdated(long long count) {
        for (auto &callback : onWifiFrameCount) {
            try {
                callback.operator()<long long>(std::move(count));
            } catch (std::bad_any_cast &) {
                Flint::Logger::error("Mismatched signal argument types!");
            }
        }
    }

    void WhenWfbFrameCountUpdated(long long count) {
        for (auto &callback : onWfbFrameCount) {
            try {
                callback.operator()<long long>(std::move(count));
            } catch (std::bad_any_cast &) {
                Flint::Logger::error("Mismatched signal argument types!");
            }
        }
    }

    void WhenRtpPktCountUpdated(long long count) {
        for (auto &callback : onRtpPktCount) {
            try {
                callback.operator()<long long>(std::move(count));
            } catch (std::bad_any_cast &) {
                Flint::Logger::error("Mismatched signal argument types!");
            }
        }
    }

    void WhenRtpStream(std::string sdp) {
        for (auto &callback : onRtpStream) {
            try {
                callback.operator()<std::string>(std::move(sdp));
            } catch (std::bad_any_cast &) {
                Flint::Logger::error("Mismatched signal argument types!");
            }
        }
    }
};

#endif // SDP_HANDLER_H
