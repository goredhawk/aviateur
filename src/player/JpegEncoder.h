//
// Created by liangzhuohua on 2022/2/28.
//

#ifndef CTRLCENTER_JPEGENCODER_H
#define CTRLCENTER_JPEGENCODER_H
#include "ffmpegInclude.h"
#include <memory>
#include <string>

class JpegEncoder {
public:
    static bool encodeJpeg(const std::string &outFilePath, const std::shared_ptr<AVFrame> &frame);
};

#endif // CTRLCENTER_JPEGENCODER_H
