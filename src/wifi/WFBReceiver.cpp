//
// Created by Talus on 2024/6/10.
//

#include "WFBReceiver.h"

#include <iomanip>
#include <mutex>
#include <set>
#include <sstream>

#include "../gui_interface.h"
#include "Rtp.h"
#include "RxFrame.h"
#include "WFBProcessor.h"
#include "WiFiDriver.h"
#include "logger.h"

#pragma comment(lib, "ws2_32.lib")

std::vector<DeviceId> WFBReceiver::GetDeviceList() {
    std::vector<DeviceId> list;

    // Initialize libusb
    libusb_context *find_ctx;
    libusb_init(&find_ctx);

    // Get list of USB devices
    libusb_device **devs;
    ssize_t count = libusb_get_device_list(find_ctx, &devs);
    if (count < 0) {
        return list;
    }

    // Iterate over devices
    for (ssize_t i = 0; i < count; ++i) {
        libusb_device *dev = devs[i];
        libusb_device_descriptor desc{};
        if (libusb_get_device_descriptor(dev, &desc) == 0) {
            // Check if the device is using libusb driver
            if (desc.bDeviceClass == LIBUSB_CLASS_PER_INTERFACE) {
                uint8_t bus_num = libusb_get_bus_number(dev);
                uint8_t port_num = libusb_get_port_number(dev);

                std::stringstream ss;
                ss << std::setw(4) << std::setfill('0') << std::hex << desc.idVendor << ":";
                ss << std::setw(4) << std::setfill('0') << std::hex << desc.idProduct;
                ss << std::dec << " [" << (int)bus_num << ":" << (int)port_num << "]";

                DeviceId dev_id = {
                    .vendor_id = desc.idVendor,
                    .product_id = desc.idProduct,
                    .display_name = ss.str(),
                    .bus_num = bus_num,
                    .port_num = port_num,
                };

                list.push_back(dev_id);
            }
        }
    }

    // std::sort(list.begin(), list.end(), [](std::string &a, std::string &b) {
    //     static std::vector<std::string> specialStrings = {"0b05:17d2", "0bda:8812", "0bda:881a"};
    //     auto itA = std::find(specialStrings.begin(), specialStrings.end(), a);
    //     auto itB = std::find(specialStrings.begin(), specialStrings.end(), b);
    //     if (itA != specialStrings.end() && itB == specialStrings.end()) {
    //         return true;
    //     }
    //     if (itB != specialStrings.end() && itA == specialStrings.end()) {
    //         return false;
    //     }
    //     return a < b;
    // });

    // Free the list of devices
    libusb_free_device_list(devs, 1);

    // Deinitialize libusb
    libusb_exit(find_ctx);

    return list;
}

bool WFBReceiver::Start(const DeviceId &deviceId, uint8_t channel, int channelWidthMode, const std::string &kPath) {
    GuiInterface::Instance().wifiFrameCount_ = 0;
    GuiInterface::Instance().wfbFrameCount_ = 0;
    GuiInterface::Instance().rtpPktCount_ = 0;
    GuiInterface::Instance().UpdateCount();

    keyPath = kPath;

    if (usbThread) {
        return false;
    }

    auto logger = std::make_shared<Logger>();

    int rc = libusb_init(&ctx);
    if (rc < 0) {
        GuiInterface::Instance().PutLog(LogLevel::Error, "Failed to initialize libusb");
        return false;
    }

    libusb_set_option(ctx, LIBUSB_OPTION_LOG_LEVEL, LIBUSB_LOG_LEVEL_ERROR);

    // Get list of USB devices
    libusb_device **devs;
    ssize_t count = libusb_get_device_list(ctx, &devs);
    if (count < 0) {
        return false;
    }

    libusb_device *target_dev{};

    // Iterate over devices
    for (ssize_t i = 0; i < count; ++i) {
        libusb_device *dev = devs[i];
        libusb_device_descriptor desc{};
        if (libusb_get_device_descriptor(dev, &desc) == 0) {
            // Check if the device is using libusb driver
            if (desc.bDeviceClass == LIBUSB_CLASS_PER_INTERFACE) {
                int bus_num = libusb_get_bus_number(dev);
                int port_num = libusb_get_port_number(dev);

                if (desc.idVendor == deviceId.vendor_id && desc.idProduct == deviceId.product_id &&
                    bus_num == deviceId.bus_num && port_num == deviceId.port_num) {
                    target_dev = dev;
                }
            }
        }
    }

    if (!target_dev) {
        GuiInterface::Instance().PutLog(LogLevel::Error, "Invalid device ID!");
        // Free the list of devices
        libusb_free_device_list(devs, 1);
        return false;
    }

    // This cannot handle multiple devices with same vendor_id and product_id.
    // devHandle = libusb_open_device_with_vid_pid(ctx, wifiDeviceVid, wifiDevicePid);
    libusb_open(target_dev, &devHandle);

    // Free the list of devices
    libusb_free_device_list(devs, 1);

    if (devHandle == nullptr) {
        libusb_exit(ctx);

        GuiInterface::Instance().PutLog(LogLevel::Error,
                                        "Cannot open device {:04x}:{:04x} at [{:}:{:}]",
                                        deviceId.vendor_id,
                                        deviceId.product_id,
                                        deviceId.bus_num,
                                        deviceId.port_num);
        GuiInterface::Instance().ShowTip(FTR("invalid usb msg"));
        return false;
    }

    // Check if the kernel driver attached
    if (libusb_kernel_driver_active(devHandle, 0)) {
        // Detach driver
        rc = libusb_detach_kernel_driver(devHandle, 0);
    }

    rc = libusb_claim_interface(devHandle, 0);
    if (rc < 0) {
        GuiInterface::Instance().PutLog(LogLevel::Error, "Failed to claim interface");
        return false;
    }

    usbThread = std::make_shared<std::thread>([=, this]() {
        WiFiDriver wifi_driver{logger};
        try {
            rtlDevice = wifi_driver.CreateRtlDevice(devHandle);
            rtlDevice->Init(
                [](const Packet &p) {
                    Instance().handle80211Frame(p);
                    GuiInterface::Instance().UpdateCount();
                },
                SelectedChannel{
                    .Channel = channel,
                    .ChannelOffset = 0,
                    .ChannelWidth = static_cast<ChannelWidth_t>(channelWidthMode),
                });
        } catch (const std::runtime_error &e) {
            GuiInterface::Instance().PutLog(LogLevel::Error, e.what());
        } catch (...) {
        }

        auto rc1 = libusb_release_interface(devHandle, 0);
        if (rc1 < 0) {
            GuiInterface::Instance().PutLog(LogLevel::Error, "Failed to release interface");
        }

        GuiInterface::Instance().PutLog(LogLevel::Info, "USB thread stopped");

        libusb_close(devHandle);
        libusb_exit(ctx);

        devHandle = nullptr;
        ctx = nullptr;

        usbThread.reset();

        GuiInterface::Instance().EmitWifiStopped();
    });
    usbThread->detach();

    return true;
}

void WFBReceiver::handle80211Frame(const Packet &packet) {
    GuiInterface::Instance().wifiFrameCount_++;
    GuiInterface::Instance().UpdateCount();

    RxFrame frame(packet.Data);
    if (!frame.IsValidWfbFrame()) {
        return;
    }

    GuiInterface::Instance().wfbFrameCount_++;
    GuiInterface::Instance().UpdateCount();

    static int8_t rssi[4] = {1, 1, 1, 1};
    static uint8_t antenna[4] = {1, 1, 1, 1};

    static uint32_t link_id = 7669206; // sha1 hash of link_domain="default"
    static uint8_t video_radio_port = 0;
    static uint64_t epoch = 0;

    static uint32_t video_channel_id_f = (link_id << 8) + video_radio_port;
    static uint32_t video_channel_id_be = htobe32(video_channel_id_f);

    static auto *video_channel_id_be8 = reinterpret_cast<uint8_t *>(&video_channel_id_be);

    static std::mutex agg_mutex;
    static std::unique_ptr<Aggregator> video_aggregator = std::make_unique<Aggregator>(
        keyPath.c_str(),
        epoch,
        video_channel_id_f,
        [](uint8_t *payload, uint16_t packet_size) { WFBReceiver::Instance().handleRtp(payload, packet_size); });

    std::lock_guard lock(agg_mutex);
    if (frame.MatchesChannelID(video_channel_id_be8)) {
        video_aggregator->process_packet(packet.Data.data() + sizeof(ieee80211_header),
                                         packet.Data.size() - sizeof(ieee80211_header) - 4,
                                         0,
                                         antenna,
                                         rssi);
    }
}

#ifdef __linux__
#define INVALID_SOCKET (-1)
#endif

static unsigned long long sendFd = INVALID_SOCKET;
static volatile bool playing = false;

#define GET_H264_NAL_UNIT_TYPE(buffer_ptr) (buffer_ptr[0] & 0x1F)

inline bool isH264(const uint8_t *data) {
    auto h264NalType = GET_H264_NAL_UNIT_TYPE(data);
    return h264NalType == 24 || h264NalType == 28;
}

void WFBReceiver::handleRtp(uint8_t *payload, uint16_t packet_size) {
    GuiInterface::Instance().rtpPktCount_++;
    GuiInterface::Instance().UpdateCount();

    if (rtlDevice->should_stop) {
        return;
    }
    if (packet_size < 12) {
        return;
    }

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(GuiInterface::Instance().playerPort);
    serverAddr.sin_addr.s_addr = inet_addr("127.0.0.1");

    auto *header = (RtpHeader *)payload;

    if (!playing) {
        playing = true;
        if (GuiInterface::Instance().playerCodec == "AUTO") {
            // Check H264 or h265
            if (isH264(header->getPayloadData())) {
                GuiInterface::Instance().playerCodec = "H264";
            } else {
                GuiInterface::Instance().playerCodec = "H265";
            }
            GuiInterface::Instance().PutLog(LogLevel::Debug, "Check codec " + GuiInterface::Instance().playerCodec);
        }
        GuiInterface::Instance().NotifyRtpStream(header->pt, ntohl(header->ssrc));
    }

    // Send video to player.
    sendto(sendFd,
           reinterpret_cast<const char *>(payload),
           packet_size,
           0,
           (sockaddr *)&serverAddr,
           sizeof(serverAddr));
}

void WFBReceiver::Stop() const {
    playing = false;
    if (rtlDevice) {
        rtlDevice->should_stop = true;
    }
}

WFBReceiver::WFBReceiver() {
#ifdef __WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        GuiInterface::Instance().PutLog(LogLevel::Error, "WSAStartup failed");
        return;
    }
#endif

    sendFd = socket(AF_INET, SOCK_DGRAM, 0);
}

WFBReceiver::~WFBReceiver() {
#ifdef __WIN32
    closesocket(sendFd);
    sendFd = INVALID_SOCKET;
    WSACleanup();
#endif

    Stop();
}
