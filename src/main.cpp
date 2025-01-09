#include <glad/gl.h>
#include <nodes/ui/menu_button.h>
#include <servers/render_server.h>

#include "app.h"
#include "gui_interface.h"
#include "player/RealTimePlayer.h"
#include "resources/render_image.h"
#include "wifi/WFBReceiver.h"

class TipLabel : public Flint::Label {
public:
    float alpha = 0;
    float display_time = 2;
    float fade_time = 0.1;

    std::shared_ptr<Flint::Timer> display_timer;
    std::shared_ptr<Flint::Timer> fade_timer;

    void custom_ready() override {
        display_timer = std::make_shared<Flint::Timer>();
        fade_timer = std::make_shared<Flint::Timer>();

        add_child(display_timer);
        add_child(fade_timer);

        auto callback = [this] { this->fade_timer->start_timer(fade_time); };
        display_timer->connect_signal("timeout", callback);

        auto callback2 = [this] { set_visibility(false); };
        fade_timer->connect_signal("timeout", callback2);
    }

    void show_tip(std::string tip) {
        set_text(tip);
        set_visibility(true);
        display_timer->start_timer(display_time);
    }
};

class MyRenderRect : public Flint::TextureRect {
public:
    std::shared_ptr<RealTimePlayer> player_;
    std::string playing_file_;
    bool playing_ = false;

    std::shared_ptr<Flint::VectorImage> logo_;
    std::shared_ptr<Flint::RenderImage> render_image_;

    std::shared_ptr<TipLabel> tip_label_;

    bool is_recording = false;

    void custom_ready() override {
        logo_ = std::make_shared<Flint::VectorImage>("openipc-logo-white.svg");
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

        auto hud_panel = std::make_shared<Flint::Panel>();
        hud_panel->set_size({0, 48});
        Flint::StyleBox box;
        box.bg_color = Flint::ColorU(27, 27, 27, 27);
        box.border_width = 0;
        hud_panel->set_theme_panel(box);
        add_child(hud_panel);
        hud_panel->set_anchor_flag(Flint::AnchorFlag::BottomWide);
        // hud_panel->set_visibility(false);

        auto bitrate_label = std::make_shared<Flint::Label>();
        bitrate_label->set_text("Bitrate: 0 Kbps");
        bitrate_label->set_text_style(Flint::TextStyle{Flint::ColorU::white()});
        bitrate_label->set_anchor_flag(Flint::AnchorFlag::CenterLeft);
        hud_panel->add_child(bitrate_label);

        auto record_button = std::make_shared<Flint::Button>();
        hud_panel->add_child(record_button);
        record_button->set_text("Record");
        record_button->set_toggle_mode(true);
        record_button->set_anchor_flag(Flint::AnchorFlag::CenterRight);
        auto record_callback = [record_button, this]() {
            if (!is_recording) {
                is_recording = player_->startRecord();

                if (is_recording) {
                    record_button->set_text("Stop");
                } else {
                    tip_label_->show_tip("Recording failed!");
                }
                //     if(recordTimer.started){
                //         recordTimer.start();

                // if(!recordTimer.started){
                //     recordTimer.started = player.startRecord();
                //     if(recordTimer.started){
                //         recordTimer.start();
                //     }else{
                //         tips.showPop('Record failed! ',3000);
                //     }
                // }else{
                //     recordTimer.started = false;
                //     let f = player.stopRecord();
                //     if(f!==''){
                //         tips.showPop('Saved '+f,3000);
                //     }else{
                //         tips.showPop('Record failed! ',3000);
                //     }
                //     recordTimer.stop();
                // }
            } else {
                auto file_path = player_->stopRecord();
                // Show tip
                record_button->set_text("Record");
            }
        };
        record_button->connect_signal("pressed", record_callback);

        auto record_timer_label = std::make_shared<Flint::Label>();
        record_timer_label->set_text("Record");

        auto onBitrateUpdate = [bitrate_label](uint64_t bitrate) {
            std::string text = "Bitrate: ";
            if (bitrate > 1000 * 1000) {
                text += std::format("{:.2f}", bitrate / 1000.0 / 1000.0) + " Mbps";
            } else if (bitrate > 1000) {
                text += std::format("{:.2f}", bitrate / 1000.0) + " Kbps";
            } else {
                text += bitrate + " bps";
            }
            bitrate_label->set_text(text);
        };
        GuiInterface::Instance().bitrateUpdateCallbacks.emplace_back(onBitrateUpdate);
    }

    void custom_update(double delta) override {
        player_->update(delta);
    }

    void custom_draw() override {
        if (!playing_) {
            return;
        }
        auto render_image = (Flint::RenderImage *)texture.get();
        player_->m_yuv_renderer->render(render_image->get_texture());
    }

    void start_playing(std::string url) {
        playing_ = true;
        player_->play(url);
        texture = render_image_;
    }

    void stop_playing() {
        playing_ = false;
        // Fix crash in WFBReceiver destructor.
        if (player_) {
            player_->stop();
        }
        texture = logo_;
    }
};

class MyControlPanel : public Flint::Panel {
    std::shared_ptr<Flint::MenuButton> dongle_menu_button_;
    std::shared_ptr<Flint::MenuButton> channel_button_;
    std::shared_ptr<Flint::MenuButton> channel_width_button_;

    std::string vidPid = "";
    int channel = 173;
    int channelWidthMode = 0;
    std::string keyPath = "gs.key";
    std::string codec = "AUTO";

    std::shared_ptr<Flint::Button> play_button_;

    void update_dongle_list(Flint::PopupMenu &menu) const {
        auto dongles = GuiInterface::GetDongleList();

        for (auto dongle : dongles) {
            menu.create_item(dongle);
        }
    }

    void custom_ready() override {
        if (GuiInterface::Instance().config_file_exists) {
            vidPid = toolkit::mINI::Instance()[CONFIG_DEVICE];
            channel = toolkit::mINI::Instance()[CONFIG_CHANNEL];
            channelWidthMode = toolkit::mINI::Instance()[CONFIG_CHANNEL_WIDTH_MODE];
            keyPath = toolkit::mINI::Instance()[CONFIG_CHANNEL_KEY];
            codec = toolkit::mINI::Instance()[CONFIG_CHANNEL_CODEC];
        }

        auto margin_container = std::make_shared<Flint::MarginContainer>();
        margin_container->set_margin_all(8);
        margin_container->set_anchor_flag(Flint::AnchorFlag::FullRect);
        add_child(margin_container);

        auto vbox_container0 = std::make_shared<Flint::VBoxContainer>();
        vbox_container0->set_separation(8);
        margin_container->add_child(vbox_container0);

        auto collapse_panel = std::make_shared<Flint::CollapseContainer>();
        collapse_panel->set_title("Adapter Control");
        collapse_panel->set_color(Flint::ColorU::red());
        vbox_container0->add_child(collapse_panel);

        auto vbox_container = std::make_shared<Flint::VBoxContainer>();
        vbox_container->set_separation(8);
        collapse_panel->add_child(vbox_container);

        {
            auto label = std::make_shared<Flint::Label>();
            label->set_text("RTL8812AU VID:PID");
            vbox_container->add_child(label);
        }

        {
            dongle_menu_button_ = std::make_shared<Flint::MenuButton>();
            dongle_menu_button_->set_text(vidPid);
            vbox_container->add_child(dongle_menu_button_);

            auto dongle_menu = dongle_menu_button_->get_popup_menu();

            auto callback = [this](uint32_t) { vidPid = dongle_menu_button_->get_selected_item_text(); };
            dongle_menu_button_->connect_signal("item_selected", callback);

            update_dongle_list(*dongle_menu.lock());
        }

        {
            auto hbox_container = std::make_shared<Flint::HBoxContainer>();
            vbox_container->add_child(hbox_container);

            auto label = std::make_shared<Flint::Label>();
            label->set_text("Channel:");
            hbox_container->add_child(label);

            channel_button_ = std::make_shared<Flint::MenuButton>();
            channel_button_->set_text(std::to_string(channel));
            hbox_container->add_child(channel_button_);

            {
                auto channel_menu = channel_button_->get_popup_menu();

                auto callback = [this](uint32_t) { channel = std::stoi(channel_button_->get_selected_item_text()); };
                channel_button_->connect_signal("item_selected", callback);

                for (auto c : CHANNELS) {
                    channel_menu.lock()->create_item(std::to_string(c));
                }
            }
        }

        {
            auto hbox_container = std::make_shared<Flint::HBoxContainer>();
            vbox_container->add_child(hbox_container);

            auto label = std::make_shared<Flint::Label>();
            label->set_text("Channel Width:");
            hbox_container->add_child(label);

            channel_width_button_ = std::make_shared<Flint::MenuButton>();
            channel_width_button_->set_text(std::to_string(ChannelWidth_t(channelWidthMode)));
            channel_width_button_->set_text(CHANNEL_WIDTHS[channelWidthMode]);
            hbox_container->add_child(channel_width_button_);

            {
                auto channel_width_menu = channel_width_button_->get_popup_menu();

                auto callback = [this](uint32_t) { channelWidthMode = channel_button_->get_selected_item_index(); };
                channel_width_button_->connect_signal("item_selected", callback);

                for (auto width : CHANNEL_WIDTHS) {
                    channel_width_menu.lock()->create_item(width);
                }
            }
        }

        {
            auto label = std::make_shared<Flint::Label>();
            label->set_text("Key:");
            vbox_container->add_child(label);

            auto hbox_container = std::make_shared<Flint::HBoxContainer>();
            vbox_container->add_child(hbox_container);

            auto text_edit = std::make_shared<Flint::TextEdit>();
            text_edit->set_editable(false);
            text_edit->set_text("gs.key");
            text_edit->container_sizing.expand_h = true;
            text_edit->container_sizing.flag_h = Flint::ContainerSizingFlag::Fill;
            hbox_container->add_child(text_edit);

            auto file_dialog = std::make_shared<Flint::FileDialog>();
            add_child(file_dialog);

            auto select_button = std::make_shared<Flint::Button>();
            select_button->set_text("Open");

            std::weak_ptr file_dialog_weak = file_dialog;
            std::weak_ptr text_edit_weak = text_edit;
            auto callback = [file_dialog_weak, text_edit_weak] {
                auto path = file_dialog_weak.lock()->show();
                if (path.has_value()) {
                    std::filesystem::path p(path.value());
                    text_edit_weak.lock()->set_text(p.filename().string());
                }
            };
            select_button->connect_signal("pressed", callback);
            hbox_container->add_child(select_button);
        }

        {
            play_button_ = std::make_shared<Flint::Button>();
            play_button_->set_text("Start");
            play_button_->container_sizing.expand_h = true;
            play_button_->container_sizing.flag_h = Flint::ContainerSizingFlag::Fill;

            auto callback1 = [this] {
                if (play_button_->get_text() == "Start") {
                    play_button_->set_text("Stop");
                    bool res = GuiInterface::Start(vidPid, channel, channelWidthMode, keyPath, codec);
                } else {
                    play_button_->set_text("Start");
                    GuiInterface::Stop();
                }
            };
            play_button_->connect_signal("pressed", callback1);
            vbox_container->add_child(play_button_);
        }

        auto collapse_panel2 = std::make_shared<Flint::CollapseContainer>();
        collapse_panel2->set_title("Player Control");
        // collapse_panel2->set_color(Flint::ColorU::green());
        vbox_container0->add_child(collapse_panel2);
    }
};

int main() {
    Flint::App app({1280, 720});
    app.set_window_title("Aviateur - OpenIPC FPV Ground Station");
    Flint::Logger::set_level(Flint::Logger::Level::Silence);

    auto hbox_container = std::make_shared<Flint::HBoxContainer>();
    hbox_container->set_separation(8);
    hbox_container->set_anchor_flag(Flint::AnchorFlag::FullRect);
    app.get_tree_root()->add_child(hbox_container);

    auto render_rect = std::make_shared<MyRenderRect>();
    render_rect->set_custom_minimum_size({640, 360});
    render_rect->container_sizing.expand_h = true;
    render_rect->container_sizing.expand_v = true;
    render_rect->container_sizing.flag_h = Flint::ContainerSizingFlag::Fill;
    render_rect->container_sizing.flag_v = Flint::ContainerSizingFlag::Fill;
    hbox_container->add_child(render_rect);

    auto control_panel = std::make_shared<MyControlPanel>();
    control_panel->set_custom_minimum_size({280, 0});
    control_panel->container_sizing.expand_v = true;
    control_panel->container_sizing.flag_v = Flint::ContainerSizingFlag::Fill;
    hbox_container->add_child(control_panel);

    auto render_rect_raw = render_rect.get();
    auto onRtpStream = [render_rect_raw](std::string sdp_file) {
        render_rect_raw->playing_file_ = sdp_file;
        render_rect_raw->start_playing(sdp_file);
    };
    GuiInterface::Instance().rtpStreamCallbacks.emplace_back(onRtpStream);

    auto onWifiStop = [render_rect_raw] { render_rect_raw->stop_playing(); };
    GuiInterface::Instance().wifiStopCallbacks.emplace_back(onWifiStop);

    auto prompt_popup = std::make_shared<Flint::Panel>();

    app.main_loop();

    return EXIT_SUCCESS;
}
