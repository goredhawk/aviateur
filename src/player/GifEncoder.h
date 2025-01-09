//
// Created by liangzhuohua on 2022/4/22.
//

#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "ffmpegInclude.h"

class GifEncoder {
public:
    ~GifEncoder();
    // 初始化编码器
    bool open(int width, int height, AVPixelFormat pixelFormat, int frameRate, const std::string &outputPath);
    // 编码帧
    bool encodeFrame(const std::shared_ptr<AVFrame> &frame);
    // 关闭编码器
    std::string close();
    // 帧率
    int getFrameRate() const {
        return _frameRate;
    }
    // 上次编码时间
    uint64_t getLastEncodeTime() const {
        return _lastEncodeTime;
    }
    // 是否已经打开
    bool isOpened();

    std::string _saveFilePath;

protected:
    std::mutex _encodeMtx;
    // 编码上下文
    std::shared_ptr<AVFormatContext> _formatCtx;
    // 编码上下文
    std::shared_ptr<AVCodecContext> _codecCtx;
    // 色彩空间转换
    SwsContext *_imgConvertCtx{};
    // 颜色转换临时frame
    std::shared_ptr<AVFrame> _tmpFrame;
    std::vector<uint8_t> _buff;
    // 最后编码时间
    uint64_t _lastEncodeTime = 0;
    // 帧率
    int _frameRate = 0;
    // 是否打开
    volatile bool _opened = false;
};
