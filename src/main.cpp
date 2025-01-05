#include "app.h"
#include "player/RealTimePlayer.h"
#include "resources/render_image.h"
#include "wifi/WFBReceiver.h"

#include <servers/render_server.h>
#include <servers/vector_server.h>

class MyRenderRect : public Flint::TextureRect {
    std::shared_ptr<RealTimePlayer> player_;

    std::shared_ptr<Flint::RenderImage> image_;

    void custom_ready() override {
        auto render_server = Flint::RenderServer::get_singleton();
        player_ = std::make_shared<RealTimePlayer>(render_server->device_, render_server->queue_);

        image_ = std::make_shared<Flint::RenderImage>(size.to_i32());
    }

    void custom_update(double delta) override { player_->update(delta); }

    void custom_draw() override {
        if (image_->get_size() != size.to_i32()) {
            image_ = std::make_shared<Flint::RenderImage>(size.to_i32());
        }
        image_->reclaim_render_target();

        auto yuv_texture
            = Flint::VectorServer::get_singleton()->get_texture_by_render_target_id(image_->get_render_target());
        player_->m_yuv_renderer->render(yuv_texture);
    }

    void start_playing(std::string url) { player_->play(url); }

    void stop_playing() { player_->stop(); }
};

class MyControlPanel : public Flint::Panel {
    std::shared_ptr<Flint::PopupMenu> dongle_menu_;

    void update_dongle_list() const {
        auto dongles = WFBReceiver::Instance().GetDongleList();

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
    render_rect->container_sizing.expand_h = true;
    render_rect->container_sizing.flag_h = Flint::ContainerSizingFlag::Fill;
    hbox_container->add_child(render_rect);

    auto control_panel = std::make_shared<MyControlPanel>();
    control_panel->set_custom_minimum_size({ 280, 720 });
    hbox_container->add_child(control_panel);

    app.main_loop();

    return EXIT_SUCCESS;
}
