#include "player_rect.h"

#include "../gui_interface.h"

void PlayerRect::show_red_tip(std::string tip) {
    auto red = Flint::ColorU(201, 79, 79);
    tip_label_->set_text_style(Flint::TextStyle{red});
    tip_label_->show_tip(tip);
}

void PlayerRect::show_green_tip(std::string tip) {
    auto green = Flint::ColorU(78, 135, 82);
    tip_label_->set_text_style(Flint::TextStyle{green});
    tip_label_->show_tip(tip);
}

void PlayerRect::custom_input(Flint::InputEvent &event) {
    auto input_server = Flint::InputServer::get_singleton();

    if (event.type == Flint::InputEventType::Key) {
        auto key_args = event.args.key;

        if (key_args.key == Flint::KeyCode::F11) {
            if (key_args.pressed) {
                fullscreen_button_->press();
            }
        }

        if (playing_ && key_args.key == Flint::KeyCode::F10) {
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

    collapse_panel_ = std::make_shared<Flint::CollapseContainer>();
    collapse_panel_->set_title("Player Control");
    collapse_panel_->set_collapse(true);
    collapse_panel_->set_color(Flint::ColorU(84, 138, 247));
    collapse_panel_->set_anchor_flag(Flint::AnchorFlag::TopRight);
    collapse_panel_->set_visibility(false);
    add_child(collapse_panel_);

    auto vbox = std::make_shared<Flint::VBoxContainer>();
    collapse_panel_->add_child(vbox);

    logo_ = std::make_shared<Flint::VectorImage>("assets/openipc-logo-white.svg");
    texture = logo_;

    auto render_server = Flint::RenderServer::get_singleton();
    player_ = std::make_shared<RealTimePlayer>(render_server->device_, render_server->queue_);

    render_image_ = std::make_shared<Flint::RenderImage>(Pathfinder::Vec2I{1920, 1080});

    set_stretch_mode(StretchMode::KeepAspectCentered);

    tip_label_ = std::make_shared<TipLabel>();
    tip_label_->set_anchor_flag(Flint::AnchorFlag::Center);
    tip_label_->set_visibility(false);
    tip_label_->set_text_style(Flint::TextStyle{Flint::ColorU::red()});
    add_child(tip_label_);

    hud_container_ = std::make_shared<Flint::HBoxContainer>();
    add_child(hud_container_);
    hud_container_->set_size({0, 48});
    Flint::StyleBox box;
    box.bg_color = Flint::ColorU(27, 27, 27, 27);
    box.border_width = 0;
    box.corner_radius = 0;
    hud_container_->set_theme_bg(box);
    hud_container_->set_anchor_flag(Flint::AnchorFlag::BottomWide);
    hud_container_->set_visibility(false);
    hud_container_->set_separation(16);

    {
        video_info_label_ = std::make_shared<Flint::Label>();
        hud_container_->add_child(video_info_label_);
        video_info_label_->set_text_style(Flint::TextStyle{Flint::ColorU::white()});
        video_info_label_->set_text("");

        auto onFpsUpdate = [this](uint32_t width, uint32_t height, float fps) {
            std::stringstream ss;
            ss << width << "x" << height << "@" << int(round(fps));
            video_info_label_->set_text(ss.str());
        };
        GuiInterface::Instance().decoderReadyCallbacks.emplace_back(onFpsUpdate);
    }

    bitrate_label_ = std::make_shared<Flint::Label>();
    hud_container_->add_child(bitrate_label_);
    bitrate_label_->set_text("Bit rate: 0 bps");
    bitrate_label_->set_text_style(Flint::TextStyle{Flint::ColorU::white()});

    {
        display_fps_label_ = std::make_shared<Flint::Label>();
        hud_container_->add_child(display_fps_label_);
        display_fps_label_->set_text_style(Flint::TextStyle{Flint::ColorU::white()});
        display_fps_label_->set_text("Display FPS: ");
    }

    hw_status_label_ = std::make_shared<Flint::Label>();
    hud_container_->add_child(hw_status_label_);
    hw_status_label_->set_text_style(Flint::TextStyle{Flint::ColorU::white()});

    record_status_label_ = std::make_shared<Flint::Label>();
    hud_container_->add_child(record_status_label_);
    record_status_label_->container_sizing.expand_h = true;
    record_status_label_->container_sizing.flag_h = Flint::ContainerSizingFlag::ShrinkEnd;
    record_status_label_->set_text("Not recording");
    record_status_label_->set_text_style(Flint::TextStyle{Flint::ColorU::white()});

    auto capture_button = std::make_shared<Flint::Button>();
    vbox->add_child(capture_button);
    capture_button->set_text("Capture Frame");
    auto icon = std::make_shared<Flint::VectorImage>("assets/CaptureImage.svg");
    capture_button->set_icon_normal(icon);
    auto capture_callback = [this] {
        auto output_file = player_->captureJpeg();
        if (output_file.empty()) {
            show_red_tip("Failed to capture frame!");
        } else {
            show_green_tip("Frame saved to: " + output_file);
        }
    };
    capture_button->connect_signal("pressed", capture_callback);

    record_button_ = std::make_shared<Flint::Button>();
    vbox->add_child(record_button_);
    auto icon2 = std::make_shared<Flint::VectorImage>("assets/RecordVideo.svg");
    record_button_->set_icon_normal(icon2);
    record_button_->set_text("Record MP4 (F10)");

    auto record_button_raw = record_button_.get();
    auto record_callback = [record_button_raw, this] {
        if (!is_recording) {
            is_recording = player_->startRecord();

            if (is_recording) {
                record_button_raw->set_text("Stop Recording (F10)");

                record_start_time = std::chrono::steady_clock::now();

                record_status_label_->set_text("Recording 00:00");
            } else {
                record_status_label_->set_text("Not recording");
                show_red_tip("Recording failed!");
            }
        } else {
            is_recording = false;

            auto output_file = player_->stopRecord();

            record_button_raw->set_text("Record MP4 (F10)");
            record_status_label_->set_text("Not Recording");

            if (output_file.empty()) {
                show_red_tip("Failed to save the record file!");
            } else {
                show_green_tip("Video saved to: " + output_file);
            }
        }
    };
    record_button_->connect_signal("pressed", record_callback);

    {
        video_stabilization_button_ = std::make_shared<Flint::CheckButton>();
        video_stabilization_button_->set_text("Video Stabilization");
        vbox->add_child(video_stabilization_button_);

        auto callback = [this](bool toggled) {
            player_->yuvRenderer_->mStabilize = toggled;
            if (toggled) {
                show_red_tip("Video stabilization is experimental!");
            }
        };
        video_stabilization_button_->connect_signal("toggled", callback);
    }

    {
        auto button = std::make_shared<Flint::CheckButton>();
        button->set_text("Force Software Decoder");
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
        std::string text = "Bit rate: ";
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

    hw_status_label_->set_text("Hw decoding: " + std::string(player_->isHardwareAccelerated() ? "ON" : "OFF"));

    display_fps_label_->set_text("Display FPS: " + std::to_string(Flint::Engine::get_singleton()->get_fps_int()));

    if (is_recording) {
        std::chrono::duration<double, std::chrono::seconds::period> duration =
            std::chrono::steady_clock::now() - record_start_time;

        int total_seconds = duration.count();
        int hours = total_seconds / 3600;
        int minutes = (total_seconds % 3600) / 60;
        int seconds = total_seconds % 60;

        std::ostringstream ss;
        ss << "Recording ";
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
    auto render_image = (Flint::RenderImage *)texture.get();
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

    // Fix crash in WFBReceiver destructor.
    if (player_) {
        player_->stop();
    }
    texture = logo_;

    collapse_panel_->set_visibility(false);
    hud_container_->set_visibility(false);
}
