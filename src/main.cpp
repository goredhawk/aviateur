#include "app.h"
#include "player/RealTimePlayer.h"

#include <servers/render_server.h>

class MyPlayer : public Flint::Node {
    std::shared_ptr<RealTimePlayer> player;

    void custom_ready() override {
        auto render_server = Flint::RenderServer::get_singleton();
        player = std::make_shared<RealTimePlayer>(render_server->device_, render_server->queue_);
    }

    void custom_update(double delta) override {
        player->update(delta);
    }
};

class MyControlPanel : public Flint::Panel {
    void custom_ready() override {
        auto vbox_container = std::make_shared<Flint::VBoxContainer>();
        vbox_container->set_separation(8);
        vbox_container->set_position({100, 100});
        add_child(vbox_container);
        //
        // {
        //     auto dongle_menu = std::make_shared<Flint::PopupMenu>();
        //     dongle_menu->container_sizing.flag_h = Flint::ContainerSizingFlag::ShrinkStart;
        //     vbox_container->add_child(dongle_menu);
        // }
        // {
        //     auto button = std::make_shared<Flint::Button>();
        //     button->set_text("Start");
        //     button->container_sizing.flag_h = Flint::ContainerSizingFlag::ShrinkStart;
        //     vbox_container->add_child(button);
        // }

    }
};

int main() {
    Flint::App app({ 1280, 720 });

    app.get_tree_root()->add_child(std::make_shared<MyControlPanel>());

    app.get_tree_root()->add_child(std::make_shared<MyPlayer>());

    app.main_loop();

    return EXIT_SUCCESS;
}
