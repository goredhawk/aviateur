#include "RealTimePlayer.h"
#include "JpegEncoder.h"
#include <future>
#include <sstream>

// GIF默认帧率
#define DEFAULT_GIF_FRAMERATE 10

RealTimePlayer::RealTimePlayer(std::shared_ptr<Pathfinder::Device> device, std::shared_ptr<Pathfinder::Queue> queue) {
    m_yuv_renderer = std::make_shared<YuvRenderer>(device, queue);
}

void RealTimePlayer::update(float delta) {
    if (m_infoChanged) {
        m_yuv_renderer->updateTextureInfo(m_videoWidth, m_videoHeight, m_videoFormat);
        m_infoChanged = false;
    }

    bool got = false;
    std::shared_ptr<AVFrame> frame = getFrame(got);
    if (got && frame->linesize[0]) {
        m_yuv_renderer->updateTextureData(frame);
    }
}

std::shared_ptr<AVFrame> RealTimePlayer::getFrame(bool &got) {
    got = false;

    std::lock_guard lck(mtx);
    // 帧缓冲区已被清空,跳过渲染
    if (videoFrameQueue.empty()) {
        return {};
    }
    // 从帧缓冲区取出帧
    std::shared_ptr<AVFrame> frame = videoFrameQueue.front();
    got = true;
    // 缓冲区出队被渲染的帧
    videoFrameQueue.pop();

    // 缓冲，追帧机制
    _lastFrame = frame;
    return frame;
}

void RealTimePlayer::onVideoInfoReady(int width, int height, int format) {
    if (m_videoWidth != width) {
        m_videoWidth = width;
        makeInfoDirty(true);
    }
    if (m_videoHeight != height) {
        m_videoHeight = height;
        makeInfoDirty(true);
    }
    if (m_videoFormat != format) {
        m_videoFormat = format;
        makeInfoDirty(true);
    }
}

void RealTimePlayer::play(const std::string &playUrl) {
    playStop = false;

    if (analysisThread.joinable()) {
        analysisThread.join();
    }

    url = playUrl;

    analysisThread = std::thread([this]() {
        auto decoder_ = std::make_shared<FFmpegDecoder>();

        // 打开并分析输入
        bool ok = decoder_->OpenInput(url);
        if (!ok) {
            // emit onError("视频加载出错", -2);
            return;
        }
        decoder = decoder_;

        decodeThread = std::thread([this]() {
            while (!playStop) {
                try {
                    // Getting frame.
                    auto frame = decoder->GetNextFrame();
                    if (!frame) {
                        continue;
                    }

                    {
                        // Push frame to the buffer queue.
                        std::lock_guard lck(mtx);
                        if (videoFrameQueue.size() > 10) {
                            videoFrameQueue.pop();
                        }
                        videoFrameQueue.push(frame);
                    }
                } catch (const std::exception &e) {
                    // emit onError(e.what(), -2);
                    // Error, stop.
                    break;
                }
            }
            playStop = true;
            // 解码已经停止，触发信号
            // emit onPlayStopped();
        });

        // Start decode thread.
        decodeThread.detach();

        if (!isMuted && decoder->HasAudio()) {
            // 开启音频
            enableAudio();
        }
        // 是否存在音频
        // emit onHasAudio(decoder->HasAudio());

        if (decoder->HasVideo()) {
            onVideoInfoReady(decoder->GetWidth(), decoder->GetHeight(), decoder->GetVideoFrameFormat());
        }

        // 码率计算回调
        // decoder->onBitrate = [this](uint64_t bitrate) { emit onBitrate(static_cast<long>(bitrate)); };
    });

    // Start analysis thread.
    analysisThread.detach();
}

void RealTimePlayer::stop() {
    playStop = true;

    if (decoder && decoder->pFormatCtx) {
        decoder->pFormatCtx->interrupt_callback.callback = [](void *) { return 1; };
    }
    if (analysisThread.joinable()) {
        analysisThread.join();
    }
    if (decodeThread.joinable()) {
        decodeThread.join();
    }
    while (!videoFrameQueue.empty()) {
        std::lock_guard lck(mtx);
        // 清空缓冲
        videoFrameQueue.pop();
    }
    // SDL_CloseAudio();
    if (decoder) {
        decoder->CloseInput();
    }
}

void RealTimePlayer::setMuted(bool muted) {
    if (!decoder->HasAudio()) {
        return;
    }
    if (!muted && decoder) {
        decoder->ClearAudioBuff();
        // 初始化声音
        if (!enableAudio()) {
            return;
        }
    } else {
        disableAudio();
    }
    isMuted = muted;
    // emit onMutedChanged(muted);
}

RealTimePlayer::~RealTimePlayer() {
    stop();
}

std::string RealTimePlayer::captureJpeg() {
    if (!_lastFrame) {
        return "";
    }
    // std::string dirPath = QFileInfo("jpg/l").absolutePath();
    // QDir dir(dirPath);
    // if (!dir.exists()) {
    //     dir.mkpath(dirPath);
    // }
    // stringstream ss;
    // ss << "jpg/";
    // ss << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
    //           .count()
    //    << ".jpg";
    // auto ok = JpegEncoder::encodeJpeg(ss.str(), _lastFrame);
    // // 截图
    // return ok ? std::string(ss.str().c_str()) : "";
}

bool RealTimePlayer::startRecord() {
    if (playStop && !_lastFrame) {
        return false;
    }
    // std::string dirPath = QFileInfo("mp4/l").absolutePath();
    // QDir dir(dirPath);
    // if (!dir.exists()) {
    //     dir.mkpath(dirPath);
    // }
    // // 保存路径
    // stringstream ss;
    // ss << "mp4/";
    // ss << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
    //           .count()
    //    << ".mp4";
    // // 创建MP4编码器
    // _mp4Encoder = make_shared<Mp4Encoder>(ss.str());
    //
    // // 添加音频流
    // if (decoder->HasAudio()) {
    //     _mp4Encoder->addTrack(decoder->pFormatCtx->streams[decoder->audioStreamIndex]);
    // }
    // // 添加视频流
    // if (decoder->HasVideo()) {
    //     _mp4Encoder->addTrack(decoder->pFormatCtx->streams[decoder->videoStreamIndex]);
    // }
    // if (!_mp4Encoder->start()) {
    //     return false;
    // }
    // // 设置获得NALU回调
    // decoder->_gotPktCallback = [this](const std::shared_ptr<AVPacket> &packet) {
    //     // 输入编码器
    //     _mp4Encoder->writePacket(packet, packet->stream_index == decoder->videoStreamIndex);
    // };
    // // 启动编码器
    // return true;
}

std::string RealTimePlayer::stopRecord() {
    if (!_mp4Encoder) {
        return {};
    }
    _mp4Encoder->stop();
    decoder->_gotPktCallback = nullptr;
    return { _mp4Encoder->_saveFilePath.c_str() };
}

int RealTimePlayer::getVideoWidth() {
    if (!decoder) {
        return 0;
    }
    return decoder->width;
}

int RealTimePlayer::getVideoHeight() {
    if (!decoder) {
        return 0;
    }
    return decoder->height;
}

bool RealTimePlayer::enableAudio() {
    if (!decoder->HasAudio()) {
        return false;
    }
    // 音频参数
    // SDL_AudioSpec audioSpec;
    // audioSpec.freq = decoder->GetAudioSampleRate();
    // audioSpec.format = AUDIO_S16;
    // audioSpec.channels = decoder->GetAudioChannelCount();
    // audioSpec.silence = 1;
    // audioSpec.samples = decoder->GetAudioFrameSamples();
    // audioSpec.padding = 0;
    // audioSpec.size = 0;
    // audioSpec.userdata = this;
    // // 音频样本读取回调
    // audioSpec.callback = [](void *Thiz, Uint8 *stream, int len) {
    //     auto *pThis = static_cast<RealTimePlayer *>(Thiz);
    //     SDL_memset(stream, 0, len);
    //     pThis->decoder->ReadAudioBuff(stream, len);
    //     if (pThis->isMuted) {
    //         SDL_memset(stream, 0, len);
    //     }
    // };
    // // 关闭音频
    // SDL_CloseAudio();
    // // 开启声音
    // if (SDL_OpenAudio(&audioSpec, nullptr) == 0) {
    //     // 播放声音
    //     SDL_PauseAudio(0);
    // } else {
    //     // emit onError("开启音频出错，如需听声音请插入音频外设\n" + std::string(SDL_GetError()), -1);
    //     return false;
    // }
    return true;
}

void RealTimePlayer::disableAudio() {
    // SDL_CloseAudio();
}

bool RealTimePlayer::startGifRecord() {
    if (playStop) {
        return false;
    }
    // 保存路径
    std::stringstream ss;
    // ss << QStandardPaths::writableLocation(QStandardPaths::DesktopLocation).toStdString() << "/";
    // ss << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
    //           .count()
    //    << ".gif";
    if (!(decoder && decoder->HasVideo())) {
        return false;
    }
    // 创建gif编码器
    _gifEncoder = std::make_shared<GifEncoder>();
    if (!_gifEncoder->open(
            decoder->width, decoder->height, decoder->GetVideoFrameFormat(), DEFAULT_GIF_FRAMERATE, ss.str())) {
        return false;
    }
    // 设置获得解码帧回调
    decoder->_gotFrameCallback = [this](const std::shared_ptr<AVFrame> &frame) {
        if (!_gifEncoder) {
            return;
        }
        if (!_gifEncoder->isOpened()) {
            return;
        }
        // 根据GIF帧率跳帧
        uint64_t now
            = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
                  .count();
        if (_gifEncoder->getLastEncodeTime() + 1000 / _gifEncoder->getFrameRate() > now) {
            return;
        }
        // 编码
        _gifEncoder->encodeFrame(frame);
    };

    return true;
}

void RealTimePlayer::stopGifRecord() {
    decoder->_gotFrameCallback = nullptr;
    if (!_gifEncoder) {
        return;
    }
    _gifEncoder->close();
}