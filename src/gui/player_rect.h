#pragma once

#include "../player/RealTimePlayer.h"
#include "app.h"
#include "tip_label.h"

class PlayerRect : public Flint::TextureRect {
public:
    std::shared_ptr<RealTimePlayer> player_;
    std::string playing_file_;
    bool playing_ = false;

    bool force_software_decoding = false;

    std::shared_ptr<Flint::VectorImage> logo_;
    std::shared_ptr<Flint::RenderImage> render_image_;

    std::shared_ptr<TipLabel> tip_label_;

    bool is_recording = false;

    std::chrono::time_point<std::chrono::steady_clock> record_start_time;

    std::shared_ptr<Flint::CollapseContainer> collapse_panel_;

    std::shared_ptr<Flint::HBoxContainer> hud_container_;

    std::shared_ptr<Flint::Label> record_status_label_;

    std::shared_ptr<Flint::Label> bitrate_label_;

    std::shared_ptr<Flint::Label> hw_status_label_;

    std::shared_ptr<Flint::Label> video_info_label_;

    std::shared_ptr<Flint::Label> display_fps_label_;

    std::shared_ptr<Flint::Button> video_stabilization_button_;

    std::shared_ptr<Flint::Button> low_light_enhancement_button_simple_;
    std::shared_ptr<Flint::Button> low_light_enhancement_button_advanced_;

    std::shared_ptr<Flint::Button> fullscreen_button_;

    std::shared_ptr<Flint::Button> record_button_;

    // Record when the signal had been lost.
    std::chrono::time_point<std::chrono::steady_clock> signal_lost_time_;

    void show_red_tip(std::string tip);

    void show_green_tip(std::string tip);

    void custom_input(Flint::InputEvent &event) override;

    void custom_ready() override;

    void custom_update(double dt) override;

    void custom_draw() override;

    void start_playing(const std::string &url);

    void stop_playing();
};
