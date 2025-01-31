#include <glad/gl.h>
#include <nodes/ui/menu_button.h>
#include <servers/render_server.h>

#include "app.h"
#include "gui/control_panel.h"
#include "gui/player_rect.h"
#include "gui_interface.h"
#include "resources/render_image.h"
#include "wifi/WFBReceiver.h"

constexpr auto LOGGER_MODULE = "Aviateur";

static Flint::App* app;

int main() {
    // Redirect standard output to a file
    //    freopen("last_run_log.txt", "w", stdout);

    // Flint::Logger::set_default_level(Flint::Logger::Level::Info);
    Flint::Logger::set_module_level("Flint", Flint::Logger::Level::Info);

    app = new Flint::App({1280, 720});
    app->set_window_title("Aviateur - OpenIPC FPV Ground Station");

    // Initialize the default libusb context.
    int rc = libusb_init(nullptr);

    Flint::Logger::set_module_level(LOGGER_MODULE, Flint::Logger::Level::Info);

    auto logCallback = [](LogLevel level, std::string msg) {
        switch (level) {
            case LogLevel::Info: {
                Flint::Logger::info(msg, LOGGER_MODULE);
            } break;
            case LogLevel::Debug: {
                Flint::Logger::debug(msg, LOGGER_MODULE);
            } break;
            case LogLevel::Warn: {
                Flint::Logger::warn(msg, LOGGER_MODULE);
            } break;
            case LogLevel::Error: {
                Flint::Logger::error(msg, LOGGER_MODULE);
            } break;
            default:;
        }
    };
    GuiInterface::Instance().logCallbacks.emplace_back(logCallback);

    auto hbox_container = std::make_shared<Flint::HBoxContainer>();
    hbox_container->set_separation(2);
    hbox_container->set_anchor_flag(Flint::AnchorFlag::FullRect);
    app->get_tree_root()->add_child(hbox_container);

    auto player_rect = std::make_shared<PlayerRect>();
    player_rect->container_sizing.expand_h = true;
    player_rect->container_sizing.expand_v = true;
    player_rect->container_sizing.flag_h = Flint::ContainerSizingFlag::Fill;
    player_rect->container_sizing.flag_v = Flint::ContainerSizingFlag::Fill;
    hbox_container->add_child(player_rect);

    auto control_panel = std::make_shared<ControlPanel>();
    control_panel->set_custom_minimum_size({280, 0});
    control_panel->container_sizing.expand_v = true;
    control_panel->container_sizing.flag_v = Flint::ContainerSizingFlag::Fill;
    hbox_container->add_child(control_panel);

    std::weak_ptr control_panel_weak = control_panel;
    std::weak_ptr player_rect_weak = player_rect;

    auto onWifiStop = [control_panel_weak, player_rect_weak] {
        if (!control_panel_weak.expired() && !player_rect_weak.expired()) {
            player_rect_weak.lock()->stop_playing();
            player_rect_weak.lock()->show_red_tip("Wi-Fi stopped!");
            control_panel_weak.lock()->update_adapter_start_button_looking(true);
        }
    };
    GuiInterface::Instance().wifiStopCallbacks.emplace_back(onWifiStop);

    {
        player_rect->fullscreen_button_ = std::make_shared<Flint::CheckButton>();
        player_rect->add_child(player_rect->fullscreen_button_);
        player_rect->fullscreen_button_->set_anchor_flag(Flint::AnchorFlag::TopLeft);
        player_rect->fullscreen_button_->set_text("Fullscreen (F11)");

        auto callback = [control_panel_weak](bool toggled) {
            if (!control_panel_weak.expired()) {
                app->set_fullscreen(toggled);
                control_panel_weak.lock()->set_visibility(!toggled);
            }
        };
        player_rect->fullscreen_button_->connect_signal("toggled", callback);
    }

    app->main_loop();

    // Quit app.
    delete app;
    app = nullptr;

    libusb_exit(nullptr);

    return EXIT_SUCCESS;
}
