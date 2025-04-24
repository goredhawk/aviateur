#pragma once

#include <common/any_callable.h>
#include <mini/ini.h>
#include <servers/translation_server.h>

#include <filesystem>
#include <fstream>
#include <future>
#include <nlohmann/json.hpp>

#ifdef __linux__
    #include <pwd.h>
    #include <unistd.h>
#endif

#include "app.h"
#include "wifi/WfbReceiver.h"

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

#define DEFAULT_PORT 52356

constexpr auto LOGGER_MODULE = "Aviateur";

const revector::ColorU GREEN = revector::ColorU(78, 135, 82);
const revector::ColorU RED = revector::ColorU(201, 79, 79);
const revector::ColorU YELLOW = revector::ColorU(255, 201, 14);

/// Channels.
constexpr std::array CHANNELS{
    1,  2,  3,   4,   5,   6,   7,   8,   9,   10,  11,  12,  13,  32,  36,  40,  44,  48,  52,  56,  60,  64,
    68, 96, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144, 149, 153, 157, 161, 165, 169, 173, 177,
};

/// Channel widths.
constexpr std::array CHANNEL_WIDTHS{
    "20",
    "40",
};

enum class LogLevel {
    Info,
    Debug,
    Warn,
    Error,
};

struct RxStatus {
    uint8_t rssi[2];
    int8_t snr[2];
};

class GuiInterface {
public:
    static GuiInterface &Instance() {
        static GuiInterface interface;
        return interface;
    }

    explicit GuiInterface() {
#ifdef _WIN32
        ShowWindow(GetConsoleWindow(), SW_HIDE); // SW_RESTORE to bring back

        // Windows crash dump
        SetUnhandledExceptionFilter(UnhandledExceptionFilter);
#endif

        // Redirect standard output to a file
        // DO NOT USE WHEN DEPLOYING, AS IT WILL CRASH THE APP ON USER PCs.
        // freopen((GetAppDataDir() + std::string("last_run_log.txt")).c_str(), "w", stdout);

        // Set up loggers
        {
            // revector::Logger::set_default_level(revector::Logger::Level::Info);
            revector::Logger::set_module_level("revector", revector::Logger::Level::Info);
            revector::Logger::set_module_level(LOGGER_MODULE, revector::Logger::Level::Info);

            auto logCallback = [](LogLevel level, std::string msg) {
                switch (level) {
                    case LogLevel::Info: {
                        revector::Logger::info(msg, LOGGER_MODULE);
                    } break;
                    case LogLevel::Debug: {
                        revector::Logger::debug(msg, LOGGER_MODULE);
                    } break;
                    case LogLevel::Warn: {
                        revector::Logger::warn(msg, LOGGER_MODULE);
                    } break;
                    case LogLevel::Error: {
                        revector::Logger::error(msg, LOGGER_MODULE);
                    } break;
                    default:;
                }
            };
            logCallbacks.emplace_back(logCallback);
        }

        auto dir = GetAppDataDir();

        // Load config.
        mINI::INIFile file(dir + CONFIG_FILE);
        bool readSuccess = file.read(ini_);

        if (!readSuccess) {
            ini_[CONFIG_ADAPTER][ADAPTER_DEVICE] = "";
            ini_[CONFIG_ADAPTER][ADAPTER_CHANNEL] = "161";
            ini_[CONFIG_ADAPTER][ADAPTER_CHANNEL_WIDTH_MODE] = "0";
            ini_[CONFIG_ADAPTER][ADAPTER_CHANNEL_KEY] = "";
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

    static std::vector<DeviceId> GetDeviceList() {
        return WfbReceiver::GetDeviceList();
    }

    static std::string GetAppDataDir() {
#ifdef _WIN32
        auto dir = std::string(getenv("APPDATA")) + "\\Aviateur\\";
#else ifdef __linux__
        passwd *pw = getpwuid(getuid());
        const char *home_dir = pw->pw_dir;
        auto dir = std::string(home_dir) + "/aviateur/";
#endif
        return dir;
    }

    static std::string GetCaptureDir() {
#ifdef _WIN32
        auto dir = std::string(getenv("USERPROFILE")) + "\\Videos\\Aviateur Captures\\";
#else
        passwd *pw = getpwuid(getuid());
        const char *home_dir = pw->pw_dir;
        auto dir = std::string(home_dir) + "/Pictures/Aviateur Captures/";
#endif
        return dir;
    }

    static bool SaveConfig() {
        // For clearing obsolete entries.
        // Instance().ini_.clear();

        Instance().ini_[CONFIG_GUI][CONFIG_GUI_LANG] = Instance().locale_;

        auto dir = GetAppDataDir();

        try {
            if (!std::filesystem::exists(dir)) {
                std::filesystem::create_directories(dir);
            }
        } catch (const std::exception &e) {
            std::cerr << e.what() << std::endl;
        }

        mINI::INIFile file(dir + std::string(CONFIG_FILE));
        bool writeSuccess = file.write(Instance().ini_, true);

        return writeSuccess;
    }

    static bool Start(const DeviceId &deviceId,
                      int channel,
                      int channelWidthMode,
                      std::string keyPath,
                      const std::string &codec) {
        Instance().ini_[CONFIG_ADAPTER][ADAPTER_DEVICE] = deviceId.display_name;
        Instance().ini_[CONFIG_ADAPTER][ADAPTER_CHANNEL] = std::to_string(channel);
        Instance().ini_[CONFIG_ADAPTER][ADAPTER_CHANNEL_WIDTH_MODE] = std::to_string(channelWidthMode);
        Instance().ini_[CONFIG_ADAPTER][ADAPTER_CHANNEL_KEY] = keyPath;
        Instance().ini_[CONFIG_ADAPTER][ADAPTER_CHANNEL_CODEC] = codec;

        // Set port.
        Instance().playerPort = GetFreePort(DEFAULT_PORT);
        Instance().PutLog(LogLevel::Info, "Using port: {}", Instance().playerPort);

        Instance().playerCodec = codec;

        // If no custom key provided by the user, use the default key.
        if (keyPath.empty()) {
            keyPath = revector::get_asset_dir("gs.key");
            Instance().PutLog(LogLevel::Info, "Using GS key: {}", keyPath);
        }
        return WfbReceiver::Instance().Start(deviceId, channel, channelWidthMode, keyPath);
    }

    static bool Stop() {
        WfbReceiver::Instance().Stop();
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
        std::string sdpFile = "sdp/sdp" + std::to_string(playerPort) + ".sdp";

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

    static int GetFreePort(int start_port) {
#ifdef _WIN32
        // Declare some variables
        WSADATA wsaData;

        int free_port = 0;

        int iResult = 0; // used to return function results

        // the listening socket to be created
        SOCKET soc = INVALID_SOCKET;

        //----------------------
        // Initialize Winsock
        iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (iResult != NO_ERROR) {
            wprintf(L"Error at WSAStartup()\n");
            return 0;
        }

        // Create a SOCKET for listening for incoming connection requests
        soc = socket(AF_INET, SOCK_DGRAM, 0);
        if (soc == INVALID_SOCKET) {
            wprintf(L"socket function failed with error: %u\n", WSAGetLastError());
            WSACleanup();
            return 0;
        }

        for (int port = start_port; port < start_port + 200; ++port) {
            // The sockaddr_in structure specifies the address family,
            // IP address, and port for the socket that is being bound.
            sockaddr_in sin;
            sin.sin_family = AF_INET;
            sin.sin_addr.s_addr = inet_addr("0.0.0.0");
            sin.sin_port = htons(port);

            // Bind the socket.
            iResult = bind(soc, (sockaddr *)&sin, sizeof(sin));
            if (iResult == SOCKET_ERROR) {
                Instance().PutLog(LogLevel::Info, "bind failed with error {}", WSAGetLastError());
            } else {
                free_port = port;
                break;
            }
        }

        closesocket(soc);
        WSACleanup();

        return free_port;
#else
        return start_port;
#endif
    }

    void set_locale(std::string locale) {
        locale_ = locale;
        revector::TranslationServer::get_singleton()->set_locale(locale_);
    }

    mINI::INIStructure ini_;

    std::string locale_ = "en";

    long long wfbFrameCount_ = 0;
    long long wifiFrameCount_ = 0;
    long long rtpPktCount_ = 0;
    int playerPort = 0;
    std::string playerCodec;

    bool config_file_exists = true;

    RxStatus rx_status_{};

    // Signals.
    std::vector<revector::AnyCallable<void>> logCallbacks;
    std::vector<revector::AnyCallable<void>> tipCallbacks;
    std::vector<revector::AnyCallable<void>> wifiStopCallbacks;
    std::vector<revector::AnyCallable<void>> wifiFrameCountCallbacks;
    std::vector<revector::AnyCallable<void>> wfbFrameCountCallbacks;
    std::vector<revector::AnyCallable<void>> rtpPktCountCallbacks;
    std::vector<revector::AnyCallable<void>> rtpStreamCallbacks;
    std::vector<revector::AnyCallable<void>> bitrateUpdateCallbacks;
    std::vector<revector::AnyCallable<void>> decoderReadyCallbacks;

    std::vector<revector::AnyCallable<void>> urlStreamShouldStopCallbacks;

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
