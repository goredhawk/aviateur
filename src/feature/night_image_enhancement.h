#pragma once

#include <opencv2/dnn.hpp>

class PairLIE {
public:
    PairLIE(const std::string& modelPath, float exposure = 0.5);
    cv::Mat detect(const cv::Mat& grayImg);

private:
    int inpWidth;
    int inpHeight;
    cv::Mat exposure_;
    cv::dnn::Net net;
};
