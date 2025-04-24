#include "player_rect.h"

#include "../gui_interface.h"

class SignalBar : public revector::ProgressBar {
    void custom_update(double dt) override {
        if (value < 33.3) {
            theme_progress->bg_color = RED;
        }
        if (value > 33.3 && value < 66.67) {
            theme_progress->bg_color = YELLOW;
        }
        if (value > 66.67) {
            theme_progress->bg_color = GREEN;
        }
    }
};

void PlayerRect::show_red_tip(std::string tip) {
    tip_label_->set_text_style(revector::TextStyle{RED});
    tip_label_->show_tip(tip);
}

void PlayerRect::show_green_tip(std::string tip) {
    tip_label_->set_text_style(revector::TextStyle{GREEN});
    tip_label_->show_tip(tip);
}

void PlayerRect::custom_input(revector::InputEvent &event) {
    auto input_server = revector::InputServer::get_singleton();

    if (event.type == revector::InputEventType::Key) {
        auto key_args = event.args.key;

        if (key_args.key == revector::KeyCode::F11) {
            if (key_args.pressed) {
                fullscreen_button_->press();
            }
        }

        if (playing_ && key_args.key == revector::KeyCode::F10) {
            if (key_args.pressed) {
                record_button_->press();
            }
        }
    }
}

void PlayerRect::custom_ready() {
    auto onRtpStream = [this](std::string sdp_file) {
        playing_file_ = sdp_file;
        start_playing(sdp_file);
    };
    GuiInterface::Instance().rtpStreamCallbacks.emplace_back(onRtpStream);

    collapse_panel_ = std::make_shared<revector::CollapseContainer>();
    collapse_panel_->set_title(FTR("player control"));
    collapse_panel_->set_collapse(true);
    collapse_panel_->set_color(revector::ColorU(84, 138, 247));
    collapse_panel_->set_anchor_flag(revector::AnchorFlag::TopRight);
    collapse_panel_->set_visibility(false);
    add_child(collapse_panel_);

    auto vbox = std::make_shared<revector::VBoxContainer>();
    collapse_panel_->add_child(vbox);

    logo_ = std::make_shared<revector::VectorImage>(revector::get_asset_dir("openipc-logo-white.svg"));
    texture = logo_;

    auto render_server = revector::RenderServer::get_singleton();
    player_ = std::make_shared<RealTimePlayer>(render_server->device_, render_server->queue_);

    render_image_ = std::make_shared<revector::RenderImage>(Pathfinder::Vec2I{1920, 1080});

    set_stretch_mode(StretchMode::KeepAspectCentered);

    tip_label_ = std::make_shared<TipLabel>();
    tip_label_->set_anchor_flag(revector::AnchorFlag::Center);
    tip_label_->set_visibility(false);
    tip_label_->set_text_style(revector::TextStyle{revector::ColorU::red()});
    add_child(tip_label_);

    hud_container_ = std::make_shared<revector::HBoxContainer>();
    add_child(hud_container_);
    hud_container_->set_size({0, 48});
    revector::StyleBox box;
    box.bg_color = revector::ColorU(27, 27, 27, 27);
    box.border_width = 0;
    box.corner_radius = 0;
    hud_container_->set_theme_bg(box);
    hud_container_->set_anchor_flag(revector::AnchorFlag::BottomWide);
    hud_container_->set_visibility(false);
    hud_container_->set_separation(16);

    {
        video_info_label_ = std::make_shared<revector::Label>();
        hud_container_->add_child(video_info_label_);
        video_info_label_->set_text_style(revector::TextStyle{revector::ColorU::white()});
        video_info_label_->set_text("");

        auto onFpsUpdate = [this](uint32_t width, uint32_t height, float fps) {
            std::stringstream ss;
            ss << width << "x" << height << "@" << int(round(fps));
            video_info_label_->set_text(ss.str());
        };
        GuiInterface::Instance().decoderReadyCallbacks.emplace_back(onFpsUpdate);
    }

    bitrate_label_ = std::make_shared<revector::Label>();
    hud_container_->add_child(bitrate_label_);
    bitrate_label_->set_text(FTR("bit rate") + ": 0 bps");
    bitrate_label_->set_text_style(revector::TextStyle{revector::ColorU::white()});

    {
        display_fps_label_ = std::make_shared<revector::Label>();
        hud_container_->add_child(display_fps_label_);
        display_fps_label_->set_text_style(revector::TextStyle{revector::ColorU::white()});
        display_fps_label_->set_text(FTR("display fps") + ":");
    }

    hw_status_label_ = std::make_shared<revector::Label>();
    hud_container_->add_child(hw_status_label_);
    hw_status_label_->set_text_style(revector::TextStyle{revector::ColorU::white()});

    auto rssi_label = std::make_shared<revector::Label>();
    hud_container_->add_child(rssi_label);
    rssi_label->set_text("RSSI");
    rssi_label->set_text_style(revector::TextStyle{revector::ColorU::white()});

    rssi_bar_ = std::make_shared<SignalBar>();
    hud_container_->add_child(rssi_bar_);
    rssi_bar_->set_lerp_enabled(true);
    rssi_bar_->set_custom_minimum_size({64, 16});
    rssi_bar_->set_label_visibility(false);
    rssi_bar_->container_sizing.expand_v = false;
    rssi_bar_->container_sizing.flag_v = revector::ContainerSizingFlag::ShrinkCenter;

    auto snr_label = std::make_shared<revector::Label>();
    hud_container_->add_child(snr_label);
    snr_label->set_text("SNR");
    snr_label->set_text_style(revector::TextStyle{revector::ColorU::white()});

    snr_bar_ = std::make_shared<SignalBar>();
    hud_container_->add_child(snr_bar_);
    snr_bar_->set_lerp_enabled(true);
    snr_bar_->set_custom_minimum_size({64, 16});
    snr_bar_->set_label_visibility(false);
    snr_bar_->container_sizing.expand_v = false;
    snr_bar_->container_sizing.flag_v = revector::ContainerSizingFlag::ShrinkCenter;

    rx_status_update_timer = std::make_shared<revector::Timer>();
    add_child(rx_status_update_timer);

    auto callback = [this] {
        rssi_bar_->set_value(
            (GuiInterface::Instance().rx_status_.rssi[0] + GuiInterface::Instance().rx_status_.rssi[1]) / 2);
        snr_bar_->set_value((GuiInterface::Instance().rx_status_.snr[0] + GuiInterface::Instance().rx_status_.snr[1]) /
                            2);

        rx_status_update_timer->start_timer(0.1);
    };
    rx_status_update_timer->connect_signal("timeout", callback);
    rx_status_update_timer->start_timer(0.1);

    record_status_label_ = std::make_shared<revector::Label>();
    hud_container_->add_child(record_status_label_);
    record_status_label_->container_sizing.expand_h = true;
    record_status_label_->container_sizing.flag_h = revector::ContainerSizingFlag::ShrinkEnd;
    record_status_label_->set_text("");
    record_status_label_->set_text_style(revector::TextStyle{revector::ColorU::white()});

    auto capture_button = std::make_shared<revector::Button>();
    vbox->add_child(capture_button);
    capture_button->set_text(FTR("capture frame"));
    auto icon = std::make_shared<revector::VectorImage>(revector::get_asset_dir("CaptureImage.svg"));
    capture_button->set_icon_normal(icon);
    auto capture_callback = [this] {
        auto output_file = player_->captureJpeg();
        if (output_file.empty()) {
            show_red_tip(FTR("capture fail"));
        } else {
            show_green_tip(FTR("frame saved") + output_file);
        }
    };
    capture_button->connect_signal("pressed", capture_callback);

    record_button_ = std::make_shared<revector::Button>();
    vbox->add_child(record_button_);
    auto icon2 = std::make_shared<revector::VectorImage>(revector::get_asset_dir("RecordVideo.svg"));
    record_button_->set_icon_normal(icon2);
    record_button_->set_text(FTR("record mp4") + " (F10)");

    auto record_button_raw = record_button_.get();
    auto record_callback = [record_button_raw, this] {
        if (!is_recording) {
            is_recording = player_->startRecord();

            if (is_recording) {
                record_button_raw->set_text(FTR("stop recording") + " (F10)");

                record_start_time = std::chrono::steady_clock::now();

                record_status_label_->set_text(FTR("recording") + ": 00:00");
            } else {
                record_status_label_->set_text("");
                show_red_tip(FTR("record fail"));
            }
        } else {
            is_recording = false;

            auto output_file = player_->stopRecord();

            record_button_raw->set_text(FTR("record mp4") + " (F10)");
            record_status_label_->set_text("");

            if (output_file.empty()) {
                show_red_tip(FTR("save record fail"));
            } else {
                show_green_tip(FTR("video saved") + output_file);
            }
        }
    };
    record_button_->connect_signal("pressed", record_callback);

    {
        video_stabilization_button_ = std::make_shared<revector::CheckButton>();
        video_stabilization_button_->set_text(FTR("video stab"));
        vbox->add_child(video_stabilization_button_);

        auto callback = [this](bool toggled) {
            player_->yuvRenderer_->mStabilize = toggled;
            if (toggled) {
                show_red_tip(FTR("video stab warning"));
            }
        };
        video_stabilization_button_->connect_signal("toggled", callback);
    }

    {
        low_light_enhancement_button_simple_ = std::make_shared<revector::CheckButton>();
        low_light_enhancement_button_simple_->set_text(FTR("low light enhancement simple"));
        vbox->add_child(low_light_enhancement_button_simple_);

        auto callback = [this](bool toggled) {
            player_->yuvRenderer_->mLowLightEnhancementSimple = toggled;
            if (toggled) {
                if (low_light_enhancement_button_advanced_->get_pressed()) {
                    low_light_enhancement_button_advanced_->press();
                }
            }
        };
        low_light_enhancement_button_simple_->connect_signal("toggled", callback);
    }

    {
        low_light_enhancement_button_advanced_ = std::make_shared<revector::CheckButton>();
        low_light_enhancement_button_advanced_->set_text(FTR("low light enhancement dnn"));
        vbox->add_child(low_light_enhancement_button_advanced_);

        auto callback = [this](bool toggled) {
            player_->yuvRenderer_->mLowLightEnhancementAdvanced = toggled;
            if (toggled) {
                if (low_light_enhancement_button_simple_->get_pressed()) {
                    low_light_enhancement_button_simple_->press();
                }
            }
        };
        low_light_enhancement_button_advanced_->connect_signal("toggled", callback);
    }

    {
        auto button = std::make_shared<revector::CheckButton>();
        button->set_text(FTR("sw decoding"));
        vbox->add_child(button);

        auto callback = [this](bool toggled) {
            force_software_decoding = toggled;
            if (playing_) {
                player_->stop();
                player_->play(playing_file_, force_software_decoding);
            }
        };
        button->connect_signal("toggled", callback);
    }

    auto onBitrateUpdate = [this](uint64_t bitrate) {
        std::string text = FTR("bit rate") + ": ";
        if (bitrate > 1024 * 1024) {
            text += std::format("{:.1f}", bitrate / 1024.0 / 1024.0) + " Mbps";
        } else if (bitrate > 1024) {
            text += std::format("{:.1f}", bitrate / 1024.0) + " Kbps";
        } else {
            text += std::format("{:d}", bitrate) + " bps";
        }
        bitrate_label_->set_text(text);
    };
    GuiInterface::Instance().bitrateUpdateCallbacks.emplace_back(onBitrateUpdate);

    auto onTipUpdate = [this](std::string msg) { show_red_tip(msg); };
    GuiInterface::Instance().tipCallbacks.emplace_back(onTipUpdate);

    auto onUrlStreamShouldStop = [this]() { stop_playing(); };
    GuiInterface::Instance().urlStreamShouldStopCallbacks.emplace_back(onUrlStreamShouldStop);
}

void PlayerRect::custom_update(double dt) {
    player_->update(dt);

    hw_status_label_->set_text(FTR("hw decoding") + ": " +
                               std::string(player_->isHardwareAccelerated() ? FTR("on") : FTR("off")));

    display_fps_label_->set_text(FTR("display fps") + ": " +
                                 std::to_string(revector::Engine::get_singleton()->get_fps_int()));

    if (is_recording) {
        std::chrono::duration<double, std::chrono::seconds::period> duration =
            std::chrono::steady_clock::now() - record_start_time;

        int total_seconds = duration.count();
        int hours = total_seconds / 3600;
        int minutes = (total_seconds % 3600) / 60;
        int seconds = total_seconds % 60;

        std::ostringstream ss;
        ss << FTR("recording") << ": ";
        if (hours > 0) {
            ss << hours << ":";
        }
        ss << std::setw(2) << std::setfill('0') << minutes << ":";
        ss << std::setw(2) << std::setfill('0') << seconds;

        record_status_label_->set_text(ss.str());
    }
}

void PlayerRect::custom_draw() {
    if (!playing_) {
        return;
    }
    auto render_image = (revector::RenderImage *)texture.get();
    player_->yuvRenderer_->render(render_image->get_texture(), video_stabilization_button_->get_pressed());
}

void PlayerRect::start_playing(const std::string &url) {
    playing_ = true;
    player_->play(url, force_software_decoding);
    texture = render_image_;

    collapse_panel_->set_visibility(true);
    hud_container_->set_visibility(true);
}

void PlayerRect::stop_playing() {
    playing_ = false;

    if (is_recording) {
        record_button_->press();
    }

    // Fix crash in WfbReceiver destructor.
    if (player_) {
        player_->stop();
    }
    texture = logo_;

    collapse_panel_->set_visibility(false);
    hud_container_->set_visibility(false);
}
