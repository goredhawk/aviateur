#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <string>

#include "ffmpegInclude.h"

class QQuickRealTimePlayer;

class FFmpegDecoder {
    friend class RealTimePlayer;

public:
    FFmpegDecoder() = default;

    ~FFmpegDecoder() {
        CloseInput();
    }

    bool OpenInput(std::string &inputFile, bool forceSoftwareDecoding);

    bool CloseInput();

    std::shared_ptr<AVFrame> GetNextFrame();

    int GetWidth() const {
        return width;
    }

    int GetHeight() const {
        return height;
    }

    double GetFps() const {
        return videoFps;
    }

    bool HasAudio() const {
        return hasAudioStream;
    }

    bool HasVideo() const {
        return hasVideoStream;
    }

    size_t ReadAudioBuff(uint8_t *aSample, size_t aSize);

    void ClearAudioBuff();

    int GetAudioSampleRate() const {
        return pAudioCodecCtx->sample_rate;
    }

    int GetAudioChannelCount() const {
        return pAudioCodecCtx->ch_layout.nb_channels;
    }

    AVSampleFormat GetAudioSampleFormat() const {
        return AV_SAMPLE_FMT_S16;
    }

    AVPixelFormat GetVideoFrameFormat() const {
        if (hwDecoderEnabled) {
            return AV_PIX_FMT_NV12;
        }
        return pVideoCodecCtx->pix_fmt;
    }

    int GetAudioFrameSamples() const {
        return pAudioCodecCtx->sample_rate * 2 / 25;
    }

private:
    bool OpenVideo();

    bool OpenAudio();

    void CloseVideo();

    void CloseAudio();

    int DecodeAudio(int nStreamIndex, const AVPacket *avpkt, uint8_t *pOutBuffer, size_t nOutBufferSize);

    bool DecodeVideo(const AVPacket *avpkt, std::shared_ptr<AVFrame> &pOutFrame);

    void writeAudioBuff(uint8_t *aSample, size_t aSize);

    std::function<void(const std::shared_ptr<AVPacket> &packet)> _gotPktCallback = nullptr;

    std::function<void(const std::shared_ptr<AVFrame> &frame)> _gotFrameCallback = nullptr;

    bool initHwDecoder(AVCodecContext *ctx, enum AVHWDeviceType type);

    std::chrono::time_point<std::chrono::steady_clock> startTime;

    AVFormatContext *pFormatCtx = nullptr;

    AVCodecContext *pVideoCodecCtx = nullptr;

    AVCodecContext *pAudioCodecCtx = nullptr;

    // ffmpeg 音频样本格式转换
    std::shared_ptr<SwrContext> swrCtx;

    int videoStreamIndex = -1;

    int audioStreamIndex = -1;

    volatile bool sourceIsOpened = false;

    double videoFps = 0;

    double videoBaseTime = 0;

    double audioBaseTime = 0;

    SwsContext *pImgConvertCtx = nullptr;

    std::mutex _releaseLock;

    bool hasVideoStream{};

    bool hasAudioStream{};

    int width{};

    int height{};

    void emitOnBitrate(uint64_t pBitrate) {
        onBitrate(pBitrate);
    }

    volatile uint64_t bytesSecond = 0;
    uint64_t bitrate = 0;
    uint64_t lastCountBitrateTime = 0;
    std::function<void(uint64_t bitrate)> onBitrate;

    // Audio buffer
    std::mutex abBuffMtx;
    std::shared_ptr<AVFifo> audioFifoBuffer;

    // Hardware decoding
    AVHWDeviceType hwDecoderType;
    // If a hardware decoder is being used.
    bool hwDecoderEnabled = false;
    AVPixelFormat hwPixFmt;
    AVBufferRef *hwDeviceCtx = nullptr;
    volatile bool dropCurrentVideoFrame = false;
    std::shared_ptr<AVFrame> hwFrame;
};
