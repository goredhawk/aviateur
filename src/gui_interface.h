#pragma once

#include <filesystem>
#include <fstream>
#include <future>
#include <nlohmann/json.hpp>

#include "util/mini.h"
#include "wifi/WFBReceiver.h"

#define CONFIG "config."
#define CONFIG_FILE "config.ini"
#define CONFIG_DEVICE CONFIG "pidVid"
#define CONFIG_CHANNEL CONFIG "channel"
#define CONFIG_CHANNEL_WIDTH_MODE CONFIG "channelWidth"
#define CONFIG_CHANNEL_KEY CONFIG "key"
#define CONFIG_CHANNEL_CODEC CONFIG "codec"

/// Channels.
constexpr std::array CHANNELS{
    1,  2,  3,   4,   5,   6,   7,   8,   9,   10,  11,  12,  13,  32,  36,  40,  44,  48,  52,  56,  60,  64,
    68, 96, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144, 149, 153, 157, 161, 165, 169, 173, 177,
};

/// Channel widths.
constexpr std::array CHANNEL_WIDTHS{
    "20",
    "40",
    "80",
    "160",
    "80_80",
    "5",
    "10",
    "MAX",
};

enum class LogLevel {
    Info,
    Debug,
    Warn,
    Error,
};

class GuiInterface {
public:
    static GuiInterface &Instance() {
        static GuiInterface interface;
        return interface;
    }

    explicit GuiInterface() {
        // Load config
        try {
            toolkit::mINI::Instance().parseFile(CONFIG_FILE);
        } catch (...) {
            config_file_exists = false;
        }
    }

    // Get config.
    static nlohmann::json GetConfig() {
        nlohmann::json config;
        for (const auto &item : toolkit::mINI::Instance()) {
            config[std::string(item.first)] = std::string(item.second.c_str());
        }
        return config;
    }

    static std::vector<std::string> GetDongleList() {
        return WFBReceiver::Instance().GetDongleList();
    }

    static bool Start(const std::string &vidPid,
                      int channel,
                      int channelWidthMode,
                      const std::string &keyPath,
                      const std::string &codec) {
        // Save config.
        toolkit::mINI::Instance()[CONFIG_DEVICE] = vidPid;
        toolkit::mINI::Instance()[CONFIG_CHANNEL] = channel;
        toolkit::mINI::Instance()[CONFIG_CHANNEL_WIDTH_MODE] = channelWidthMode;
        toolkit::mINI::Instance()[CONFIG_CHANNEL_KEY] = keyPath;
        toolkit::mINI::Instance()[CONFIG_CHANNEL_CODEC] = codec;
        toolkit::mINI::Instance().dumpFile(CONFIG_FILE);

        // Set port.
        Instance().playerPort = GetFreePort();

        Instance().playerCodec = codec;

        return WFBReceiver::Instance().Start(vidPid, channel, channelWidthMode, keyPath);
    }

    static bool Stop() {
        WFBReceiver::Instance().Stop();
        return true;
    }

    static void BuildSdp(const std::string &filePath, const std::string &codec, int payloadType, int port) {
        auto absolutePath = std::filesystem::absolute(filePath);
        std::string dirPath = absolutePath.parent_path().string();

        try {
            if (!std::filesystem::exists(dirPath)) {
                std::filesystem::create_directories(dirPath);
            }
        } catch (const std::exception &e) {
            std::cerr << e.what() << std::endl;
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
            LogLevel::Debug,
            "Build SDP: Codec:" + codec + " PT:" + std::to_string(payloadType) + " Port:" + std::to_string(port));
    }

    template <typename... Args>
    void PutLog(LogLevel level, format_string_t<Args...> fmt, Args &&...args) {
        std::string txt = format(fmt, args...);
        WhenLog(level, txt);
    }

    void NotifyWifiStop() {
        WhenWifiStopped();
    }

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

    long long GetWfbFrameCount() const {
        return wfbFrameCount_;
    }
    long long GetRtpPktCount() const {
        return rtpPktCount_;
    }
    long long GetWifiFrameCount() const {
        return wifiFrameCount_;
    }

    int GetPlayerPort() const {
        return playerPort;
    }
    std::string GetPlayerCodec() const {
        return playerCodec;
    }
    static int GetFreePort() {
        return 52356;
    }

    long long wfbFrameCount_ = 0;
    long long wifiFrameCount_ = 0;
    long long rtpPktCount_ = 0;
    int playerPort = 0;
    std::string playerCodec;

    bool config_file_exists = true;

    // Signals.
    std::vector<toolkit::AnyCallable<void>> logCallbacks;
    std::vector<toolkit::AnyCallable<void>> wifiStopCallbacks;
    std::vector<toolkit::AnyCallable<void>> wifiFrameCountCallbacks;
    std::vector<toolkit::AnyCallable<void>> wfbFrameCountCallbacks;
    std::vector<toolkit::AnyCallable<void>> rtpPktCountCallbacks;
    std::vector<toolkit::AnyCallable<void>> rtpStreamCallbacks;
    std::vector<toolkit::AnyCallable<void>> bitrateUpdateCallbacks;

    void WhenLog(LogLevel level, std::string msg) {
        for (auto &callback : logCallbacks) {
            try {
                callback.operator()<LogLevel, std::string>(std::move(level), std::move(msg));
            } catch (std::bad_any_cast &) {
            }
        }
    }

    void WhenWifiStopped() {
        for (auto &callback : wifiStopCallbacks) {
            try {
                callback();
            } catch (std::bad_any_cast &) {
                Instance().PutLog(LogLevel::Error, "Mismatched signal argument types!");
            }
        }
    }

    void WhenWifiFrameCountUpdated(long long count) {
        for (auto &callback : wifiFrameCountCallbacks) {
            try {
                callback.operator()<long long>(std::move(count));
            } catch (std::bad_any_cast &) {
                Instance().PutLog(LogLevel::Error, "Mismatched signal argument types!");
            }
        }
    }

    void WhenWfbFrameCountUpdated(long long count) {
        for (auto &callback : wfbFrameCountCallbacks) {
            try {
                callback.operator()<long long>(std::move(count));
            } catch (std::bad_any_cast &) {
                Instance().PutLog(LogLevel::Error, "Mismatched signal argument types!");
            }
        }
    }

    void WhenRtpPktCountUpdated(long long count) {
        for (auto &callback : rtpPktCountCallbacks) {
            try {
                callback.operator()<long long>(std::move(count));
            } catch (std::bad_any_cast &) {
                Instance().PutLog(LogLevel::Error, "Mismatched signal argument types!");
            }
        }
    }

    void WhenRtpStream(std::string sdp) {
        for (auto &callback : rtpStreamCallbacks) {
            try {
                callback.operator()<std::string>(std::move(sdp));
            } catch (std::bad_any_cast &) {
                Instance().PutLog(LogLevel::Error, "Mismatched signal argument types!");
            }
        }
    }

    void WhenBitrateUpdate(uint64_t bitrate) {
        for (auto &callback : bitrateUpdateCallbacks) {
            try {
                callback.operator()<uint64_t>(std::move(bitrate));
            } catch (std::bad_any_cast &) {
                Instance().PutLog(LogLevel::Error, "Mismatched signal argument types!");
            }
        }
    }
};
