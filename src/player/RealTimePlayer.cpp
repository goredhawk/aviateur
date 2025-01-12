#include "RealTimePlayer.h"

#include <future>
#include <sstream>

#include "../gui_interface.h"
#include "JpegEncoder.h"

// GIF默认帧率
#define DEFAULT_GIF_FRAMERATE 10

RealTimePlayer::RealTimePlayer(std::shared_ptr<Pathfinder::Device> device, std::shared_ptr<Pathfinder::Queue> queue) {
    m_yuv_renderer = std::make_shared<YuvRenderer>(device, queue);
    m_yuv_renderer->init();

    connectionLostCallbacks.push_back([this] {
        stop();
        play(url);
    });
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
    // Got a frame?
    got = false;

    std::lock_guard lck(mtx);

    // No frame in the queue
    if (videoFrameQueue.empty()) {
        return {};
    }

    // Get a frame from the queue
    std::shared_ptr<AVFrame> frame = videoFrameQueue.front();
    got = true;

    // Remove the frame from the queue.
    videoFrameQueue.pop();

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

    analysisThread = std::thread([this] {
        auto decoder_ = std::make_shared<FFmpegDecoder>();

        // 打开并分析输入
        bool ok = decoder_->OpenInput(url);
        if (!ok) {
            emitError("Loading URL failed", -2);
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
                    // Decoding stopped.

                    emitError(e.what(), -2);

                    emitConnectionLost();

                    break;
                }
            }
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
        decoder->onBitrate = [this](uint64_t bitrate) { GuiInterface::Instance().WhenBitrateUpdate(bitrate); };
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

    auto absolutePath = std::filesystem::absolute("capture");
    std::string dirPath = absolutePath.parent_path().string();

    try {
        if (!std::filesystem::exists(dirPath)) {
            std::filesystem::create_directories(dirPath);
        }
    } catch (const std::exception &e) {
        std::cerr << e.what() << std::endl;
    }

    std::stringstream filePath;
    filePath << "capture/";
    filePath << std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch())
                    .count()
             << ".jpg";

    std::ofstream outfile (filePath.str());
    outfile.close();

    auto ok = JpegEncoder::encodeJpeg(filePath.str(), _lastFrame);

    return ok ? std::string(filePath.str()) : "";
}

bool RealTimePlayer::startRecord() {
    if (playStop && !_lastFrame) {
        return false;
    }

    auto absolutePath = std::filesystem::absolute("recording");

    try {
        if (!std::filesystem::exists(absolutePath)) {
            std::filesystem::create_directories(absolutePath);
        }
    } catch (const std::exception &e) {
        std::cerr << e.what() << std::endl;
    }

    std::stringstream video_file_path;
    video_file_path << "recording/";
    video_file_path << std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count()
                    << ".mp4";

    std::ofstream outfile (video_file_path.str());
    outfile.close();

    _mp4Encoder = std::make_shared<Mp4Encoder>(video_file_path.str());

    // Audio track not handled for now.
    if (decoder->HasAudio()) {
        _mp4Encoder->addTrack(decoder->pFormatCtx->streams[decoder->audioStreamIndex]);
    }

    // Add video track.
    if (decoder->HasVideo()) {
        _mp4Encoder->addTrack(decoder->pFormatCtx->streams[decoder->videoStreamIndex]);
    }

    if (!_mp4Encoder->start()) {
        return false;
    }

    // 设置获得NALU回调
    decoder->_gotPktCallback = [this](const std::shared_ptr<AVPacket> &packet) {
        // 输入编码器
        _mp4Encoder->writePacket(packet, packet->stream_index == decoder->videoStreamIndex);
    };

    return true;
}

std::string RealTimePlayer::stopRecord() const {
    if (!_mp4Encoder) {
        return {};
    }
    _mp4Encoder->stop();
    decoder->_gotPktCallback = nullptr;

    return _mp4Encoder->_saveFilePath;
}

int RealTimePlayer::getVideoWidth() const {
    if (!decoder) {
        return 0;
    }
    return decoder->width;
}

int RealTimePlayer::getVideoHeight() const {
    if (!decoder) {
        return 0;
    }
    return decoder->height;
}

void RealTimePlayer::emitConnectionLost() {
    for (auto &callback : connectionLostCallbacks) {
        try {
            callback();
        } catch (std::bad_any_cast &) {
            abort();
        }
    }
}

void RealTimePlayer::emitError(std::string msg, int errorCode) {
    GuiInterface::Instance().PutLog(LogLevel::Error, "{%s}. Error code: {%d}", msg.c_str(), errorCode);

    for (auto &callback : errorCallbacks) {
        try {
            callback.operator()<std::string, int>(std::move(msg), std::move(errorCode));
        } catch (std::bad_any_cast &) {
            abort();
        }
    }
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

bool RealTimePlayer::hasAudio() const {
    if (!decoder) {
        return false;
    }
    // No audio for now.
    // return decoder->HasAudio();
    return false;
}

bool RealTimePlayer::startGifRecord() {
    if (playStop) {
        return false;
    }

    if (!(decoder && decoder->HasVideo())) {
        return false;
    }

    std::stringstream gif_file_path;
    gif_file_path << "recording/";
    gif_file_path << std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count()
                  << ".gif";

    _gifEncoder = std::make_shared<GifEncoder>();

    if (!_gifEncoder->open(decoder->width,
                           decoder->height,
                           decoder->GetVideoFrameFormat(),
                           DEFAULT_GIF_FRAMERATE,
                           gif_file_path.str())) {
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
        uint64_t now =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
                .count();
        if (_gifEncoder->getLastEncodeTime() + 1000 / _gifEncoder->getFrameRate() > now) {
            return;
        }
        // 编码
        _gifEncoder->encodeFrame(frame);
    };

    return true;
}

std::string RealTimePlayer::stopGifRecord() const {
    decoder->_gotFrameCallback = nullptr;
    if (!_gifEncoder) {
        return "";
    }
    _gifEncoder->close();
    return _gifEncoder->_saveFilePath;
}
