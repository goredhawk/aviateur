#pragma once

#include "YuvRenderer.h"
#include "ffmpegDecode.h"
#include <memory>
#include <queue>
#include <thread>

#include "GifEncoder.h"
#include "Mp4Encoder.h"

class TItemRender;

class RealTimePlayer {
public:
    RealTimePlayer(std::shared_ptr<Pathfinder::Device> device, std::shared_ptr<Pathfinder::Queue> queue);
    ~RealTimePlayer();
    void update(float delta);

    std::shared_ptr<AVFrame> getFrame(bool &got);

    bool infoDirty() const { return m_infoChanged; }
    void makeInfoDirty(bool dirty) { m_infoChanged = dirty; }
    int videoWidth() const { return m_videoWidth; }
    int videoHeight() const { return m_videoHeight; }
    int videoFormat() const { return m_videoFormat; }
    bool getMuted() const { return isMuted; }
    // 播放
    void play(const std::string &playUrl);
    // 停止
    void stop();
    // 静音
    void setMuted(bool muted = false);
    // 截图
    std::string captureJpeg();
    // 录像
    bool startRecord();
    std::string stopRecord();
    // 录制GIF
    bool startGifRecord();
    void stopGifRecord();
    // 获取视频宽度
    int getVideoWidth();
    // 获取视频高度
    int getVideoHeight();

    // Signals
    // 播放已经停止
    void onPlayStopped();
    // 出错
    void onError(std::string msg, int code);
    // 获取录音音量
    void gotRecordVol(double vol);
    // 获得码率
    void onBitrate(long bitrate);
    // 静音
    void onMutedChanged(bool muted);
    // 是否有音频
    void onHasAudio(bool has);

    friend class TItemRender;

protected:
    // ffmpeg解码器
    std::shared_ptr<FFmpegDecoder> decoder;
    // 播放地址
    std::string url;
    // 播放标记位
    volatile bool playStop = true;
    // 静音标记位
    volatile bool isMuted = true;
    // 帧队列
    std::queue<std::shared_ptr<AVFrame>> videoFrameQueue;
    std::mutex mtx;
    // 解码线程
    std::thread decodeThread;
    // 分析线程
    std::thread analysisThread;
    // 最后输出的帧
    std::shared_ptr<AVFrame> _lastFrame;
    // 视频是否ready
    void onVideoInfoReady(int width, int height, int format);
    // 播放音频
    bool enableAudio();
    // 停止播放音频
    void disableAudio();
    // MP4录制器
    std::shared_ptr<Mp4Encoder> _mp4Encoder;
    // GIF录制器
    std::shared_ptr<GifEncoder> _gifEncoder;
    // 是否有声音
    bool hasAudio() {
        if (!decoder) {
            return false;
        }
        return decoder->HasAudio();
    }

public:
    std::shared_ptr<YuvRenderer> m_yuv_renderer;
    int m_videoWidth {};
    int m_videoHeight {};
    int m_videoFormat {};
    bool m_infoChanged = false;
};
