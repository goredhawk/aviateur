#include "control_panel.h"

#include <resources/default_resource.h>

#include "settings_tab.h"

void ControlPanel::update_dongle_list() {
    auto menu = dongle_menu_button_->get_popup_menu().lock();

    devices_ = GuiInterface::GetDeviceList();

    menu->clear_items();

    bool previous_device_exists = false;
    for (const auto &d : devices_) {
        if (net_card_name == d.display_name) {
            previous_device_exists = true;
            selected_net_card = d;
        }
        menu->create_item(d.display_name);
    }

    if (!previous_device_exists) {
        net_card_name = "";
        selected_net_card = {};
    }
}

void ControlPanel::update_adapter_start_button_looking(bool start_status) const {
    tab_container_->set_tab_disabled(!start_status);

    if (!start_status) {
        play_button_->theme_normal.bg_color = RED;
        play_button_->theme_hovered.bg_color = RED;
        play_button_->theme_pressed.bg_color = RED;
        play_button_->set_text(FTR("stop") + " (F5)");
    } else {
        play_button_->theme_normal.bg_color = GREEN;
        play_button_->theme_hovered.bg_color = GREEN;
        play_button_->theme_pressed.bg_color = GREEN;
        play_button_->set_text(FTR("start") + " (F5)");
    }
}

void ControlPanel::update_url_start_button_looking(bool start_status) const {
    tab_container_->set_tab_disabled(!start_status);

    if (!start_status) {
        play_url_button_->theme_normal.bg_color = RED;
        play_url_button_->theme_hovered.bg_color = RED;
        play_url_button_->theme_pressed.bg_color = RED;
        play_url_button_->set_text(FTR("close") + " (F5)");
    } else {
        play_url_button_->theme_normal.bg_color = GREEN;
        play_url_button_->theme_hovered.bg_color = GREEN;
        play_url_button_->theme_pressed.bg_color = GREEN;
        play_url_button_->set_text(FTR("start") + " (F5)");
    }
}

void ControlPanel::custom_ready() {
    auto &ini = GuiInterface::Instance().ini_;
    net_card_name = ini[CONFIG_ADAPTER][ADAPTER_DEVICE];
    channel = std::stoi(ini[CONFIG_ADAPTER][ADAPTER_CHANNEL]);
    channelWidthMode = std::stoi(ini[CONFIG_ADAPTER][ADAPTER_CHANNEL_WIDTH_MODE]);
    keyPath = ini[CONFIG_ADAPTER][ADAPTER_CHANNEL_KEY];
    codec = ini[CONFIG_ADAPTER][ADAPTER_CHANNEL_CODEC];

    auto default_theme = revector::DefaultResource::get_singleton()->get_default_theme();
    theme_bg = std::make_optional(default_theme->panel.styles["background"]);
    theme_bg.value().corner_radius = 0;
    theme_bg.value().border_width = 0;
    theme_bg->border_width = 0;

    set_anchor_flag(revector::AnchorFlag::RightWide);

    tab_container_ = std::make_shared<revector::TabContainer>();
    add_child(tab_container_);
    tab_container_->set_anchor_flag(revector::AnchorFlag::FullRect);

    // Wi-Fi adapter tab
    {
        auto margin_container = std::make_shared<revector::MarginContainer>();
        margin_container->set_margin_all(8);
        tab_container_->add_child(margin_container);
        tab_container_->set_tab_title(0, FTR("wi-fi adapter"));

        auto vbox_container = std::make_shared<revector::VBoxContainer>();
        vbox_container->set_separation(8);
        margin_container->add_child(vbox_container);

        {
            auto hbox_container = std::make_shared<revector::HBoxContainer>();
            hbox_container->set_separation(8);
            vbox_container->add_child(hbox_container);

            auto label = std::make_shared<revector::Label>();
            label->set_text(FTR("device"));
            hbox_container->add_child(label);

            dongle_menu_button_ = std::make_shared<revector::MenuButton>();

            dongle_menu_button_->container_sizing.expand_h = true;
            dongle_menu_button_->container_sizing.flag_h = revector::ContainerSizingFlag::Fill;
            hbox_container->add_child(dongle_menu_button_);

            // Do this before setting dongle button text.
            update_dongle_list();
            dongle_menu_button_->set_text(net_card_name);

            auto callback = [this](uint32_t) { net_card_name = dongle_menu_button_->get_selected_item_text(); };
            dongle_menu_button_->connect_signal("item_selected", callback);

            refresh_dongle_button_ = std::make_shared<revector::Button>();
            auto icon = std::make_shared<revector::VectorImage>(revector::get_asset_dir("Refresh.svg"));
            refresh_dongle_button_->set_icon_normal(icon);
            refresh_dongle_button_->set_text("");
            hbox_container->add_child(refresh_dongle_button_);

            auto callback2 = [this]() { update_dongle_list(); };
            refresh_dongle_button_->connect_signal("pressed", callback2);
        }

        {
            auto hbox_container = std::make_shared<revector::HBoxContainer>();
            vbox_container->add_child(hbox_container);

            auto label = std::make_shared<revector::Label>();
            label->set_text(FTR("channel"));
            hbox_container->add_child(label);

            channel_button_ = std::make_shared<revector::MenuButton>();
            channel_button_->container_sizing.expand_h = true;
            channel_button_->container_sizing.flag_h = revector::ContainerSizingFlag::Fill;
            hbox_container->add_child(channel_button_);

            {
                auto channel_menu = channel_button_->get_popup_menu();

                auto callback = [this](uint32_t) { channel = std::stoi(channel_button_->get_selected_item_text()); };
                channel_button_->connect_signal("item_selected", callback);

                uint32_t selected = 0;
                for (auto c : CHANNELS) {
                    channel_menu.lock()->create_item(std::to_string(c));
                    if (std::to_string(channel) == std::to_string(c)) {
                        selected = channel_menu.lock()->get_item_count() - 1;
                    }
                }

                channel_button_->select_item(selected);
            }
        }

        {
            auto hbox_container = std::make_shared<revector::HBoxContainer>();
            vbox_container->add_child(hbox_container);

            auto label = std::make_shared<revector::Label>();
            label->set_text(FTR("channel width"));
            hbox_container->add_child(label);

            channel_width_button_ = std::make_shared<revector::MenuButton>();
            channel_width_button_->container_sizing.expand_h = true;
            channel_width_button_->container_sizing.flag_h = revector::ContainerSizingFlag::Fill;
            hbox_container->add_child(channel_width_button_);

            {
                auto channel_width_menu = channel_width_button_->get_popup_menu();

                auto callback = [this](uint32_t) {
                    auto selected = channel_width_button_->get_selected_item_index();
                    if (selected.has_value()) {
                        channelWidthMode = selected.value();
                    }
                };
                channel_width_button_->connect_signal("item_selected", callback);

                uint32_t selected = 0;
                for (auto width : CHANNEL_WIDTHS) {
                    channel_width_menu.lock()->create_item(width);
                    int current_index = channel_width_menu.lock()->get_item_count() - 1;
                    if (channelWidthMode == current_index) {
                        selected = current_index;
                    }
                }
                channel_width_button_->select_item(selected);
            }
        }

        {
            auto hbox_container = std::make_shared<revector::HBoxContainer>();
            vbox_container->add_child(hbox_container);

            auto label = std::make_shared<revector::Label>();
            label->set_text(FTR("key"));
            hbox_container->add_child(label);

            auto text_edit = std::make_shared<revector::TextEdit>();
            text_edit->set_editable(false);
            text_edit->set_text(std::filesystem::path(keyPath).filename().string());
            text_edit->container_sizing.expand_h = true;
            text_edit->container_sizing.flag_h = revector::ContainerSizingFlag::Fill;
            hbox_container->add_child(text_edit);

            auto file_dialog = std::make_shared<revector::FileDialog>();
            add_child(file_dialog);

            if (!keyPath.empty()) {
                auto defaultKeyPath = std::filesystem::absolute(keyPath).string();
                file_dialog->set_default_path(defaultKeyPath);
            }

            auto select_button = std::make_shared<revector::Button>();
            select_button->set_text(FTR("open"));

            std::weak_ptr file_dialog_weak = file_dialog;
            std::weak_ptr text_edit_weak = text_edit;
            auto callback = [this, file_dialog_weak, text_edit_weak] {
                auto path = file_dialog_weak.lock()->show();
                if (path.has_value()) {
                    std::filesystem::path p(path.value());
                    text_edit_weak.lock()->set_text(p.filename().string());
                    keyPath = path.value();
                }
            };
            select_button->connect_signal("pressed", callback);
            hbox_container->add_child(select_button);
        }

        {
            play_button_ = std::make_shared<revector::Button>();
            play_button_->container_sizing.expand_h = true;
            play_button_->container_sizing.flag_h = revector::ContainerSizingFlag::Fill;
            update_adapter_start_button_looking(true);

            auto callback1 = [this] {
                bool start = play_button_->get_text() == FTR("start") + " (F5)";

                if (start) {
                    std::optional<DeviceId> target_device_id;
                    for (auto &d : devices_) {
                        if (net_card_name == d.display_name) {
                            target_device_id = d;
                        }
                    }

                    if (target_device_id.has_value()) {
                        bool res =
                            GuiInterface::Start(target_device_id.value(), channel, channelWidthMode, keyPath, codec);
                        if (!res) {
                            start = false;
                        }
                    } else {
                        start = false;
                    }
                } else {
                    GuiInterface::Stop();
                }

                update_adapter_start_button_looking(!start);
            };
            play_button_->connect_signal("pressed", callback1);
            vbox_container->add_child(play_button_);
        }
    }

    // Local tab
    {
        auto margin_container = std::make_shared<revector::MarginContainer>();
        margin_container->set_margin_all(8);
        tab_container_->add_child(margin_container);
        tab_container_->set_tab_title(1, FTR("streaming"));

        auto vbox_container = std::make_shared<revector::VBoxContainer>();
        vbox_container->set_separation(8);
        margin_container->add_child(vbox_container);

        auto hbox_container = std::make_shared<revector::HBoxContainer>();
        vbox_container->add_child(hbox_container);

        auto label = std::make_shared<revector::Label>();
        label->set_text("URL:");
        hbox_container->add_child(label);

        url_edit_ = std::make_shared<revector::TextEdit>();
        url_edit_->set_editable(true);
        url_edit_->set_text(GuiInterface::Instance().ini_[CONFIG_STREAMING][CONFIG_STREAMING_URL]);
        url_edit_->container_sizing.expand_h = true;
        url_edit_->container_sizing.flag_h = revector::ContainerSizingFlag::Fill;
        hbox_container->add_child(url_edit_);

        {
            play_url_button_ = std::make_shared<revector::Button>();
            play_url_button_->container_sizing.expand_h = true;
            play_url_button_->container_sizing.flag_h = revector::ContainerSizingFlag::Fill;
            update_url_start_button_looking(true);

            auto callback1 = [this] {
                bool start = play_url_button_->get_text() == FTR("start") + " (F5)";

                if (start) {
                    GuiInterface::Instance().EmitRtpStream(url_edit_->get_text());
                    GuiInterface::Instance().ini_[CONFIG_STREAMING][CONFIG_STREAMING_URL] = url_edit_->get_text();
                } else {
                    GuiInterface::Instance().EmitUrlStreamShouldStop();
                }

                update_url_start_button_looking(!start);
            };

            play_url_button_->connect_signal("pressed", callback1);
            vbox_container->add_child(play_url_button_);
        }
    }

    // Settings tab
    {
        auto margin_container = std::make_shared<SettingsContainer>();
        tab_container_->add_child(margin_container);
        tab_container_->set_tab_title(2, FTR("settings"));
    }
}

void ControlPanel::custom_input(revector::InputEvent &event) {
    auto input_server = revector::InputServer::get_singleton();

    if (event.type == revector::InputEventType::Key) {
        auto key_args = event.args.key;

        if (key_args.key == revector::KeyCode::F5) {
            if (key_args.pressed) {
                if (tab_container_->get_current_tab().has_value()) {
                    if (tab_container_->get_current_tab().value() == 0) {
                        play_button_->press();
                    } else {
                        play_url_button_->press();
                    }
                }
            }
        }
    }
}
