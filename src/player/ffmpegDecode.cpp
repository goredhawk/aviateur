#include "ffmpegDecode.h"

#include <iostream>
#include <vector>

#include "src/gui_interface.h"

#define MAX_AUDIO_PACKET (2 * 1024 * 1024)

bool FfmpegDecoder::OpenInput(std::string &inputFile, bool forceSoftwareDecoding) {
    av_log_set_level(AV_LOG_ERROR);

    CloseInput();

    hwDecoderEnabled = false;

    // Check if any hardware decoder exists.
    if (!forceSoftwareDecoding) {
        AVHWDeviceType decoderType = AV_HWDEVICE_TYPE_NONE;
        std::vector<AVHWDeviceType> supportedHWDevices;
        do {
            decoderType = av_hwdevice_iterate_types(decoderType);

            if (decoderType != AV_HWDEVICE_TYPE_NONE) {
                auto decoderName = std::string(av_hwdevice_get_type_name(decoderType));
                GuiInterface::Instance().PutLog(LogLevel::Info, "Found hardware decoder: " + decoderName);
                supportedHWDevices.push_back(decoderType);
            }
        } while (decoderType != AV_HWDEVICE_TYPE_NONE);

        if (!supportedHWDevices.empty()) {
            hwDecoderType = supportedHWDevices.front();
            GuiInterface::Instance().PutLog(
                LogLevel::Info,
                "Using hardware decoder: " + std::string(av_hwdevice_get_type_name(hwDecoderType)));
            hwDecoderEnabled = true;
        }
    }

    AVDictionary *param = nullptr;

    av_dict_set(&param, "preset", "ultrafast", 0);
    av_dict_set(&param, "tune", "zerolatency", 0);
    av_dict_set(&param, "buffer_size", "425984", 0);
    av_dict_set(&param, "rtsp_transport", "tcp", 0);
    av_dict_set(&param, "protocol_whitelist", "file,udp,tcp,rtp,rtmp,rtsp,http", 0);
    av_dict_set_int(&param, "stimeout", (int64_t)10, 0);

    // 打开输入
    if (avformat_open_input(&pFormatCtx, inputFile.c_str(), nullptr, &param) != 0) {
        CloseInput();
        return false;
    }

    // 超时机制
    static const int timeout = 10;
    startTime = std::chrono::steady_clock::now();

    pFormatCtx->interrupt_callback.callback = [](void *timestamp) -> int {
        auto now = std::chrono::steady_clock::now();
        std::chrono::duration<double, std::chrono::seconds::period> duration =
            now - *(std::chrono::time_point<std::chrono::steady_clock> *)timestamp;
        return duration.count() > timeout;
    };
    pFormatCtx->interrupt_callback.opaque = &startTime;

    if (avformat_find_stream_info(pFormatCtx, nullptr) < 0) {
        CloseInput();
        return false;
    }

    std::chrono::duration<double, std::chrono::seconds::period> duration = std::chrono::steady_clock::now() - startTime;
    // 分析超时，退出，可能格式不正确
    if (duration.count() > timeout) {
        CloseInput();
        return false;
    }
    pFormatCtx->interrupt_callback.callback = nullptr;
    pFormatCtx->interrupt_callback.opaque = nullptr;

    // 打开视频/音频输入
    hasVideoStream = OpenVideo();
    hasAudioStream = OpenAudio();

    sourceIsOpened = true;

    // 转换时间基
    if (videoStreamIndex != -1) {
        videoFps = static_cast<float>(av_q2d(pFormatCtx->streams[videoStreamIndex]->r_frame_rate));
        videoBaseTime = av_q2d(pFormatCtx->streams[videoStreamIndex]->time_base);

        GuiInterface::Instance().PutLog(LogLevel::Info, "Video FPS: {}", videoFps);
    }

    if (audioStreamIndex != -1) {
        audioBaseTime = av_q2d(pFormatCtx->streams[audioStreamIndex]->time_base);
    }

    // 创建音频解码缓存
    if (hasAudioStream) {
        audioFifoBuffer = std::shared_ptr<AVFifo>(
            av_fifo_alloc2(0, GetAudioFrameSamples() * GetAudioChannelCount() * 10, AV_FIFO_FLAG_AUTO_GROW));
    }

    return true;
}

bool FfmpegDecoder::CloseInput() {
    sourceIsOpened = false;

    std::lock_guard lck(_releaseLock);

    CloseVideo();
    CloseAudio();

    if (pFormatCtx) {
        avformat_close_input(&pFormatCtx);
        pFormatCtx = nullptr;
    }

    return true;
}

void freeFrame(AVFrame *f) {
    av_frame_free(&f);
}
void freePkt(AVPacket *f) {
    av_packet_free(&f);
}
void freeSwrCtx(SwrContext *s) {
    swr_free(&s);
}

std::shared_ptr<AVFrame> FfmpegDecoder::GetNextFrame() {
    std::lock_guard lck(_releaseLock);

    std::shared_ptr<AVFrame> res;

    if (videoStreamIndex == -1 && audioStreamIndex == -1) {
        return res;
    }

    if (!sourceIsOpened) {
        return res;
    }

    while (true) {
        if (!pFormatCtx) {
            throw std::runtime_error("AVFormatContext is null");
        }
        std::shared_ptr<AVPacket> packet = std::shared_ptr<AVPacket>(av_packet_alloc(), &freePkt);
        int ret = av_read_frame(pFormatCtx, packet.get());
        if (ret < 0) {
            char errStr[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, errStr, AV_ERROR_MAX_STRING_SIZE);
            throw std::runtime_error("av_read_frame failed: " + std::string(errStr));
        }

        // Calculate bitrate
        {
            bytesSecond += packet->size;
            uint64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::system_clock::now().time_since_epoch())
                               .count();
            if (now - lastCountBitrateTime >= 1000) {
                // 计算码率定时器
                bitrate = bytesSecond * 8 * 1000 / (now - lastCountBitrateTime);
                bytesSecond = 0;

                emitBitrateUpdate(bitrate);

                lastCountBitrateTime = now;
            }
        }

        // Handle video
        if (packet->stream_index == videoStreamIndex) {
            // 回调nalu
            if (_gotPktCallback) {
                _gotPktCallback(packet);
            }
            // 处理视频数据
            std::shared_ptr<AVFrame> pVideoYuv = std::shared_ptr<AVFrame>(av_frame_alloc(), &freeFrame);
            // 解码视频祯
            bool isDecodeComplete = DecodeVideo(packet.get(), pVideoYuv);
            if (isDecodeComplete) {
                res = pVideoYuv;
            }
            // 回调frame
            if (_gotFrameCallback) {
                _gotFrameCallback(pVideoYuv);
            }
            break;
        }

        // Handle audio
        if (packet->stream_index == audioStreamIndex) {
            // 回调nalu
            if (_gotPktCallback) {
                _gotPktCallback(packet);
            }
            // 处理音频数据
            if (packet->dts != AV_NOPTS_VALUE) {
                int audioFrameSize = MAX_AUDIO_PACKET;
                std::shared_ptr<uint8_t> pFrameAudio = std::shared_ptr<uint8_t>(new uint8_t[audioFrameSize]);
                // 解码音频祯
                int nDecodedSize = DecodeAudio(audioStreamIndex, packet.get(), pFrameAudio.get(), audioFrameSize);
                // 解码成功，解码数据写入音频缓存
                if (nDecodedSize > 0) {
                    writeAudioBuff(pFrameAudio.get(), nDecodedSize);
                }
            }
            if (!HasVideo()) {
                return res;
            }
        }
    }
    return res;
}

bool FfmpegDecoder::initHwDecoder(AVCodecContext *ctx, const AVHWDeviceType type) {
    if (av_hwdevice_ctx_create(&hwDeviceCtx, type, nullptr, nullptr, 0) < 0) {
        return false;
    }
    ctx->hw_device_ctx = av_buffer_ref(hwDeviceCtx);

    return true;
}

bool FfmpegDecoder::OpenVideo() {
    bool res = false;

    if (pFormatCtx) {
        videoStreamIndex = -1;

        for (uint32_t i = 0; i < pFormatCtx->nb_streams; i++) {
            if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                videoStreamIndex = i;
                const AVCodec *codec = avcodec_find_decoder(pFormatCtx->streams[i]->codecpar->codec_id);

                if (!codec) {
                    continue;
                }

                if (hwDecoderEnabled) {
                    for (int configIndex = 0;; configIndex++) {
                        const AVCodecHWConfig *config = avcodec_get_hw_config(codec, configIndex);
                        if (!config) {
                            hwDecoderEnabled = false;
                            GuiInterface::Instance().PutLog(LogLevel::Error,
                                                            "AVCodecHWConfig is null, disabling hardware decoder");
                            break;
                        }

                        if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX &&
                            config->device_type == hwDecoderType) {
                            hwPixFmt = config->pix_fmt;

                            std::ostringstream oss;
                            oss << "Hardware acceleration pixel format: " << hwPixFmt;
                            GuiInterface::Instance().PutLog(LogLevel::Info, oss.str());

                            break;
                        }
                    }
                }

                pVideoCodecCtx = avcodec_alloc_context3(codec);
                if (pVideoCodecCtx) {
                    if (hwDecoderEnabled) {
                        hwDecoderEnabled = initHwDecoder(pVideoCodecCtx, hwDecoderType);

                        if (!hwDecoderEnabled) {
                            GuiInterface::Instance().PutLog(LogLevel::Error, "hwDecoderInit failed");
                        }
                    }

                    if (avcodec_parameters_to_context(pVideoCodecCtx, pFormatCtx->streams[i]->codecpar) >= 0) {
                        res = avcodec_open2(pVideoCodecCtx, codec, nullptr) >= 0;
                        if (res) {
                            width = pVideoCodecCtx->width;
                            height = pVideoCodecCtx->height;
                        }
                    }
                }

                break;
            }
        }

        if (!res) {
            CloseVideo();
        }
    }

    return res;
}

bool FfmpegDecoder::DecodeVideo(const AVPacket *av_pkt, std::shared_ptr<AVFrame> &pOutFrame) {
    bool res = false;

    if (pVideoCodecCtx && av_pkt && pOutFrame) {
        int ret = avcodec_send_packet(pVideoCodecCtx, av_pkt);
        if (ret < 0) {
            char errStr[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, errStr, AV_ERROR_MAX_STRING_SIZE);
            throw std::runtime_error("avcodec_send_packet failed: " + std::string(errStr));
        }

        if (hwDecoderEnabled) {
            // Initialize the hardware frame.
            if (!hwFrame) {
                hwFrame = std::shared_ptr<AVFrame>(av_frame_alloc(), &freeFrame);
            }

            ret = avcodec_receive_frame(pVideoCodecCtx, hwFrame.get());
        } else {
            ret = avcodec_receive_frame(pVideoCodecCtx, pOutFrame.get());
        }

        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            // No output available right now or end of stream
            res = false;
        } else if (ret < 0) {
            char errStr[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, errStr, AV_ERROR_MAX_STRING_SIZE);
            throw std::runtime_error("avcodec_receive_frame failed: " + std::string(errStr));
        } else {
            // Successfully decoded a frame
            res = true;
        }

        if (hwDecoderEnabled) {
            if (dropCurrentVideoFrame) {
                pOutFrame.reset();
                return false;
            }

            // Copy data from the hw surface to the out frame.
            ret = av_hwframe_transfer_data(pOutFrame.get(), hwFrame.get(), 0);

            if (ret < 0) {
                char errStr[AV_ERROR_MAX_STRING_SIZE];
                av_strerror(ret, errStr, AV_ERROR_MAX_STRING_SIZE);
                throw std::runtime_error("av_hwframe_transfer_data failed: " + std::string(errStr));
            }
        }
    }

    return res;
}

bool FfmpegDecoder::OpenAudio() {
    bool res = false;

    if (pFormatCtx) {
        audioStreamIndex = -1;

        for (unsigned int i = 0; i < pFormatCtx->nb_streams; i++) {
            if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
                audioStreamIndex = i;
                const AVCodec *codec = avcodec_find_decoder(pFormatCtx->streams[i]->codecpar->codec_id);

                if (codec) {
                    pAudioCodecCtx = avcodec_alloc_context3(codec);
                    if (pAudioCodecCtx) {
                        if (avcodec_parameters_to_context(pAudioCodecCtx, pFormatCtx->streams[i]->codecpar) >= 0) {
                            res = avcodec_open2(pAudioCodecCtx, codec, nullptr) >= 0;
                        }
                    }
                }

                break;
            }
        }

        if (!res) {
            CloseAudio();
        }
    }

    return res;
}

void FfmpegDecoder::CloseVideo() {
    if (pVideoCodecCtx) {
        avcodec_free_context(&pVideoCodecCtx);
        pVideoCodecCtx = nullptr;
        videoStreamIndex = 0;
    }
}

void FfmpegDecoder::CloseAudio() {
    if (pAudioCodecCtx) {
        avcodec_free_context(&pAudioCodecCtx);
        pAudioCodecCtx = nullptr;
        audioStreamIndex = 0;
    }
}

int FfmpegDecoder::DecodeAudio(int nStreamIndex, const AVPacket *av_pkt, uint8_t *pOutBuffer, size_t nOutBufferSize) {
    size_t decodedSize = 0;

    int packetSize = av_pkt->size;
    const uint8_t *pPacketData = av_pkt->data;

    while (packetSize > 0) {
        size_t sizeToDecode = nOutBufferSize;
        uint8_t *pDest = pOutBuffer + decodedSize;
        AVFrame *audioFrame = av_frame_alloc();
        if (!audioFrame) {
            throw std::runtime_error("Failed to allocate audio frame");
        }

        int packetDecodedSize = avcodec_receive_frame(pAudioCodecCtx, audioFrame);

        if (packetDecodedSize >= 0) {
            if (audioFrame->format != AV_SAMPLE_FMT_S16) {
                // Convert frame to AV_SAMPLE_FMT_S16 if needed
                if (!swrCtx) {
                    SwrContext *ptr = nullptr;
                    swr_alloc_set_opts2(&ptr,
                                        &pAudioCodecCtx->ch_layout,
                                        AV_SAMPLE_FMT_S16,
                                        pAudioCodecCtx->sample_rate,
                                        &pAudioCodecCtx->ch_layout,
                                        static_cast<AVSampleFormat>(audioFrame->format),
                                        pAudioCodecCtx->sample_rate,
                                        0,
                                        nullptr);

                    auto ret = swr_init(ptr);
                    if (ret < 0) {
                        char errStr[AV_ERROR_MAX_STRING_SIZE];
                        av_strerror(ret, errStr, AV_ERROR_MAX_STRING_SIZE);
                        throw std::runtime_error("Decoding audio  failed: " + std::string(errStr));
                    }
                    swrCtx = std::shared_ptr<SwrContext>(ptr, &freeSwrCtx);
                }

                // Convert audio frame to S16 format
                int samples = swr_convert(swrCtx.get(),
                                          &pDest,
                                          audioFrame->nb_samples,
                                          (const uint8_t **)audioFrame->data,
                                          audioFrame->nb_samples);
                sizeToDecode =
                    samples * pAudioCodecCtx->ch_layout.nb_channels * av_get_bytes_per_sample(AV_SAMPLE_FMT_S16);
            } else {
                // Copy S16 audio data directly
                sizeToDecode = av_samples_get_buffer_size(nullptr,
                                                          pAudioCodecCtx->ch_layout.nb_channels,
                                                          audioFrame->nb_samples,
                                                          AV_SAMPLE_FMT_S16,
                                                          1);
                memcpy(pDest, audioFrame->data[0], sizeToDecode);
            }
        }

        av_frame_free(&audioFrame);

        if (packetDecodedSize < 0) {
            decodedSize = 0;
            break;
        }

        packetSize -= packetDecodedSize;
        pPacketData += packetDecodedSize;

        if (sizeToDecode <= 0) {
            continue;
        }

        decodedSize += sizeToDecode;
    }

    return decodedSize;
}

void FfmpegDecoder::writeAudioBuff(uint8_t *aSample, size_t aSize) {
    std::lock_guard lck(abBuffMtx);
    if (av_fifo_can_write(audioFifoBuffer.get()) < aSize) {
        std::vector<uint8_t> tmp;
        tmp.resize(aSize);
        av_fifo_read(audioFifoBuffer.get(), tmp.data(), aSize);
    }
    av_fifo_write(audioFifoBuffer.get(), aSample, aSize);
}

size_t FfmpegDecoder::ReadAudioBuff(uint8_t *aSample, size_t aSize) {
    std::lock_guard lck(abBuffMtx);
    if (av_fifo_elem_size(audioFifoBuffer.get()) < aSize) {
        return 0;
    }
    av_fifo_read(audioFifoBuffer.get(), aSample, aSize);
    return aSize;
}

void FfmpegDecoder::ClearAudioBuff() {
    std::lock_guard lck(abBuffMtx);
    av_fifo_reset2(audioFifoBuffer.get());
}
