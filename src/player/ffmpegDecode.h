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

    bool OpenInput(std::string &inputFile);

    bool CloseInput();

    std::shared_ptr<AVFrame> GetNextFrame();

    int GetWidth() const {
        return width;
    }

    int GetHeight() const {
        return height;
    }

    double GetFps() const {
        return videoFramePerSecond;
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
        if (isHwDecoderEnable) {
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

    // 初始化硬件解码器
    bool hwDecoderInit(AVCodecContext *ctx, enum AVHWDeviceType type);

    std::chrono::time_point<std::chrono::steady_clock> startTime;

    // ffmpeg 解封装上下文
    AVFormatContext *pFormatCtx = nullptr;

    // ffmpeg 视频编码上下文
    AVCodecContext *pVideoCodecCtx = nullptr;

    // ffmpeg音频编码上下文
    AVCodecContext *pAudioCodecCtx = nullptr;

    // ffmpeg 音频样本格式转换
    std::shared_ptr<SwrContext> swrCtx;

    // 视频轨道顺序
    int videoStreamIndex = -1;

    // 音轨顺序
    int audioStreamIndex = -1;

    // 输入源是否成功打开
    volatile bool isOpen = false;

    // Video 帧率
    double videoFramePerSecond = 0;

    // ffmpeg 视频时间基
    double videoBaseTime = 0;

    // ffmpeg 音频时间基
    double audioBaseTime = 0;

    // ffmpeg 视频格式转换
    SwsContext *pImgConvertCtx = nullptr;

    // 解码器全局释放锁
    std::mutex _releaseLock;

    // 是否存在视频流
    bool hasVideoStream{};
    // 是否存在音频流
    bool hasAudioStream{};

    // 视频宽度
    int width{};

    // 视频高度
    int height{};

    void emitOnBitrate(uint64_t pBitrate) {
        onBitrate(pBitrate);
    }

    volatile uint64_t bytesSecond = 0;
    uint64_t bitrate = 0;
    uint64_t lastCountBitrateTime = 0;
    std::function<void(uint64_t bitrate)> onBitrate;

    // 音频队列
    std::mutex abBuffMtx;
    std::shared_ptr<AVFifo> audioFifoBuffer;

    // 硬件解码
    AVHWDeviceType hwDecoderType;
    bool isHwDecoderEnable = false;
    AVPixelFormat hwPixFmt;
    AVBufferRef *hwDeviceCtx = nullptr;
    volatile bool dropCurrentVideoFrame = false;
    // Hardware frame
    std::shared_ptr<AVFrame> hwFrame;
};
