#include "app.h"
#include "player/RealTimePlayer.h"
#include "resources/render_image.h"
#include "sdp_handler.h"
#include "wifi/WFBReceiver.h"

#include <servers/render_server.h>

class MyRenderRect : public Flint::TextureRect {
public:
    std::shared_ptr<RealTimePlayer> player_;
    std::string playing_file_;
    bool playing_ = false;

    void custom_ready() override {
        set_custom_minimum_size({ 400, 400 });
        container_sizing.expand_h = true;
        container_sizing.flag_h = Flint::ContainerSizingFlag::Fill;

        auto render_server = Flint::RenderServer::get_singleton();
        player_ = std::make_shared<RealTimePlayer>(render_server->device_, render_server->queue_);

        texture = std::make_shared<Flint::RenderImage>(Pathfinder::Vec2I { 400, 400 });
    }

    void custom_update(double delta) override { player_->update(delta); }

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
    }

    void stop_playing() {
        playing_ = false;
        player_->stop();
    }
};

class MyControlPanel : public Flint::Panel {
    std::shared_ptr<Flint::PopupMenu> dongle_menu_;

    std::string vidPid = "0bda:8812";
    int channel = 173;
    int channelWidthMode = 0;
    std::string keyPath = "D:/Dev/Projects/fpv4win/gs.key";
    std::string codec = "AUTO";

    std::shared_ptr<Flint::Button> play_button_;

    void update_dongle_list() const {
        auto dongles = SdpHandler::GetDongleList();

        dongle_menu_->clear_items();
        for (auto dongle : dongles) {
            dongle_menu_->create_item(dongle);
        }
    }

    void custom_ready() override {
        auto vbox_container = std::make_shared<Flint::VBoxContainer>();
        vbox_container->set_separation(8);
        vbox_container->set_anchor_flag(Flint::AnchorFlag::FullRect);
        add_child(vbox_container);

        {
            dongle_menu_ = std::make_shared<Flint::PopupMenu>();
            dongle_menu_->container_sizing.flag_h = Flint::ContainerSizingFlag::ShrinkStart;
            vbox_container->add_child(dongle_menu_);

            update_dongle_list();
        }
        {
            play_button_ = std::make_shared<Flint::Button>();
            play_button_->set_text("Start");
            play_button_->container_sizing.expand_h = true;
            play_button_->container_sizing.flag_h = Flint::ContainerSizingFlag::Fill;

            auto callback1 = [this] {
                if (this->play_button_->get_text() == "Start") {
                    this->play_button_->set_text("Stop");
                    SdpHandler::Instance().Start(vidPid, channel, channelWidthMode, keyPath, codec);
                } else {
                    this->play_button_->set_text("Start");
                    SdpHandler::Instance().Stop();
                }
            };
            play_button_->connect_signal("pressed", callback1);
            vbox_container->add_child(play_button_);
        }
    }
};

int main() {
    Flint::App app({ 1280, 720 });

    auto hbox_container = std::make_shared<Flint::HBoxContainer>();
    hbox_container->set_separation(8);
    app.get_tree_root()->add_child(hbox_container);

    auto render_rect = std::make_shared<MyRenderRect>();
    hbox_container->add_child(render_rect);

    auto control_panel = std::make_shared<MyControlPanel>();
    control_panel->set_custom_minimum_size({ 280, 720 });
    hbox_container->add_child(control_panel);

    auto render_rect_raw = render_rect.get();
    auto onRtpStream = [render_rect_raw](std::string sdp_file) {
        render_rect_raw->playing_file_ = sdp_file;
        render_rect_raw->start_playing(sdp_file);
    };
    SdpHandler::Instance().onRtpStream = onRtpStream;

    auto onWifiStop = [render_rect_raw]() { render_rect_raw->stop_playing(); };
    SdpHandler::Instance().onWifiStop = onWifiStop;

    app.main_loop();

    return EXIT_SUCCESS;
}
