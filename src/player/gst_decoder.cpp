#include "gst_decoder.h"

#ifdef AVIATEUR_ENABLE_GSTREAMER

    // #include <gst/gl/gl.h>
    #include <gst/video/video.h>

    #include "src/gui_interface.h"

static gboolean gst_bus_cb(GstBus *bus, GstMessage *message, gpointer data) {
    GstBin *pipeline = GST_BIN(data);

    switch (GST_MESSAGE_TYPE(message)) {
        case GST_MESSAGE_ERROR: {
            GError *gerr;
            gchar *debug_msg;
            gst_message_parse_error(message, &gerr, &debug_msg);
            GST_DEBUG_BIN_TO_DOT_FILE(pipeline, GST_DEBUG_GRAPH_SHOW_ALL, "mss-pipeline-ERROR");
            gchar *dot_data = gst_debug_bin_to_dot_data(GST_BIN(pipeline), GST_DEBUG_GRAPH_SHOW_ALL);
            g_free(dot_data);
            g_error("Error: %s (%s)", gerr->message, debug_msg);
            g_error_free(gerr);
            g_free(debug_msg);
        } break;
        case GST_MESSAGE_WARNING: {
            GError *gerr;
            gchar *debug_msg;
            gst_message_parse_warning(message, &gerr, &debug_msg);
            GST_DEBUG_BIN_TO_DOT_FILE(pipeline, GST_DEBUG_GRAPH_SHOW_ALL, "mss-pipeline-WARNING");
            g_warning("Warning: %s (%s)", gerr->message, debug_msg);
            g_error_free(gerr);
            g_free(debug_msg);
        } break;
        case GST_MESSAGE_EOS: {
            g_error("Got EOS!!");
        } break;
        default:
            break;
    }
    return TRUE;
}

void GstDecoder::init() {
    if (initialized_) {
        return;
    }

    initialized_ = true;

    // setenv("GST_DEBUG", "GST_TRACER:7", 1);
    // setenv("GST_TRACERS", "latency(flags=pipeline)", 1); // Latency
    // setenv("GST_DEBUG_FILE", "./latency.log", 1);

    gst_debug_set_default_threshold(GST_LEVEL_WARNING);

    gst_init(NULL, NULL);
}

// static gboolean on_client_draw(GstGLImageSink *sink, GstGLContext *context, GstSample *sample, gpointer data) {
//     GstVideoFrame v_frame;
//     GstVideoInfo v_info;
//     GstBuffer *buf = gst_sample_get_buffer(sample);
//     const GstCaps *caps = gst_sample_get_caps(sample);
//
//     gst_video_info_from_caps(&v_info, caps);
//
//     if (!gst_video_frame_map(&v_frame, &v_info, buf, (GstMapFlags)(GST_MAP_READ | GST_MAP_GL))) {
//         g_warning("Failed to map the video buffer");
//         return TRUE;
//     }
//
//     // Src GL texture
//     guint texture = *(guint *)v_frame.data[0];
//
//     // Use texture
//
//     return TRUE;
// }

void GstDecoder::create_pipeline() {
    if (pipeline_) {
        return;
    }

    GError *error = NULL;

    gchar *pipeline_str = g_strdup_printf(
        "udpsrc name=udpsrc "
        "caps=application/x-rtp,media=(string)video,clock-rate=(int)90000,encoding-name=(string)%s ! "
        "rtpjitterbuffer ! "
        "%s ! "
        "decodebin3 ! "
        "autovideosink name=glsink sync=false",
        GuiInterface::Instance().playerCodec.c_str(),
        GuiInterface::Instance().playerCodec == "H264" ? "rtph264depay" : "rtph265depay");

    pipeline_ = gst_parse_launch(pipeline_str,

                                 &error);
    // pipeline_ = gst_parse_launch(
    //     "udpsrc name=udpsrc ! video/mpegts,systemstream=true,clock-rate=90000 ! "
    //     "tsdemux ! "
    //     "h264parse ! avdec_h264 ! "
    //     "autovideosink name=glsink sync=false",
    //     &error);
    g_assert_no_error(error);

    g_free(pipeline_str);

    // GstElement *glsink = gst_bin_get_by_name(GST_BIN(pipeline_), "glsink");
    // g_signal_connect(glsink, "client-draw", (GCallback)on_client_draw, this);
    // gst_object_unref(glsink);

    GstBus *bus = gst_element_get_bus(pipeline_);
    gst_bus_add_watch(bus, gst_bus_cb, pipeline_);
    gst_clear_object(&bus);
}

void GstDecoder::play_pipeline(const std::string &uri) {
    GstElement *udpsrc = gst_bin_get_by_name(GST_BIN(pipeline_), "udpsrc");

    if (uri.empty()) {
        g_object_set(udpsrc, "port", GuiInterface::Instance().playerPort, NULL);
    } else {
        g_object_set(udpsrc, "uri", uri.c_str(), NULL);
    }

    gst_object_unref(udpsrc);

    g_assert(gst_element_set_state(pipeline_, GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE);
}

void GstDecoder::stop_pipeline() {
    gst_element_send_event(pipeline_, gst_event_new_eos());

    // Wait for an EOS message on the pipeline bus.
    GstMessage *msg = gst_bus_timed_pop_filtered(GST_ELEMENT_BUS(pipeline_),
                                                 GST_CLOCK_TIME_NONE,
                                                 static_cast<GstMessageType>(GST_MESSAGE_EOS | GST_MESSAGE_ERROR));

    // TODO: should check if we got an error message here or an eos.
    (void)msg;

    // Completely stop the pipeline.
    gst_element_set_state(pipeline_, GST_STATE_NULL);
    gst_message_unref(msg);
    gst_object_unref(pipeline_);
    pipeline_ = nullptr;
}

#endif
