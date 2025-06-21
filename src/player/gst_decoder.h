#pragma once

#ifdef AVIATEUR_ENABLE_GSTREAMER

#include <gst/gst.h>

#include <string>

class GstDecoder {
public:
    GstDecoder() = default;

    void init();

    void create_pipeline();

    void play_pipeline(const std::string &uri);

    void stop_pipeline();

private:
    GstElement* pipeline_{};

    bool initialized_ = false;
};

#endif
