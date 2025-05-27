#include "WfbngLink.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <iomanip>
#include <iostream>
#include <list>
#include <map>
#include <mutex>
#include <random>
#include <span>
#include <sstream>
#include <string>
#include <thread>

#include "RxFrame.h"
#include "SignalQualityCalculator.h"
#include "TxFrame.h"
#include "libusb.h"
#include "wfb-ng/wifibroadcast.hpp"

#undef TAG
#define TAG "pixelpilot"

#define CRASH()     \
    do {            \
        int *i = 0; \
        *i = 42;    \
    } while (0)

std::string generate_random_string(size_t length) {
    const std::string characters = "abcdefghijklmnopqrstuvwxyz";
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> distrib(0, characters.size() - 1);

    std::string result;
    result.reserve(length);
    for (size_t i = 0; i < length; ++i) {
        result += characters[distrib(gen)];
    }
    return result;
}

WfbngLink::WfbngLink() : current_fd(-1), adaptive_link_enabled(true), adaptive_tx_power(30) {
    initAgg();
    Logger_t log;
    wifi_driver = std::make_unique<WiFiDriver>(log);
}

void WfbngLink::initAgg() {
    std::string client_addr = "127.0.0.1";
    uint64_t epoch = 0;

    uint8_t video_radio_port = 0;
    uint32_t video_channel_id_f = (link_id << 8) + video_radio_port;
    video_channel_id_be = htobe32(video_channel_id_f);
    auto udsName = std::string("my_socket");

    video_aggregator = std::make_unique<AggregatorUDPv4>(client_addr, 5600, keyPath, epoch, video_channel_id_f, 0);

    int mavlink_client_port = 14550;
    uint8_t mavlink_radio_port = 0x10;
    uint32_t mavlink_channel_id_f = (link_id << 8) + mavlink_radio_port;
    mavlink_channel_id_be = htobe32(mavlink_channel_id_f);

    mavlink_aggregator =
        std::make_unique<AggregatorUDPv4>(client_addr, mavlink_client_port, keyPath, epoch, mavlink_channel_id_f, 0);

    int udp_client_port = 8000;
    uint8_t udp_radio_port = wfb_rx_port;
    uint32_t udp_channel_id_f = (link_id << 8) + udp_radio_port;
    udp_channel_id_be = htobe32(udp_channel_id_f);

    udp_aggregator =
        std::make_unique<AggregatorUDPv4>(client_addr, udp_client_port, keyPath, epoch, udp_channel_id_f, 0);
}

int WfbngLink::run(int wifiChannel, int bw, int fd) {
    int r;
    libusb_context *ctx = NULL;
    txFrame = std::make_shared<TxFrame>();

    r = libusb_set_option(NULL, LIBUSB_OPTION_NO_DEVICE_DISCOVERY);
    r = libusb_init(&ctx);
    if (r < 0) {
        // printf("Failed to init libusb.");
        return r;
    }

    // Open adapters
    struct libusb_device_handle *dev_handle;
    r = libusb_wrap_sys_device(ctx, (intptr_t)fd, &dev_handle);
    if (r < 0) {
        libusb_exit(ctx);
        return r;
    }

    if (libusb_kernel_driver_active(dev_handle, 0)) {
        r = libusb_detach_kernel_driver(dev_handle, 0);
        printf("libusb_detach_kernel_driver: %d", r);
    }
    r = libusb_claim_interface(dev_handle, 0);
    printf("Creating driver and device for fd=%d", fd);

    rtl_devices.emplace(fd, wifi_driver->CreateRtlDevice(dev_handle));
    if (!rtl_devices.at(fd)) {
        printf("CreateRtlDevice error");
        return -1;
    }

    uint8_t *video_channel_id_be8 = reinterpret_cast<uint8_t *>(&video_channel_id_be);
    uint8_t *udp_channel_id_be8 = reinterpret_cast<uint8_t *>(&udp_channel_id_be);
    uint8_t *mavlink_channel_id_be8 = reinterpret_cast<uint8_t *>(&mavlink_channel_id_be);

    try {
        auto packetProcessor =
            [this, video_channel_id_be8, mavlink_channel_id_be8, udp_channel_id_be8](const Packet &packet) {
                RxFrame frame(packet.Data);
                if (!frame.IsValidWfbFrame()) {
                    return;
                }
                int8_t rssi[4] = {(int8_t)packet.RxAtrib.rssi[0], (int8_t)packet.RxAtrib.rssi[1], 1, 1};
                uint32_t freq = 0;
                int8_t noise[4] = {1, 1, 1, 1};
                uint8_t antenna[4] = {1, 1, 1, 1};

                std::lock_guard<std::mutex> lock(agg_mutex);
                if (frame.MatchesChannelID(video_channel_id_be8)) {
                    SignalQualityCalculator::get_instance().add_rssi(packet.RxAtrib.rssi[0], packet.RxAtrib.rssi[1]);
                    SignalQualityCalculator::get_instance().add_snr(packet.RxAtrib.snr[0], packet.RxAtrib.snr[1]);

                    video_aggregator->process_packet(packet.Data.data() + sizeof(ieee80211_header),
                                                     packet.Data.size() - sizeof(ieee80211_header) - 4,
                                                     0,
                                                     antenna,
                                                     rssi,
                                                     noise,
                                                     freq,
                                                     0,
                                                     0,
                                                     NULL);
                    if (should_clear_stats) {
                        video_aggregator->clear_stats();
                        should_clear_stats = false;
                    }
                } else if (frame.MatchesChannelID(mavlink_channel_id_be8)) {
                    mavlink_aggregator->process_packet(packet.Data.data() + sizeof(ieee80211_header),
                                                       packet.Data.size() - sizeof(ieee80211_header) - 4,
                                                       0,
                                                       antenna,
                                                       rssi,
                                                       noise,
                                                       freq,
                                                       0,
                                                       0,
                                                       NULL);
                } else if (frame.MatchesChannelID(udp_channel_id_be8)) {
                    udp_aggregator->process_packet(packet.Data.data() + sizeof(ieee80211_header),
                                                   packet.Data.size() - sizeof(ieee80211_header) - 4,
                                                   0,
                                                   antenna,
                                                   rssi,
                                                   noise,
                                                   freq,
                                                   0,
                                                   0,
                                                   NULL);
                }
            };

        // Store the current fd for later TX power updates.
        current_fd = fd;

        if (!usb_event_thread) {
            auto usb_event_thread_func = [ctx, this, fd] {
                while (true) {
                    auto dev = this->rtl_devices.at(fd).get();
                    if (dev == nullptr || dev->should_stop) break;
                    struct timeval timeout = {0, 500000}; // 500ms timeout
                    int r = libusb_handle_events_timeout(ctx, &timeout);
                    if (r < 0) {
                        this->log->error("Error handling events: {}", r);
                        // break;
                    }
                }
            };

            init_thread(usb_event_thread, [=]() { return std::make_unique<std::thread>(usb_event_thread_func); });

            std::shared_ptr<TxArgs> args = std::make_shared<TxArgs>();
            args->udp_port = 8001;
            args->link_id = link_id;
            args->keypair = keyPath;
            args->stbc = true;
            args->ldpc = true;
            args->mcs_index = 0;
            args->vht_mode = false;
            args->short_gi = false;
            args->bandwidth = 20;
            args->k = 1;
            args->n = 5;
            args->radio_port = wfb_tx_port;

            printf("radio link ID %d, radio PORT %d", args->link_id, args->radio_port);

            Rtl8812aDevice *current_device = rtl_devices.at(fd).get();
            if (!usb_tx_thread) {
                init_thread(usb_tx_thread, [&]() {
                    return std::make_unique<std::thread>([this, current_device, args] {
                        txFrame->run(current_device, args.get());
                        printf("usb_transfer thread should terminate");
                    });
                });
            }

            if (adaptive_link_enabled) {
                stop_adaptive_link();
                start_link_quality_thread(fd);
            }
        }

        auto bandWidth = (bw == 20 ? CHANNEL_WIDTH_20 : CHANNEL_WIDTH_40);
        rtl_devices.at(fd)->Init(packetProcessor,
                                 SelectedChannel{
                                     .Channel = static_cast<uint8_t>(wifiChannel),
                                     .ChannelOffset = 0,
                                     .ChannelWidth = bandWidth,
                                 });
    } catch (const std::runtime_error &error) {
        printf("runtime_error: %s", error.what());
        auto dev = rtl_devices.at(fd).get();
        if (dev) {
            dev->should_stop = true;
        }
        txFrame->stop();

        destroy_thread(usb_tx_thread);
        destroy_thread(usb_event_thread);
        stop_adaptive_link();
        return -1;
    }

    printf("Init done, releasing...");
    auto dev = rtl_devices.at(fd).get();
    if (dev) {
        dev->should_stop = true;
    }
    txFrame->stop();

    destroy_thread(usb_tx_thread);
    destroy_thread(usb_event_thread);
    stop_adaptive_link();

    r = libusb_release_interface(dev_handle, 0);
    printf("libusb_release_interface: %d", r);
    libusb_exit(ctx);
    return 0;
}

void WfbngLink::stop(int fd) {
    if (rtl_devices.find(fd) == rtl_devices.end()) {
        printf("rtl_devices.find(%d) == rtl_devices.end()", fd);
        CRASH();
        return;
    }
    auto dev = rtl_devices.at(fd).get();
    if (dev) {
        dev->should_stop = true;
    } else {
        printf("rtl_devices.at(%d) is nullptr", fd);
    }
    stop_adaptive_link();
}

// Modified start_link_quality_thread: use adaptive_link_enabled and adaptive_tx_power
void WfbngLink::start_link_quality_thread(int fd) {
    auto thread_func = [this, fd]() {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        const char *ip = "10.5.0.10";
        int port = 9999;
        int sockfd;
        struct sockaddr_in server_addr;
        // Create UDP socket
        if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
            printf("Socket creation failed");
            return;
        }
        int opt = 1;
        setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port);
        if (inet_pton(AF_INET, ip, &server_addr.sin_addr) <= 0) {
            printf("Invalid IP address");
            close(sockfd);
            return;
        }
        int sockfd2;
        struct sockaddr_in server_addr2;
        // Create UDP socket
        if ((sockfd2 = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
            printf("Socket creation failed");
            return;
        }
        int opt2 = 1;
        setsockopt(sockfd2, SOL_SOCKET, SO_REUSEADDR, &opt2, sizeof(opt2));
        memset(&server_addr2, 0, sizeof(server_addr2));
        server_addr2.sin_family = AF_INET;
        server_addr2.sin_port = htons(7755);
        if (inet_pton(AF_INET, ip, &server_addr2.sin_addr) <= 0) {
            printf("Invalid IP address");
            close(sockfd);
            return;
        }
        while (!this->adaptive_link_should_stop) {
            auto quality = SignalQualityCalculator::get_instance().calculate_signal_quality();
#if defined(ANDROID_DEBUG_RSSI) || true
            // __android_log_print(ANDROID_LOG_WARN, TAG, "quality %d", quality.quality);
#endif
            time_t currentEpoch = time(nullptr);
            const auto map_range =
                [](double value, double inputMin, double inputMax, double outputMin, double outputMax) {
                    return outputMin + ((value - inputMin) * (outputMax - outputMin) / (inputMax - inputMin));
                };
            // map to 1000..2000
            quality.quality = map_range(quality.quality, -1024, 1024, 1000, 2000);
            {
                uint32_t len;
                char message[100];

                /**
                     1741491090:1602:1602:1:0:-70:24:num_ants:pnlt:fec_change:code

                     <gs_time>:<link_score>:<link_score>:<fec>:<lost>:<rssi_dB>:<snr_dB>:<num_ants>:<noise_penalty>:<fec_change>:<idr_request_code>

                    gs_time: gs clock
                    link_score: 1000 - 2000 sent twice (already including any penalty)
                    link_score: 1000 - 2000 sent twice (already including any penalty)
                    fec: instantaneus fec_rec (only used by old fec_rec_pntly now disabled by default)
                    lost: instantaneus lost (not used)
                    rssi_dB:  best antenna rssi (for osd)
                    snr_dB: best antenna snr_dB (for osd)
                    num_ants: number of gs antennas (for osd)
                    noise_penalty: penalty deducted from score due to noise (for osd)
                    fec_change: int from 0 to 5 : how much to alter fec based on noise
                    optional idr_request_code:  4 char unique code to request 1 keyframe (no need to send special extra
                   packets)
                 */

                if (quality.lost_last_second > 2)
                    fec.bump(5);
                else {
                    if (quality.recovered_last_second > 30) fec.bump(5);
                    if (quality.recovered_last_second > 24) fec.bump(3);
                    if (quality.recovered_last_second > 22) fec.bump(2);
                    if (quality.recovered_last_second > 18) fec.bump(1);
                    if (quality.recovered_last_second < 18) fec.bump(0);
                }

                snprintf(message + sizeof(len),
                         sizeof(message) - sizeof(len),
                         "%ld:%d:%d:%d:%d:%d:%f:0:-1:%d:%s\n",
                         static_cast<long>(currentEpoch),
                         quality.quality,
                         quality.quality,
                         quality.recovered_last_second,
                         quality.lost_last_second,
                         quality.quality,
                         quality.snr,
                         fec.value(),
                         quality.idr_code.c_str());
                len = strlen(message + sizeof(len));
                len = htonl(len);
                memcpy(message, &len, sizeof(len));
                printf(" message %s", message + 4);
                ssize_t sent = sendto(sockfd,
                                      message,
                                      strlen(message + sizeof(len)) + sizeof(len),
                                      0,
                                      (struct sockaddr *)&server_addr,
                                      sizeof(server_addr));
                if (sent < 0) {
                    printf("Failed to send message");
                    break;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        close(sockfd);
        this->adaptive_link_should_stop = false;
    };

    init_thread(link_quality_thread, [=]() { return std::make_unique<std::thread>(thread_func); });
    rtl_devices.at(fd)->SetTxPower(adaptive_tx_power);
}

void WfbNgLink_SetAdaptiveLinkEnabled(WfbngLink *link, bool enabled) {
    bool wasEnabled = link->adaptive_link_enabled;
    link->adaptive_link_enabled = enabled;
    // If we are enabling adaptive mode (and it was previously disabled)
    if (enabled && !wasEnabled) {
        link->stop_adaptive_link();
        if (link->current_fd != -1) {
            // If a previous adaptive thread exists, join it first.
            // Restart the adaptive (link quality) thread.
            link->start_link_quality_thread(link->current_fd);
        }
    }
    // When disabling, wait for the thread to exit (if running).
    if (!enabled && wasEnabled) {
        link->stop_adaptive_link();
    }
}

void WfbNgLink_SetTxPower(WfbngLink *link, int power) {
    if (link->adaptive_tx_power == power) return;

    link->adaptive_tx_power = power;
    if (link->current_fd != -1 && link->rtl_devices.find(link->current_fd) != link->rtl_devices.end()) {
        link->rtl_devices.at(link->current_fd)->SetTxPower(power);
    }
    // If adaptive mode is enabled and the adaptive thread is not running, restart it.
    if (link->adaptive_link_enabled) {
        link->stop_adaptive_link();
        if (link->current_fd != -1) {
            link->start_link_quality_thread(link->current_fd);
        }
    }
}

void WfbNgLink_SetUseFec(WfbngLink *link, int use) {
    link->fec.setEnabled(use);
}
