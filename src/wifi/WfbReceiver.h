//
// Created by Talus on 2024/6/10.
//

#pragma once

#ifdef _WIN32
    #include <libusb.h>
#else
    #include <libusb-1.0/libusb.h>
#endif
#include <string>
#include <thread>
#include <vector>

#include "FrameParser.h"
#include "Rtl8812aDevice.h"

struct DeviceId {
    uint16_t vendor_id;
    uint16_t product_id;
    std::string display_name;
    uint8_t bus_num;
    uint8_t port_num;
};

class WfbReceiver {
public:
    WfbReceiver();
    ~WfbReceiver();
    static WfbReceiver &Instance() {
        static WfbReceiver wfb_receiver;
        return wfb_receiver;
    }

    static std::vector<DeviceId> GetDeviceList();
    bool Start(const DeviceId &deviceId, uint8_t channel, int channelWidth, const std::string &keyPath);
    void Stop() const;
    void handle80211Frame(const Packet &pkt);
    void handleRtp(uint8_t *payload, uint16_t packet_size);

protected:
    libusb_context *ctx{};
    libusb_device_handle *devHandle{};
    std::shared_ptr<std::thread> usbThread;
    std::unique_ptr<Rtl8812aDevice> rtlDevice;
    std::string keyPath;
};
