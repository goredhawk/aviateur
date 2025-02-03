#pragma once

#include <common/any_callable.h>
#include <mini/ini.h>
#include <servers/translation_server.h>

#include <filesystem>
#include <fstream>
#include <future>
#include <nlohmann/json.hpp>

#include "wifi/WFBReceiver.h"

#define CONFIG_FILE "config.ini"

#define CONFIG_ADAPTER "adapter"
#define ADAPTER_DEVICE "pid_vid"
#define ADAPTER_CHANNEL "channel"
#define ADAPTER_CHANNEL_WIDTH_MODE "channel_width_mode"
#define ADAPTER_CHANNEL_KEY "key"
#define ADAPTER_CHANNEL_CODEC "codec"

#define CONFIG_STREAMING "streaming"
#define CONFIG_STREAMING_URL "url"

#define CONFIG_GUI "gui"
#define CONFIG_GUI_LANG "language"

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
        // Load config.
        mINI::INIFile file(CONFIG_FILE);
        bool readSuccess = file.read(ini_);

        if (!readSuccess) {
            ini_[CONFIG_ADAPTER][ADAPTER_DEVICE] = "";
            ini_[CONFIG_ADAPTER][ADAPTER_CHANNEL] = "161";
            ini_[CONFIG_ADAPTER][ADAPTER_CHANNEL_WIDTH_MODE] = "0";
            ini_[CONFIG_ADAPTER][ADAPTER_CHANNEL_KEY] = "gs.key";
            ini_[CONFIG_ADAPTER][ADAPTER_CHANNEL_CODEC] = "AUTO";

            ini_[CONFIG_STREAMING][CONFIG_STREAMING_URL] = "udp://239.0.0.1:1234";

            ini_[CONFIG_GUI][CONFIG_GUI_LANG] = "en";
        } else {
            set_locale(ini_[CONFIG_GUI][CONFIG_GUI_LANG]);
        }
    }

    ~GuiInterface() {
        SaveConfig();
    }

    static std::vector<std::string> GetDongleList() {
        return WFBReceiver::Instance().GetDongleList();
    }

    static bool SaveConfig() {
        // For clearing obsolete entries.
        // Instance().ini_.clear();

        Instance().ini_[CONFIG_GUI][CONFIG_GUI_LANG] = Instance().locale_;

        mINI::INIFile file(CONFIG_FILE);
        bool writeSuccess = file.write(Instance().ini_, true);

        return writeSuccess;
    }

    static bool Start(const std::string &vidPid,
                      int channel,
                      int channelWidthMode,
                      const std::string &keyPath,
                      const std::string &codec) {
        Instance().ini_[CONFIG_ADAPTER][ADAPTER_DEVICE] = vidPid;
        Instance().ini_[CONFIG_ADAPTER][ADAPTER_CHANNEL] = std::to_string(channel);
        Instance().ini_[CONFIG_ADAPTER][ADAPTER_CHANNEL_WIDTH_MODE] = std::to_string(channelWidthMode);
        Instance().ini_[CONFIG_ADAPTER][ADAPTER_CHANNEL_KEY] = keyPath;
        Instance().ini_[CONFIG_ADAPTER][ADAPTER_CHANNEL_CODEC] = codec;

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
    void PutLog(LogLevel level, const std::string_view message, Args... format_items) {
        std::string str = std::vformat(message, std::make_format_args(format_items...));
        EmitLog(level, str);
    }

    int NotifyRtpStream(int pt, uint16_t ssrc) {
        // Get free port.
        std::string sdpFile = "sdp/sdp.sdp";
        BuildSdp(sdpFile, playerCodec, pt, playerPort);

        EmitRtpStream(sdpFile);

        return Instance().playerPort;
    }

    void UpdateCount() {
        EmitWifiFrameCountUpdated(wifiFrameCount_);
        EmitWfbFrameCountUpdated(wfbFrameCount_);
        EmitRtpPktCountUpdated(rtpPktCount_);
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

    void set_locale(std::string locale) {
        locale_ = locale;
        Flint::TranslationServer::get_singleton()->set_locale(locale_);
    }

    mINI::INIStructure ini_;

    std::string locale_ = "en";

    long long wfbFrameCount_ = 0;
    long long wifiFrameCount_ = 0;
    long long rtpPktCount_ = 0;
    int playerPort = 0;
    std::string playerCodec;

    bool config_file_exists = true;

    // Signals.
    std::vector<Flint::AnyCallable<void>> logCallbacks;
    std::vector<Flint::AnyCallable<void>> tipCallbacks;
    std::vector<Flint::AnyCallable<void>> wifiStopCallbacks;
    std::vector<Flint::AnyCallable<void>> wifiFrameCountCallbacks;
    std::vector<Flint::AnyCallable<void>> wfbFrameCountCallbacks;
    std::vector<Flint::AnyCallable<void>> rtpPktCountCallbacks;
    std::vector<Flint::AnyCallable<void>> rtpStreamCallbacks;
    std::vector<Flint::AnyCallable<void>> bitrateUpdateCallbacks;
    std::vector<Flint::AnyCallable<void>> decoderReadyCallbacks;

    std::vector<Flint::AnyCallable<void>> urlStreamShouldStopCallbacks;

    void EmitLog(LogLevel level, std::string msg) {
        for (auto &callback : logCallbacks) {
            try {
                callback.operator()<LogLevel, std::string>(std::move(level), std::move(msg));
            } catch (std::bad_any_cast &) {
            }
        }
    }

    void ShowTip(std::string msg) {
        for (auto &callback : tipCallbacks) {
            try {
                callback.operator()<std::string>(std::move(msg));
            } catch (std::bad_any_cast &) {
            }
        }
    }

    void EmitWifiStopped() {
        for (auto &callback : wifiStopCallbacks) {
            try {
                callback();
            } catch (std::bad_any_cast &) {
                Instance().PutLog(LogLevel::Error, "Mismatched signal argument types!");
            }
        }
    }

    void EmitWifiFrameCountUpdated(long long count) {
        for (auto &callback : wifiFrameCountCallbacks) {
            try {
                callback.operator()<long long>(std::move(count));
            } catch (std::bad_any_cast &) {
                Instance().PutLog(LogLevel::Error, "Mismatched signal argument types!");
            }
        }
    }

    void EmitWfbFrameCountUpdated(long long count) {
        for (auto &callback : wfbFrameCountCallbacks) {
            try {
                callback.operator()<long long>(std::move(count));
            } catch (std::bad_any_cast &) {
                Instance().PutLog(LogLevel::Error, "Mismatched signal argument types!");
            }
        }
    }

    void EmitRtpPktCountUpdated(long long count) {
        for (auto &callback : rtpPktCountCallbacks) {
            try {
                callback.operator()<long long>(std::move(count));
            } catch (std::bad_any_cast &) {
                Instance().PutLog(LogLevel::Error, "Mismatched signal argument types!");
            }
        }
    }

    void EmitRtpStream(std::string sdp) {
        for (auto &callback : rtpStreamCallbacks) {
            try {
                callback.operator()<std::string>(std::move(sdp));
            } catch (std::bad_any_cast &) {
                Instance().PutLog(LogLevel::Error, "Mismatched signal argument types!");
            }
        }
    }

    void EmitBitrateUpdate(uint64_t bitrate) {
        for (auto &callback : bitrateUpdateCallbacks) {
            try {
                callback.operator()<uint64_t>(std::move(bitrate));
            } catch (std::bad_any_cast &) {
                Instance().PutLog(LogLevel::Error, "Mismatched signal argument types!");
            }
        }
    }

    void EmitDecoderReady(uint32_t width, uint32_t height, float videoFps) {
        for (auto &callback : decoderReadyCallbacks) {
            try {
                callback.operator()<uint32_t, uint32_t, float>(std::move(width),
                                                               std::move(height),
                                                               std::move(videoFps));
            } catch (std::bad_any_cast &) {
                Instance().PutLog(LogLevel::Error, "Mismatched signal argument types!");
            }
        }
    }

    void EmitUrlStreamShouldStop() {
        for (auto &callback : urlStreamShouldStopCallbacks) {
            try {
                callback.operator()<>();
            } catch (std::bad_any_cast &) {
                Instance().PutLog(LogLevel::Error, "Mismatched signal argument types!");
            }
        }
    }
};
