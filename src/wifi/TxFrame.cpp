#ifdef __linux__

    #include "TxFrame.h"

    #include <linux/ip.h>
    #include <linux/udp.h>

    #include "pcap/pcap.h"

struct ieee80211_hdr {
    uint16_t /*__le16*/ frame_control;
    uint16_t /*__le16*/ duration_id;
    uint8_t addr1[6];
    uint8_t addr2[6];
    uint8_t addr3[6];
    uint16_t /*__le16*/ seq_ctrl;
    // uint8_t addr4[6];
} __attribute__((packed));

    #define WLAN_FC_TYPE_DATA 2
    #define WLAN_FC_SUBTYPE_DATA 0

constexpr char *TAG = "TXFrame";

//-------------------------------------------------------------
// Implementation of Transmitter
//-------------------------------------------------------------

Transmitter::Transmitter(int k, int n, const std::string &keypair, uint64_t epoch, uint32_t channelId)
    : fecPtr_(nullptr, FecDeleter{}), fecK_(k), fecN_(n), blockIndex_(0), fragmentIndex_(0),
      block_(static_cast<size_t>(n)), maxPacketSize_(0), epoch_(epoch), channelId_(channelId) {
    // Create new fec object
    fec_t *rawFec = fec_new(fecK_, fecN_);
    if (!rawFec) {
        throw std::runtime_error("fec_new() failed");
    }
    fecPtr_.reset(rawFec);

    // Allocate block buffers
    for (int i = 0; i < fecN_; ++i) {
        block_[i] = std::unique_ptr<uint8_t[]>(new uint8_t[MAX_FEC_PAYLOAD]);
        std::memset(block_[i].get(), 0, MAX_FEC_PAYLOAD);
    }

    // Read keypair from file
    FILE *fp = std::fopen(keypair.c_str(), "rb");
    if (!fp) {
        throw std::runtime_error(string_format("Unable to open %s: %s", keypair.c_str(), std::strerror(errno)));
    }

    if (std::fread(txSecretKey_, crypto_box_SECRETKEYBYTES, 1, fp) != 1) {
        std::fclose(fp);
        throw std::runtime_error(string_format("Unable to read tx secret key: %s", std::strerror(errno)));
    }
    if (std::fread(rxPublicKey_, crypto_box_PUBLICKEYBYTES, 1, fp) != 1) {
        std::fclose(fp);
        throw std::runtime_error(string_format("Unable to read rx public key: %s", std::strerror(errno)));
    }
    std::fclose(fp);

    // Generate a fresh session key
    makeSessionKey();
}

Transmitter::~Transmitter() {
    // block_, fecPtr_ automatically cleaned up via unique_ptr
}

bool Transmitter::sendPacket(const uint8_t *buf, size_t size, uint8_t flags) {
    // If we are asked to finalize FEC block with no data while the block is empty, ignore
    if (fragmentIndex_ == 0 && (flags & WFB_PACKET_FEC_ONLY)) {
        return false;
    }

    // Ensure size is within user payload limit
    if (size > MAX_PAYLOAD_SIZE) {
        throw std::runtime_error("sendPacket: size exceeds MAX_PAYLOAD_SIZE");
    }

    // Write header
    auto *packetHdr = reinterpret_cast<wpacket_hdr_t *>(block_[fragmentIndex_].get());
    packetHdr->flags = flags;
    packetHdr->packet_size = htons(static_cast<uint16_t>(size));

    // Copy payload
    std::memcpy(block_[fragmentIndex_].get() + sizeof(wpacket_hdr_t), buf, size);

    // Zero out the remainder
    size_t totalHdrSize = sizeof(wpacket_hdr_t);
    if ((totalHdrSize + size) < MAX_FEC_PAYLOAD) {
        std::memset(block_[fragmentIndex_].get() + totalHdrSize + size, 0, MAX_FEC_PAYLOAD - (totalHdrSize + size));
    }

    // Send this fragment
    sendBlockFragment(totalHdrSize + size);

    // Track the largest data size in block
    maxPacketSize_ = std::max(maxPacketSize_, totalHdrSize + size);
    fragmentIndex_++;

    // If not enough fragments for FEC, we are done
    if (fragmentIndex_ < static_cast<uint8_t>(fecK_)) {
        return true;
    }

    // If we have k fragments, encode the parity
    fec_encode(fecPtr_.get(),
               const_cast<const uint8_t **>(reinterpret_cast<uint8_t **>(block_.data())),
               reinterpret_cast<uint8_t **>(block_.data()) + fecK_,
               maxPacketSize_);

    // Send all FEC fragments
    while (fragmentIndex_ < static_cast<uint8_t>(fecN_)) {
        sendBlockFragment(maxPacketSize_);
        fragmentIndex_++;
    }

    // Move to next block
    blockIndex_++;
    fragmentIndex_ = 0;
    maxPacketSize_ = 0;

    // Generate a new session key after we have looped over MAX_BLOCK_IDX blocks
    if (blockIndex_ > MAX_BLOCK_IDX) {
        makeSessionKey();
        sendSessionKey();
        blockIndex_ = 0;
    }
    return true;
}

void Transmitter::sendSessionKey() {
    injectPacket(sessionKeyPacket_, sizeof(sessionKeyPacket_));
}

void Transmitter::sendBlockFragment(size_t packetSize) {
    // Prepare local buffer for encryption
    uint8_t cipherBuf[MAX_FORWARDER_PACKET_SIZE];
    std::memset(cipherBuf, 0, sizeof(cipherBuf));

    auto *blockHdr = reinterpret_cast<wblock_hdr_t *>(cipherBuf);
    blockHdr->packet_type = WFB_PACKET_DATA;
    blockHdr->data_nonce = htobe64(((blockIndex_ & BLOCK_IDX_MASK) << 8) + fragmentIndex_);

    unsigned long long cipherLen = 0;

    // AEAD encrypt
    int rc = crypto_aead_chacha20poly1305_encrypt(cipherBuf + sizeof(wblock_hdr_t),
                                                  &cipherLen,
                                                  block_[fragmentIndex_].get(),
                                                  packetSize,
                                                  reinterpret_cast<const uint8_t *>(blockHdr),
                                                  sizeof(wblock_hdr_t),
                                                  nullptr,
                                                  reinterpret_cast<const uint8_t *>(&blockHdr->data_nonce),
                                                  sessionKey_);
    if (rc != 0) {
        throw std::runtime_error("Unable to encrypt packet!");
    }

    size_t finalSize = sizeof(wblock_hdr_t) + cipherLen;
    injectPacket(cipherBuf, finalSize);
}

void Transmitter::makeSessionKey() {
    // Random session key
    randombytes_buf(sessionKey_, sizeof(sessionKey_));

    auto *hdr = reinterpret_cast<wsession_hdr_t *>(sessionKeyPacket_);
    hdr->packet_type = WFB_PACKET_SESSION;
    randombytes_buf(hdr->session_nonce, sizeof(hdr->session_nonce));

    wsession_data_t sessionData = {};
    sessionData.epoch = htobe64(epoch_);
    sessionData.channel_id = htonl(channelId_);
    sessionData.fec_type = WFB_FEC_VDM_RS;
    sessionData.k = static_cast<uint8_t>(fecK_);
    sessionData.n = static_cast<uint8_t>(fecN_);
    std::memcpy(sessionData.session_key, sessionKey_, sizeof(sessionKey_));

    // Box it
    if (crypto_box_easy(sessionKeyPacket_ + sizeof(wsession_hdr_t),
                        reinterpret_cast<const uint8_t *>(&sessionData),
                        sizeof(sessionData),
                        hdr->session_nonce,
                        rxPublicKey_,
                        txSecretKey_) != 0) {
        throw std::runtime_error("Unable to create session key packet!");
    }
}

//-------------------------------------------------------------
// RawSocketTransmitter
//-------------------------------------------------------------

RawSocketTransmitter::RawSocketTransmitter(int k,
                                           int n,
                                           const std::string &keypair,
                                           uint64_t epoch,
                                           uint32_t channelId,
                                           const std::vector<std::string> &wlans,
                                           std::shared_ptr<uint8_t[]> radiotapHeader,
                                           size_t radiotapHeaderLen,
                                           uint8_t frameType)
    : Transmitter(k, n, keypair, epoch, channelId), channelId_(channelId), currentOutput_(0), ieee80211Sequence_(0),
      radiotapHeader_(std::move(radiotapHeader)), radiotapHeaderLen_(radiotapHeaderLen), frameType_(frameType) {
    // Create raw sockets and bind to specified interfaces
    for (const auto &iface : wlans) {
        int fd = ::socket(PF_PACKET, SOCK_RAW, 0);
        if (fd < 0) {
            throw std::runtime_error(
                string_format("Unable to open PF_PACKET socket on %s: %s", iface.c_str(), std::strerror(errno)));
        }

        const int optval = 1;
        if (setsockopt(fd, SOL_PACKET, PACKET_QDISC_BYPASS, &optval, sizeof(optval)) != 0) {
            ::close(fd);
            throw std::runtime_error(
                string_format("Unable to set PACKET_QDISC_BYPASS on %s: %s", iface.c_str(), std::strerror(errno)));
        }

        struct ifreq ifr;
        std::memset(&ifr, 0, sizeof(ifr));
        std::strncpy(ifr.ifr_name, iface.c_str(), sizeof(ifr.ifr_name) - 1);

        if (ioctl(fd, SIOCGIFINDEX, &ifr) < 0) {
            ::close(fd);
            throw std::runtime_error(
                string_format("Unable to get interface index for %s: %s", iface.c_str(), std::strerror(errno)));
        }

        struct sockaddr_ll sll;
        std::memset(&sll, 0, sizeof(sll));
        sll.sll_family = AF_PACKET;
        sll.sll_ifindex = ifr.ifr_ifindex;
        sll.sll_protocol = 0;

        if (::bind(fd, reinterpret_cast<struct sockaddr *>(&sll), sizeof(sll)) < 0) {
            ::close(fd);
            throw std::runtime_error(string_format("Unable to bind to %s: %s", iface.c_str(), std::strerror(errno)));
        }

        sockFds_.push_back(fd);
    }
}

RawSocketTransmitter::~RawSocketTransmitter() {
    for (int fd : sockFds_) {
        ::close(fd);
    }
}

void RawSocketTransmitter::injectPacket(const uint8_t *buf, size_t size) {
    if (size > MAX_FORWARDER_PACKET_SIZE) {
        throw std::runtime_error("RawSocketTransmitter::injectPacket - packet too large");
    }

    // Build 802.11 header
    uint8_t ieeeHdr[sizeof(ieee80211_header)];
    std::memcpy(ieeeHdr, ieee80211_header, sizeof(ieee80211_header));

    // Patch the Frame Control field, channel ID, and seq number
    ieeeHdr[0] = frameType_;
    uint32_t channelIdBE = htonl(channelId_);
    std::memcpy(ieeeHdr + SRC_MAC_THIRD_BYTE, &channelIdBE, sizeof(uint32_t));
    std::memcpy(ieeeHdr + DST_MAC_THIRD_BYTE, &channelIdBE, sizeof(uint32_t));

    ieeeHdr[FRAME_SEQ_LB] = static_cast<uint8_t>(ieee80211Sequence_ & 0xff);
    ieeeHdr[FRAME_SEQ_HB] = static_cast<uint8_t>((ieee80211Sequence_ >> 8) & 0xff);
    ieee80211Sequence_ += 16;

    // iovec for sendmsg
    struct iovec iov[3];
    std::memset(iov, 0, sizeof(iov));

    iov[0].iov_base = radiotapHeader_.get();
    iov[0].iov_len = radiotapHeaderLen_;
    iov[1].iov_base = const_cast<uint8_t *>(ieeeHdr);
    iov[1].iov_len = sizeof(ieeeHdr);
    iov[2].iov_base = const_cast<uint8_t *>(buf);
    iov[2].iov_len = size;

    msghdr msg;
    std::memset(&msg, 0, sizeof(msg));
    msg.msg_iov = iov;
    msg.msg_iovlen = 3;

    if (currentOutput_ >= 0) {
        // Single-interface mode
        uint64_t startUs = get_time_us();
        int rc = ::sendmsg(sockFds_[currentOutput_], &msg, 0);
        bool success = (rc >= 0 || errno == ENOBUFS);

        if (rc < 0 && errno != ENOBUFS) {
            throw std::runtime_error(string_format("Unable to inject packet: %s", std::strerror(errno)));
        }

        uint64_t key = (static_cast<uint64_t>(currentOutput_) << 8) | 0xff;
        antennaStat_[key].logLatency(get_time_us() - startUs, success, static_cast<uint32_t>(size));
    } else {
        // Mirror mode: send on all interfaces
        for (size_t i = 0; i < sockFds_.size(); i++) {
            uint64_t startUs = get_time_us();
            int rc = ::sendmsg(sockFds_[i], &msg, 0);
            bool success = (rc >= 0 || errno == ENOBUFS);

            if (rc < 0 && errno != ENOBUFS) {
                throw std::runtime_error(string_format("Unable to inject packet: %s", std::strerror(errno)));
            }
            uint64_t key = (static_cast<uint64_t>(i) << 8) | 0xff;
            antennaStat_[key].logLatency(get_time_us() - startUs, success, static_cast<uint32_t>(size));
        }
    }
}

void RawSocketTransmitter::dumpStats(FILE *fp,
                                     uint64_t ts,
                                     uint32_t &injectedPackets,
                                     uint32_t &droppedPackets,
                                     uint32_t &injectedBytes) {
    for (auto &kv : antennaStat_) {
        const auto &stats = kv.second;
        uint64_t countAll = stats.countPacketsInjected + stats.countPacketsDropped;
        uint64_t avgLatency = (countAll == 0) ? 0 : (stats.latencySum / countAll);

        // fprintf(fp,
        //         "%" PRIu64 "\tTX_ANT\t%" PRIx64 "\t%u:%u:%" PRIu64 ":%" PRIu64 ":%" PRIu64 "\n",
        //         ts,
        //         kv.first,
        //         stats.countPacketsInjected,
        //         stats.countPacketsDropped,
        //         stats.latencyMin,
        //         avgLatency,
        //         stats.latencyMax);

        injectedPackets += stats.countPacketsInjected;
        droppedPackets += stats.countPacketsDropped;
        injectedBytes += stats.countBytesInjected;
    }
    antennaStat_.clear();
}

//-------------------------------------------------------------
// UdpTransmitter
//-------------------------------------------------------------

UdpTransmitter::UdpTransmitter(int k,
                               int n,
                               const std::string &keypair,
                               const std::string &clientAddr,
                               int basePort,
                               uint64_t epoch,
                               uint32_t channelId)
    : Transmitter(k, n, keypair, epoch, channelId), sockFd_(-1), basePort_(basePort) {
    sockFd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (sockFd_ < 0) {
        throw std::runtime_error(string_format("Error opening UDP socket: %s", std::strerror(errno)));
    }

    std::memset(&saddr_, 0, sizeof(saddr_));
    saddr_.sin_family = AF_INET;
    saddr_.sin_addr.s_addr = ::inet_addr(clientAddr.c_str());
    saddr_.sin_port = htons(static_cast<unsigned short>(basePort_));
}

UdpTransmitter::~UdpTransmitter() {
    ::close(sockFd_);
}

void UdpTransmitter::selectOutput(int idx) {
    saddr_.sin_port = htons(static_cast<unsigned short>(basePort_ + idx));
}

void UdpTransmitter::injectPacket(const uint8_t *buf, size_t size) {
    // Create a random wrxfwd_t header
    wrxfwd_t fwdHeader = {};
    fwdHeader.wlan_idx = static_cast<uint8_t>(std::rand() % 2);

    std::memset(fwdHeader.antenna, 0xff, sizeof(fwdHeader.antenna));
    std::memset(fwdHeader.rssi, SCHAR_MIN, sizeof(fwdHeader.rssi));

    fwdHeader.antenna[0] = static_cast<uint8_t>(std::rand() % 2);
    fwdHeader.rssi[0] = static_cast<int8_t>(std::rand() & 0xff);

    // Two-element iovec
    iovec iov[2];
    iov[0].iov_base = reinterpret_cast<void *>(&fwdHeader);
    iov[0].iov_len = sizeof(fwdHeader);
    iov[1].iov_base = const_cast<uint8_t *>(buf);
    iov[1].iov_len = size;

    msghdr msg;
    std::memset(&msg, 0, sizeof(msg));
    msg.msg_name = &saddr_;
    msg.msg_namelen = sizeof(saddr_);
    msg.msg_iov = iov;
    msg.msg_iovlen = 2;

    ::sendmsg(sockFd_, &msg, 0);
}

//-------------------------------------------------------------
// UsbTransmitter
//-------------------------------------------------------------

UsbTransmitter::UsbTransmitter(int k,
                               int n,
                               const std::string &keypair,
                               uint64_t epoch,
                               uint32_t channelId,
                               const std::vector<std::string> &wlans,
                               uint8_t *radiotapHeader,
                               size_t radiotapHeaderLen,
                               uint8_t frameType,
                               Rtl8812aDevice *device)
    : Transmitter(k, n, keypair, epoch, channelId), channelId_(channelId), currentOutput_(0), ieee80211Sequence_(0),
      radiotapHeader_(radiotapHeader), radiotapHeaderLen_(radiotapHeaderLen), frameType_(frameType),
      rtlDevice_(device) {
    (void)wlans; // Not used directly here
}

void UsbTransmitter::dumpStats(FILE *fp,
                               uint64_t ts,
                               uint32_t &injectedPackets,
                               uint32_t &droppedPackets,
                               uint32_t &injectedBytes) {
    for (auto &kv : antennaStat_) {
        const auto &stats = kv.second;
        uint64_t countAll = stats.countPacketsInjected + stats.countPacketsDropped;
        uint64_t avgLatency = (countAll == 0) ? 0 : (stats.latencySum / countAll);

    #ifdef __ANDROID__
        __android_log_print(ANDROID_LOG_INFO,
                            TAG,
                            "%" PRIu64 "\tTX_ANT\t%" PRIx64 "\t%u:%u:%" PRIu64 ":%" PRIu64 ":%" PRIu64 "\n",
                            ts,
                            kv.first,
                            stats.countPacketsInjected,
                            stats.countPacketsDropped,
                            stats.latencyMin,
                            avgLatency,
                            stats.latencyMax);
    #else
            // fprintf(fp,
            //         "%" PRIu64 "\tTX_ANT\t%" PRIx64 "\t%u:%u:%" PRIu64 ":%" PRIu64 ":%" PRIu64 "\n",
            //         ts,
            //         kv.first,
            //         stats.countPacketsInjected,
            //         stats.countPacketsDropped,
            //         stats.latencyMin,
            //         avgLatency,
            //         stats.latencyMax);
    #endif

        injectedPackets += stats.countPacketsInjected;
        droppedPackets += stats.countPacketsDropped;
        injectedBytes += stats.countBytesInjected;
    }
    antennaStat_.clear();
}

void UsbTransmitter::injectPacket(const uint8_t *buf, size_t size) {
    if (!rtlDevice_ || rtlDevice_->should_stop) {
    #ifdef __ANDROID__
        __android_log_print(ANDROID_LOG_DEBUG, TAG, "Main thread exited, cannot send packets");
    #endif
        throw std::runtime_error("USB Transmitter: main thread exit, should stop");
    }

    if (size > MAX_FORWARDER_PACKET_SIZE) {
        throw std::runtime_error("UsbTransmitter::injectPacket - packet too large");
    }

    uint8_t ieeeHdr[sizeof(ieee80211_header)];
    std::memcpy(ieeeHdr, ieee80211_header, sizeof(ieee80211_header));

    // Patch frame type
    ieeeHdr[0] = frameType_;
    uint32_t channelIdBE = htonl(channelId_);
    std::memcpy(ieeeHdr + SRC_MAC_THIRD_BYTE, &channelIdBE, sizeof(uint32_t));
    std::memcpy(ieeeHdr + DST_MAC_THIRD_BYTE, &channelIdBE, sizeof(uint32_t));

    ieeeHdr[FRAME_SEQ_LB] = static_cast<uint8_t>(ieee80211Sequence_ & 0xff);
    ieeeHdr[FRAME_SEQ_HB] = static_cast<uint8_t>((ieee80211Sequence_ >> 8) & 0xff);
    ieee80211Sequence_ += 16;

    uint64_t startUs = get_time_us();

    // Merge into one contiguous buffer
    size_t totalSize = radiotapHeaderLen_ + sizeof(ieeeHdr) + size;
    std::unique_ptr<uint8_t[]> buffer(new uint8_t[totalSize]);

    std::memcpy(buffer.get(), radiotapHeader_, radiotapHeaderLen_);
    std::memcpy(buffer.get() + radiotapHeaderLen_, ieeeHdr, sizeof(ieeeHdr));
    std::memcpy(buffer.get() + radiotapHeaderLen_ + sizeof(ieeeHdr), buf, size);

    bool result = static_cast<bool>(rtlDevice_->send_packet(buffer.get(), totalSize));

    #ifdef __ANDROID__
    //    __android_log_print(ANDROID_LOG_DEBUG, TAG, "send_packet res:%d", result);
    #endif

    uint64_t key = (static_cast<uint64_t>(currentOutput_) << 8) | 0xff;
    antennaStat_[key].logLatency(get_time_us() - startUs, result, static_cast<uint32_t>(size));
}

//-------------------------------------------------------------
// TxFrame
//-------------------------------------------------------------

TxFrame::TxFrame() = default;
TxFrame::~TxFrame() = default;

void TxFrame::stop() {
    shouldStop_ = true;
}

uint32_t TxFrame::extractRxqOverflow(struct msghdr *msg) {
    for (struct cmsghdr *cmsg = CMSG_FIRSTHDR(msg); cmsg != nullptr; cmsg = CMSG_NXTHDR(msg, cmsg)) {
        if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SO_RXQ_OVFL) {
            uint32_t val = 0;
            std::memcpy(&val, CMSG_DATA(cmsg), sizeof(val));
            return val;
        }
    }
    return 0;
}

int TxFrame::open_udp_socket_for_rx(int port, int buf_size) {
    int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        throw std::runtime_error(string_format("Unable to open socket: %s", std::strerror(errno)));
    }

    int optval = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    // Set receive timeout to 500ms
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 500000; // 500ms
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        ::close(fd);
        throw std::runtime_error(string_format("Unable to set socket timeout: %s", std::strerror(errno)));
    }

    if (buf_size) {
        if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &buf_size, sizeof(buf_size)) < 0) {
            ::close(fd);
            throw std::runtime_error(string_format("Unable to set requested buffer size: %s", std::strerror(errno)));
        }
        int actual_buf_size = 0;
        socklen_t optlen = sizeof(actual_buf_size);
        getsockopt(fd, SOL_SOCKET, SO_RCVBUF, &actual_buf_size, &optlen);
        if (actual_buf_size < buf_size * 2) {
            // Linux doubles the value we set
            fprintf(stderr, "Warning: requested rx buffer size %d but got %d\n", buf_size, actual_buf_size / 2);
        }
    }

    struct sockaddr_in saddr;
    std::memset(&saddr, 0, sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_addr.s_addr = htonl(INADDR_ANY);
    saddr.sin_port = htons(static_cast<uint16_t>(port));

    if (::bind(fd, reinterpret_cast<struct sockaddr *>(&saddr), sizeof(saddr)) < 0) {
        ::close(fd);
        throw std::runtime_error(string_format("Unable to bind to port %d: %s", port, std::strerror(errno)));
    }

    return fd;
}

/**
 * Radiotap is a protocol of sorts that is used to convey information about the
 * physical-layer part of wireless transmissions. When monitoring an interface
 * for packets, it will contain information such as what rate was used, what
 * channel it was sent on, etc. When injecting a packet, we can use it to tell
 * the 802.11 card how we want the frame to be transmitted.
 *
 * The format of the radiotap header is somewhat odd.
 * include/net/ieee80211_radiotap.h does an okay job of explaining it, but I'll
 * try to give a quick overview here.
 *
 * Keep in mind that all the fields here are little-endian, so you should
 * reverse the order of the bytes in your head when reading. Also, fields that
 * are set to 0 just mean that we let the card choose what values to use for
 * that option (for rate and channel for example, we'll let the card decide).
 */
static const uint8_t u8aRadiotapHeader[] = {

    0x00,
    0x00, // <-- radiotap version (ignore this)
    0x18,
    0x00, // <-- number of bytes in our header (count the number of "0x"s)

    /**
     * The next field is a bitmap of which options we are including.
     * The full list of which field is which option is in ieee80211_radiotap.h,
     * but I've chosen to include:
     *   0x00 0x01: timestamp
     *   0x00 0x02: flags
     *   0x00 0x03: rate
     *   0x00 0x04: channel
     *   0x80 0x00: tx flags (seems silly to have this AND flags, but oh well)
     */
    0x0f,
    0x80,
    0x00,
    0x00,

    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00, // <-- timestamp

    /**
     * This is the first set of flags, and we've set the bit corresponding to
     * IEEE80211_RADIOTAP_F_FCS, meaning we want the card to add a FCS at the end
     * of our buffer for us.
     */
    0x10,

    0x00, // <-- rate
    0x00,
    0x00,
    0x00,
    0x00, // <-- channel

    /**
     * This is the second set of flags, specifically related to transmissions. The
     * bit we've set is IEEE80211_RADIOTAP_F_TX_NOACK, which means the card won't
     * wait for an ACK for this frame, and that it won't retry if it doesn't get
     * one.
     */
    0x08,
    0x00,
};

/**
 * After an 802.11 MAC-layer header, a logical link control (LLC) header should
 * be placed to tell the receiver what kind of data will follow (see IEEE 802.2
 * for more information).
 *
 * For political reasons, IP wasn't allocated a global so-called SAP number,
 * which means that a simple LLC header is not enough to indicate that an IP
 * frame was sent. 802.2 does, however, allow EtherType types (the same kind of
 * type numbers used in, you guessed it, Ethernet) through the use of the
 * "Subnetwork Access Protocol", or SNAP. To use SNAP, the three bytes in the
 * LLC have to be set to the magical numbers 0xAA 0xAA 0x03. The next five bytes
 * are then interpreted as a SNAP header. To specify an EtherType, we need to
 * set the first three of them to 0. The last two bytes can then finally be set
 * to 0x0800, which is the IP EtherType.
 */
const uint8_t ipllc[8] = {0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00, 0x08, 0x00};

/**
 * A simple implementation of the internet checksum used by IP
 * Not very interesting, so it has been moved below main()
 */
uint16_t inet_csum(const void *buf, size_t hdr_len);

uint16_t inet_csum(const void *buf, size_t hdr_len) {
    unsigned long sum = 0;
    const uint16_t *ip1;

    ip1 = (const uint16_t *)buf;
    while (hdr_len > 1) {
        sum += *ip1++;
        if (sum & 0x80000000) sum = (sum & 0xFFFF) + (sum >> 16);
        hdr_len -= 2;
    }

    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);

    return (~sum);
}

/* A bogus MAC address just to show that it can be done */
const uint8_t mac[6] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab};

void TxFrame::dataSource(std::shared_ptr<Transmitter> &transmitter,
                         std::vector<int> &rxFds,
                         int fecTimeout,
                         bool mirror,
                         int logInterval) {
    int nfds = static_cast<int>(rxFds.size());
    if (nfds <= 0) {
        throw std::runtime_error("dataSource: no valid rx sockets");
    }

    // Set timeout on all sockets
    for (int fd : rxFds) {
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 500000; // 500ms
        if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
            throw std::runtime_error(string_format("Unable to set socket timeout: %s", std::strerror(errno)));
        }
    }

    std::vector<pollfd> fds(nfds);
    for (int i = 0; i < nfds; ++i) {
        fds[i].fd = rxFds[i];
        fds[i].events = POLLIN;
    }

    uint64_t sessionKeyAnnounceTs = 0;
    uint32_t rxqOverflowCount = 0;
    uint64_t logSendTs = 0;
    uint64_t fecCloseTs = (fecTimeout > 0) ? get_time_ms() + fecTimeout : 0;

    // Stats counters
    uint32_t countPFecTimeouts = 0;
    uint32_t countPIncoming = 0;
    uint32_t countBIncoming = 0;
    uint32_t countPInjected = 0;
    uint32_t countBInjected = 0;
    uint32_t countPDropped = 0;
    uint32_t countPTruncated = 0;

    int startFdIndex = 0;

    while (true) {
        if (shouldStop_) {
    #ifdef __ANDROID__
            __android_log_print(ANDROID_LOG_DEBUG, TAG, "TxFrame: stopping main loop");
    #endif
            break;
        }

        uint64_t curTs = get_time_ms();
        int pollTimeout = 0;
        if (curTs < logSendTs) {
            pollTimeout = static_cast<int>(logSendTs - curTs);
        }

        if (fecTimeout > 0) {
            int ft = static_cast<int>((fecCloseTs > curTs) ? (fecCloseTs - curTs) : 0);
            if (pollTimeout == 0 || ft < pollTimeout) {
                pollTimeout = ft;
            }
        }

        int rc = ::poll(fds.data(), nfds, pollTimeout);
        if (rc < 0) {
            if (errno == EINTR || errno == EAGAIN) {
                continue;
            }
            throw std::runtime_error(string_format("poll error: %s", std::strerror(errno)));
        }

        // Logging at intervals
        curTs = get_time_ms();
        if (curTs >= logSendTs) {
            transmitter->dumpStats(stdout, curTs, countPInjected, countPDropped, countBInjected);
    #ifdef __ANDROID__
            __android_log_print(ANDROID_LOG_INFO,
                                TAG,
                                "%" PRIu64 "\tPKT\t%u:%u:%u:%u:%u:%u:%u\n",
                                curTs,
                                countPFecTimeouts,
                                countPIncoming,
                                countBIncoming,
                                countPInjected,
                                countBInjected,
                                countPDropped,
                                countPTruncated);
    #else
                // std::fprintf(stdout,
                //              "%" PRIu64 "\tPKT\t%u:%u:%u:%u:%u:%u:%u\n",
                //              curTs,
                //              countPFecTimeouts,
                //              countPIncoming,
                //              countBIncoming,
                //              countPInjected,
                //              countBInjected,
                //              countPDropped,
                //              countPTruncated);
                // std::fflush(stdout);
    #endif

            if (countPDropped) {
                std::fprintf(stderr, "%u packets dropped\n", countPDropped);
            }
            if (countPTruncated) {
                std::fprintf(stderr, "%u packets truncated\n", countPTruncated);
            }

            // Reset counters
            countPFecTimeouts = 0;
            countPIncoming = 0;
            countBIncoming = 0;
            countPInjected = 0;
            countBInjected = 0;
            countPDropped = 0;
            countPTruncated = 0;
            logSendTs = curTs + logInterval;
        }

        if (rc == 0) {
            // Timed out
            if (fecTimeout > 0 && (curTs >= fecCloseTs)) {
                // Send a FEC-only to close block if block is open
                if (!transmitter->sendPacket(nullptr, 0, WFB_PACKET_FEC_ONLY)) {
                    ++countPFecTimeouts;
                }
                fecCloseTs = get_time_ms() + fecTimeout;
            }
            continue;
        }

        // We have events
        int i = startFdIndex;
        for (startFdIndex = 0; rc > 0; i++) {
            pollfd &pfd = fds[static_cast<size_t>(i % nfds)];
            if (pfd.revents & (POLLERR | POLLNVAL)) {
                throw std::runtime_error(string_format("socket error: %s", std::strerror(errno)));
            }

            if (pfd.revents & POLLIN) {
                --rc;

                // Mirror or single output selection
                transmitter->selectOutput(mirror ? -1 : (i % nfds));

                while (true) {
                    if (shouldStop_) {
    #ifdef __ANDROID__
                        __android_log_print(ANDROID_LOG_DEBUG, TAG, "TxFrame: stopping in POLLIN loop");
    #endif
                        break;
                    }

                    uint8_t buf[MAX_PAYLOAD_SIZE + 1];
                    std::memset(buf, 0, sizeof(buf));

                    uint8_t cmsgbuf[CMSG_SPACE(sizeof(uint32_t))];
                    std::memset(cmsgbuf, 0, sizeof(cmsgbuf));

                    iovec iov = {};
                    iov.iov_base = buf;
                    iov.iov_len = sizeof(buf);

                    msghdr msg = {};
                    msg.msg_iov = &iov;
                    msg.msg_iovlen = 1;
                    msg.msg_control = cmsgbuf;
                    msg.msg_controllen = sizeof(cmsgbuf);

                    ssize_t rsize = ::recvmsg(pfd.fd, &msg, 0);
                    if (rsize < 0) {
                        if (errno != EWOULDBLOCK && errno != EAGAIN && errno != ETIMEDOUT) {
                            continue;
                            throw std::runtime_error(string_format("Error receiving packet: %s", std::strerror(errno)));
                        }
                        break;
                    }

                    // Incoming stats
                    ++countPIncoming;
                    countBIncoming += static_cast<uint32_t>(rsize);

                    if (rsize > static_cast<ssize_t>(MAX_PAYLOAD_SIZE)) {
                        rsize = MAX_PAYLOAD_SIZE;
                        ++countPTruncated;
                    }

                    uint32_t curOverflow = extractRxqOverflow(&msg);
                    if (curOverflow != rxqOverflowCount) {
                        uint32_t diff = (curOverflow - rxqOverflowCount);
                        countPDropped += diff;
                        countPIncoming += diff; // All these overflows are potential incoming
                        rxqOverflowCount = curOverflow;
                    }

                    // Possibly re-announce session key
                    uint64_t nowTs = get_time_ms();
                    if (nowTs >= sessionKeyAnnounceTs) {
                        transmitter->sendSessionKey();
                        sessionKeyAnnounceTs = nowTs + SESSION_KEY_ANNOUNCE_MSEC;
                    }

                    // Total packet size
                    size_t packet_size = sizeof(u8aRadiotapHeader) + sizeof(struct ieee80211_hdr) + sizeof(ipllc) +
                                         sizeof(struct iphdr) + sizeof(struct udphdr) + rsize /* data */ + 4 /* FCS */;

                    // Packet
                    uint8_t *packet = (uint8_t *)malloc(packet_size);

                    /* The parts of our packet */
                    uint8_t *rt; /* radiotap */
                    struct ieee80211_hdr *ieee80211;
                    uint8_t *llc;
                    struct iphdr *ip;
                    struct udphdr *udp;
                    uint8_t *data;

                    /* Other useful bits */
                    uint8_t fcchunk[2]; /* 802.11 header frame control */

                    /* Put our pointers in the right place */
                    ieee80211 = (struct ieee80211_hdr *)(packet + sizeof(u8aRadiotapHeader));
                    llc = (uint8_t *)(ieee80211 + sizeof(struct ieee80211_hdr));
                    ip = (struct iphdr *)(llc + sizeof(ipllc));
                    udp = (struct udphdr *)(ip + sizeof(struct iphdr));
                    data = (uint8_t *)(udp + sizeof(struct udphdr));

                    /* The radiotap header has been explained already */
                    memcpy(packet, u8aRadiotapHeader, sizeof(u8aRadiotapHeader));

                    /**
                     * Next, we need to construct the 802.11 header
                     *
                     * The biggest trick here is the frame control field.
                     * http://www.wildpackets.com/resources/compendium/wireless_lan/wlan_packets
                     * gives a fairly good explanation.
                     *
                     * The first byte of the FC gives the type and "subtype" of the 802.11 frame.
                     * We're transmitting a data frame, so we set both the type and the subtype to
                     * DATA.
                     *
                     * Most guides also forget to mention that the bits *within each byte* in the
                     * FC are reversed (!!!), so FROMDS is actually the *second to last* bit in
                     * the FC, hence 0x02.
                     */
                    fcchunk[0] = ((WLAN_FC_TYPE_DATA << 2) | (WLAN_FC_SUBTYPE_DATA << 4));
                    fcchunk[1] = 0x02;
                    memcpy(&ieee80211->frame_control, &fcchunk[0], 2 * sizeof(uint8_t));

                    /**
                     * The remaining fields are more straight forward.
                     * The duration we can set to some arbitrary high number, and the sequence
                     * number can safely be set to 0.
                     * The addresses here can be set to whatever, but bear in mind that which
                     * address corresponds to source/destination/BSSID will vary depending on
                     * which of TODS and FROMDS are set. The full table can be found at the
                     * wildpackets.com link above, or condensed here:
                     *
                     *  +-------+---------+-------------+-------------+-------------+-----------+
                     *  | To DS | From DS | Address 1   | Address 2   | Address 3   | Address 4 |
                     *  +-------+---------+-------------+-------------+-------------+-----------+
                     *  |     0 |       0 | Destination | Source      | BSSID       | N/A       |
                     *  |     0 |       1 | Destination | BSSID       | Source      | N/A       |
                     *  |     1 |       0 | BSSID       | Source      | Destination | N/A       |
                     *  |     1 |       1 | Receiver    | Transmitter | Destination | Source    |
                     *  +-------+---------+-------------+-------------+-------------+-----------+
                     *
                     * Also note that addr4 has been commented out. This is because it should not
                     * be present unless both TODS *and* FROMDS has been set (as shown above).
                     */
                    ieee80211->duration_id = 0xffff;
                    memcpy(&ieee80211->addr1[0], mac, 6 * sizeof(uint8_t));
                    memcpy(&ieee80211->addr2[0], mac, 6 * sizeof(uint8_t));
                    memcpy(&ieee80211->addr3[0], mac, 6 * sizeof(uint8_t));
                    ieee80211->seq_ctrl = 0;
                    // hdr->addr4;

                    /* The LLC+SNAP header has already been explained above */
                    memcpy(llc, ipllc, 8 * sizeof(uint8_t));

                    const char *to = "10.5.0.10";
                    const char *from = "192.168.1.100";

                    struct sockaddr_in saddr, daddr; /* IP source and destination */
                    daddr.sin_family = AF_INET;
                    saddr.sin_family = AF_INET;
                    daddr.sin_port = htons(9999);
                    saddr.sin_port = htons(50505);
                    inet_pton(AF_INET, to, (struct in_addr *)&daddr.sin_addr.s_addr);
                    inet_pton(AF_INET, from, (struct in_addr *)&saddr.sin_addr.s_addr);

                    static int packet_id = 0;

                    // IP header
                    // struct iphdr *ip = (struct iphdr *)packet + sizeof(struct ieee80211_hdr);
                    ip->saddr = saddr.sin_addr.s_addr;
                    ip->daddr = daddr.sin_addr.s_addr;
                    ip->ihl = 5; /* header length, number of 32-bit words */
                    ip->version = 4;
                    ip->tos = 0;
                    ip->tot_len = htons(sizeof(struct iphdr) + sizeof(struct udphdr) + rsize);
                    ip->id = htons(packet_id++);
                    ip->frag_off = htons(0x4000); /* Don't fragment */
                    ip->ttl = 64;
                    ip->protocol = IPPROTO_UDP;

                    /**
                     * The checksum should be calculated over the entire header with the checksum
                     * field set to 0, so that's what we do
                     */
                    ip->check = 0; // Will be calculated later
                    ip->check = inet_csum(ip, sizeof(struct iphdr));

                    // UDP header
                    /**
                     * The UDP header is refreshingly simple.
                     * Again, notice the little-endianness of ->len
                     * UDP also lets us set the checksum to 0 to ignore it
                     */
                    udp->source = saddr.sin_port;
                    udp->dest = daddr.sin_port;
                    udp->len = htons(sizeof(struct udphdr) + rsize);
                    udp->check = 0; // Optional

                    // Payload
                    memcpy(data, buf, rsize);

                    // Forward packet
                    transmitter->sendPacket(packet, packet_size, 0);

                    free(packet);

                    // If we've hit a log boundary inside the same poll, break to flush stats
                    if (nowTs >= logSendTs) {
                        startFdIndex = i % nfds;
                        break;
                    }
                }
            }
        }

        // Reset FEC timer if data arrived
        if (fecTimeout > 0) {
            fecCloseTs = get_time_ms() + fecTimeout;
        }
    }
}

void TxFrame::run(Rtl8812aDevice *rtlDevice, TxArgs *arg) {
    // Decide if using VHT
    if (arg->bandwidth >= 80) {
        arg->vht_mode = true;
    }

    // Radiotap header preparation
    std::unique_ptr<uint8_t[]> rtHeader;
    size_t rtHeaderLen = 0;
    uint8_t frameType = FRAME_TYPE_RTS;

    // Construct the appropriate radiotap header (HT vs. VHT)
    if (!arg->vht_mode) {
        // HT mode
        uint8_t flags = 0;
        switch (arg->bandwidth) {
            case 10:
            case 20:
                flags |= IEEE80211_RADIOTAP_MCS_BW_20;
                break;
            case 40:
                flags |= IEEE80211_RADIOTAP_MCS_BW_40;
                break;
            default:
                throw std::runtime_error(string_format("Unsupported bandwidth: %d", arg->bandwidth));
        }

        if (arg->short_gi) {
            flags |= IEEE80211_RADIOTAP_MCS_SGI;
        }

        switch (arg->stbc) {
            case 0:
                break;
            case 1:
                flags |= (IEEE80211_RADIOTAP_MCS_STBC_1 << IEEE80211_RADIOTAP_MCS_STBC_SHIFT);
                break;
            case 2:
                flags |= (IEEE80211_RADIOTAP_MCS_STBC_2 << IEEE80211_RADIOTAP_MCS_STBC_SHIFT);
                break;
            case 3:
                flags |= (IEEE80211_RADIOTAP_MCS_STBC_3 << IEEE80211_RADIOTAP_MCS_STBC_SHIFT);
                break;
            default:
                throw std::runtime_error(string_format("Unsupported STBC type: %d", arg->stbc));
        }

        if (arg->ldpc) {
            flags |= IEEE80211_RADIOTAP_MCS_FEC_LDPC;
        }

        rtHeaderLen = sizeof(radiotap_header_ht);
        rtHeader.reset(new uint8_t[rtHeaderLen]);
        std::memcpy(rtHeader.get(), radiotap_header_ht, rtHeaderLen);

        rtHeader[MCS_FLAGS_OFF] = flags;
        rtHeader[MCS_IDX_OFF] = static_cast<uint8_t>(arg->mcs_index);
    } else {
        // VHT mode
        uint8_t flags = 0;
        rtHeaderLen = sizeof(radiotap_header_vht);
        rtHeader.reset(new uint8_t[rtHeaderLen]);
        std::memcpy(rtHeader.get(), radiotap_header_vht, rtHeaderLen);

        if (arg->short_gi) {
            flags |= IEEE80211_RADIOTAP_VHT_FLAG_SGI;
        }
        if (arg->stbc) {
            flags |= IEEE80211_RADIOTAP_VHT_FLAG_STBC;
        }

        switch (arg->bandwidth) {
            case 80:
                rtHeader[VHT_BW_OFF] = IEEE80211_RADIOTAP_VHT_BW_80M;
                break;
            case 160:
                rtHeader[VHT_BW_OFF] = IEEE80211_RADIOTAP_VHT_BW_160M;
                break;
            default:
                throw std::runtime_error(string_format("Unsupported VHT bandwidth: %d", arg->bandwidth));
        }

        if (arg->ldpc) {
            rtHeader[VHT_CODING_OFF] = IEEE80211_RADIOTAP_VHT_CODING_LDPC_USER0;
        }

        rtHeader[VHT_FLAGS_OFF] = flags;
        rtHeader[VHT_MCSNSS0_OFF] |= static_cast<uint8_t>((arg->mcs_index << IEEE80211_RADIOTAP_VHT_MCS_SHIFT) &
                                                          IEEE80211_RADIOTAP_VHT_MCS_MASK);
        rtHeader[VHT_MCSNSS0_OFF] |=
            static_cast<uint8_t>((arg->vht_nss << IEEE80211_RADIOTAP_VHT_NSS_SHIFT) & IEEE80211_RADIOTAP_VHT_NSS_MASK);
    }

    // Check system entropy
    {
        int fd = ::open("/dev/random", O_RDONLY);
        if (fd != -1) {
            int eCount = 0;
            if (::ioctl(fd, RNDGETENTCNT, &eCount) == 0 && eCount < 160) {
                std::fprintf(stderr,
                             "Warning: Low entropy available. Consider installing rng-utils, "
                             "jitterentropy, or haveged to increase entropy.\n");
            }
            ::close(fd);
        }
    }

    // Attempt to create a UDP listening socket
    std::vector<int> rxFds;
    int bindPort = arg->udp_port;
    int udpFd = TxFrame::open_udp_socket_for_rx(bindPort, arg->rcv_buf);

    if (arg->udp_port == 0) {
        // ephemeral port
        struct sockaddr_in saddr;
        socklen_t saddrLen = sizeof(saddr);
        if (getsockname(udpFd, reinterpret_cast<struct sockaddr *>(&saddr), &saddrLen) != 0) {
            throw std::runtime_error(string_format("Unable to get ephemeral port: %s", std::strerror(errno)));
        }
        bindPort = ntohs(saddr.sin_port);
        // std::printf("%" PRIu64 "\tLISTEN_UDP\t%d\n", get_time_ms(), bindPort);
    }

    #ifdef __ANDROID__
    __android_log_print(ANDROID_LOG_INFO, TAG, "Listening on UDP port: %d", bindPort);
    #else
    std::fprintf(stderr, "Listening on UDP port: %d\n", bindPort);
    #endif
    rxFds.push_back(udpFd);

    if (arg->udp_port == 0) {
    #ifdef __ANDROID__
        __android_log_print(ANDROID_LOG_INFO, TAG, "Listening on UDP port: %d", bindPort);
    #else
        std::fprintf(stderr, "Listening on UDP port: %d\n", bindPort);
    #endif
    }

    try {
        uint32_t channelId = (arg->link_id << 8) + arg->radio_port;
        std::shared_ptr<Transmitter> transmitter;

        if (arg->debug_port) {
            // Send data out via UDP to 127.0.0.1:debug_port
            transmitter = std::make_shared<UdpTransmitter>(arg->k,
                                                           arg->n,
                                                           arg->keypair,
                                                           "127.0.0.1",
                                                           arg->debug_port,
                                                           arg->epoch,
                                                           channelId);
        } else {
            // Use the USB-based transmitter
            transmitter = std::make_shared<UsbTransmitter>(arg->k,
                                                           arg->n,
                                                           arg->keypair,
                                                           arg->epoch,
                                                           channelId,
                                                           std::vector<std::string>{}, // wlans not used in USB
                                                           rtHeader.get(),
                                                           rtHeaderLen,
                                                           frameType,
                                                           rtlDevice);
        }

        // Start polling loop
        dataSource(transmitter, rxFds, arg->fec_timeout, arg->mirror, arg->log_interval);
    } catch (const std::runtime_error &ex) {
    #ifdef __ANDROID__
        __android_log_print(ANDROID_LOG_ERROR, TAG, "Error in TxFrame::run: %s", ex.what());
    #else
        std::fprintf(stderr, "Error in TxFrame::run: %s\n", ex.what());
    #endif
    }
}

#endif
