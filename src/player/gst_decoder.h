#pragma once

#ifdef AVIATEUR_USE_GSTREAMER

    #include <gst/gst.h>

    #include <string>

class GstDecoder {
public:
    GstDecoder() = default;

    void init();

    void create_pipeline(const std::string& codec);

    void play_pipeline(const std::string& uri);

    void stop_pipeline();

    std::string decoder_name_;

private:
    GstElement* pipeline_{};

    bool initialized_ = false;
};

#endif
