#pragma once

#include <common/any_callable.h>

#include <memory>
#include <queue>
#include <thread>

#include "GifEncoder.h"
#include "Mp4Encoder.h"
#include "YuvRenderer.h"
#include "ffmpegDecode.h"

struct SDL_AudioStream;

class RealTimePlayer {
public:
    RealTimePlayer(std::shared_ptr<Pathfinder::Device> device, std::shared_ptr<Pathfinder::Queue> queue);
    ~RealTimePlayer();
    void update(float dt);

    std::shared_ptr<AVFrame> getFrame();

    bool infoDirty() const {
        return infoChanged_;
    }
    void makeInfoDirty(bool dirty) {
        infoChanged_ = dirty;
    }
    int videoWidth() const {
        return videoWidth_;
    }
    int videoHeight() const {
        return videoHeight_;
    }
    int videoFormat() const {
        return videoFormat_;
    }
    bool getMuted() const {
        return isMuted;
    }
    // 播放
    void play(const std::string &playUrl, bool forceSoftwareDecoding);
    // 停止
    void stop();
    // 静音
    void setMuted(bool muted = false);
    // 截图
    std::string captureJpeg();

    // Record MP4
    bool startRecord();
    std::string stopRecord() const;

    // Record GIF
    bool startGifRecord();
    std::string stopGifRecord() const;

    // 获取视频宽度
    int getVideoWidth() const;
    // 获取视频高度
    int getVideoHeight() const;

    void forceSoftwareDecoding(bool force);

    bool isHardwareAccelerated() const;

    std::shared_ptr<FfmpegDecoder> getDecoder() const;

    // Signals

    std::vector<revector::AnyCallable<void>> connectionLostCallbacks;
    void emitConnectionLost();

    // void gotRecordVol(double vol);
    revector::AnyCallable<void> gotRecordVolume;

    // void onBitrate(long bitrate);
    revector::AnyCallable<void> onBitrateUpdate;

    // void onMutedChanged(bool muted);
    revector::AnyCallable<void> onMutedChanged;

    // void onHasAudio(bool has);
    revector::AnyCallable<void> onHasAudio;

protected:
    std::shared_ptr<FfmpegDecoder> decoder;

    // Play file URL
    std::string url;

    volatile bool playStop = true;

    volatile bool isMuted = true;

    SDL_AudioStream *stream;

    std::queue<std::shared_ptr<AVFrame>> videoFrameQueue;

    std::mutex mtx;

    std::thread decodeThread;

    std::thread analysisThread;

    // 最后输出的帧
    std::shared_ptr<AVFrame> lastFrame_;
    // 视频是否ready
    void onVideoInfoReady(int width, int height, int format);

    bool enableAudio();

    void disableAudio();

    std::shared_ptr<Mp4Encoder> mp4Encoder_;

    std::shared_ptr<GifEncoder> gifEncoder_;

    bool hasAudio() const;

    bool forceSoftwareDecoding_ = false;
    bool hwEnabled = false;

public:
    std::shared_ptr<YuvRenderer> yuvRenderer_;
    int videoWidth_{};
    int videoHeight_{};
    int videoFormat_{};
    bool infoChanged_ = false;
};
