#include "app.h"
#include "player/RealTimePlayer.h"
#include "resources/render_image.h"
#include "sdp_handler.h"
#include "wifi/WFBReceiver.h"

#include <servers/render_server.h>
#include <servers/vector_server.h>

class MyRenderRect : public Flint::TextureRect {
    std::shared_ptr<RealTimePlayer> player_;

    void custom_ready() override {
        set_custom_minimum_size({400, 400});
        container_sizing.expand_h = true;
        container_sizing.flag_h = Flint::ContainerSizingFlag::Fill;

        auto render_server = Flint::RenderServer::get_singleton();
        player_ = std::make_shared<RealTimePlayer>(render_server->device_, render_server->queue_);
    }

    void custom_update(double delta) override {
        player_->update(delta);
        texture = std::make_shared<Flint::RenderImage>(size.to_i32());

        Flint::VectorServer::get_singleton()->canvas->get_scene()->push_render_target(render_target_desc);
    }

    void custom_draw() override {
        auto render_image = (Flint::RenderImage *)texture.get();
        auto yuv_texture
            = Flint::VectorServer::get_singleton()->get_texture_by_render_target_id(render_image->get_render_target());
        player_->m_yuv_renderer->render(yuv_texture);
    }

    void start_playing(std::string url) { player_->play(url); }

    void stop_playing() { player_->stop(); }
};

class MyControlPanel : public Flint::Panel {
    std::shared_ptr<Flint::PopupMenu> dongle_menu_;

    std::string vidPid = "obda:8812";
    int channel = 173;
    int channelWidth = 20;
    std::string keyPath = "D:/Dev/Projects/fpv4win/gs.key";
    std::string codec = "AUTO";

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
            auto button = std::make_shared<Flint::Button>();
            button->set_text("Start");
            button->container_sizing.expand_h = true;
            button->container_sizing.flag_h = Flint::ContainerSizingFlag::Fill;

            auto button_raw = button.get();

            auto callback1 = [button_raw, this] {
                if (button_raw->get_text() == "Start") {
                    button_raw->set_text("Stop");
                    SdpHandler::Instance().Stop();
                } else {
                    button_raw->set_text("Start");
                    SdpHandler::Instance().Start(vidPid, channel, channelWidth, keyPath, codec);
                }
            };
            button->connect_signal("pressed", callback1);
            vbox_container->add_child(button);
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

    app.main_loop();

    return EXIT_SUCCESS;
}
