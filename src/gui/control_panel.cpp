#include "control_panel.h"

void ControlPanel::update_dongle_list() {
    auto menu = dongle_menu_button_->get_popup_menu().lock();

    auto dongles = GuiInterface::GetDongleList();

    menu->clear_items();

    bool previous_device_exists = false;
    for (const auto &dongle : dongles) {
        if (vidPid == dongle) {
            previous_device_exists = true;
        }
        menu->create_item(dongle);
    }

    if (!previous_device_exists) {
        vidPid = "";
    }
}

void ControlPanel::update_adapter_start_button_looking(bool start_status) const {
    tab_container_->set_tab_disabled(!start_status);

    if (!start_status) {
        auto red = Flint::ColorU(201, 79, 79);
        play_button_->theme_normal.bg_color = red;
        play_button_->theme_hovered.bg_color = red;
        play_button_->theme_pressed.bg_color = red;
        play_button_->set_text("Stop (F5)");
    } else {
        auto green = Flint::ColorU(78, 135, 82);
        play_button_->theme_normal.bg_color = green;
        play_button_->theme_hovered.bg_color = green;
        play_button_->theme_pressed.bg_color = green;
        play_button_->set_text("Start (F5)");
    }
}

void ControlPanel::update_url_start_button_looking(bool start_status) const {
    tab_container_->set_tab_disabled(!start_status);

    if (!start_status) {
        auto red = Flint::ColorU(201, 79, 79);
        play_url_button_->theme_normal.bg_color = red;
        play_url_button_->theme_hovered.bg_color = red;
        play_url_button_->theme_pressed.bg_color = red;
        play_url_button_->set_text("Close (F5)");
    } else {
        auto green = Flint::ColorU(78, 135, 82);
        play_url_button_->theme_normal.bg_color = green;
        play_url_button_->theme_hovered.bg_color = green;
        play_url_button_->theme_pressed.bg_color = green;
        play_url_button_->set_text("Open (F5)");
    }
}

void ControlPanel::custom_ready() {
    auto &ini = GuiInterface::Instance().ini_;
    vidPid = ini[CONFIG_ADAPTER][ADAPTER_DEVICE];
    channel = std::stoi(ini[CONFIG_ADAPTER][ADAPTER_CHANNEL]);
    channelWidthMode = std::stoi(ini[CONFIG_ADAPTER][ADAPTER_CHANNEL_WIDTH_MODE]);
    keyPath = ini[CONFIG_ADAPTER][ADAPTER_CHANNEL_KEY];
    codec = ini[CONFIG_ADAPTER][ADAPTER_CHANNEL_CODEC];

    theme_panel_->border_width = 0;

    tab_container_ = std::make_shared<Flint::TabContainer>();
    add_child(tab_container_);
    tab_container_->set_anchor_flag(Flint::AnchorFlag::FullRect);

    // Wi-Fi adapter tab
    {
        auto margin_container = std::make_shared<Flint::MarginContainer>();
        margin_container->set_margin_all(8);
        tab_container_->add_child(margin_container);
        tab_container_->set_tab_title(0, "Adapter");

        auto vbox_container = std::make_shared<Flint::VBoxContainer>();
        vbox_container->set_separation(8);
        margin_container->add_child(vbox_container);

        {
            auto hbox_container = std::make_shared<Flint::HBoxContainer>();
            hbox_container->set_separation(8);
            vbox_container->add_child(hbox_container);

            auto label = std::make_shared<Flint::Label>();
            label->set_text("Adapter ID:");
            hbox_container->add_child(label);

            dongle_menu_button_ = std::make_shared<Flint::MenuButton>();

            dongle_menu_button_->container_sizing.expand_h = true;
            dongle_menu_button_->container_sizing.flag_h = Flint::ContainerSizingFlag::Fill;
            hbox_container->add_child(dongle_menu_button_);

            // Do this before setting dongle button text.
            update_dongle_list();
            dongle_menu_button_->set_text(vidPid);

            auto callback = [this](uint32_t) { vidPid = dongle_menu_button_->get_selected_item_text(); };
            dongle_menu_button_->connect_signal("item_selected", callback);

            refresh_dongle_button_ = std::make_shared<Flint::Button>();
            auto icon = std::make_shared<Flint::VectorImage>("assets/Refresh.svg");
            refresh_dongle_button_->set_icon_normal(icon);
            refresh_dongle_button_->set_text("");
            hbox_container->add_child(refresh_dongle_button_);

            auto callback2 = [this]() { update_dongle_list(); };
            refresh_dongle_button_->connect_signal("pressed", callback2);
        }

        {
            auto hbox_container = std::make_shared<Flint::HBoxContainer>();
            vbox_container->add_child(hbox_container);

            auto label = std::make_shared<Flint::Label>();
            label->set_text("Channel:");
            hbox_container->add_child(label);

            channel_button_ = std::make_shared<Flint::MenuButton>();
            channel_button_->container_sizing.expand_h = true;
            channel_button_->container_sizing.flag_h = Flint::ContainerSizingFlag::Fill;
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
            auto hbox_container = std::make_shared<Flint::HBoxContainer>();
            vbox_container->add_child(hbox_container);

            auto label = std::make_shared<Flint::Label>();
            label->set_text("Channel Width:");
            hbox_container->add_child(label);

            channel_width_button_ = std::make_shared<Flint::MenuButton>();
            channel_width_button_->container_sizing.expand_h = true;
            channel_width_button_->container_sizing.flag_h = Flint::ContainerSizingFlag::Fill;
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
            auto hbox_container = std::make_shared<Flint::HBoxContainer>();
            vbox_container->add_child(hbox_container);

            auto label = std::make_shared<Flint::Label>();
            label->set_text("Key:");
            hbox_container->add_child(label);

            auto text_edit = std::make_shared<Flint::TextEdit>();
            text_edit->set_editable(false);
            text_edit->set_text(keyPath);
            text_edit->container_sizing.expand_h = true;
            text_edit->container_sizing.flag_h = Flint::ContainerSizingFlag::Fill;
            hbox_container->add_child(text_edit);

            auto file_dialog = std::make_shared<Flint::FileDialog>();
            add_child(file_dialog);
            file_dialog->set_default_path(std::filesystem::absolute(keyPath).string());

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
            play_button_->container_sizing.expand_h = true;
            play_button_->container_sizing.flag_h = Flint::ContainerSizingFlag::Fill;
            update_adapter_start_button_looking(true);

            auto callback1 = [this] {
                bool start = play_button_->get_text() == "Start (F5)";

                if (start) {
                    bool res = GuiInterface::Start(vidPid, channel, channelWidthMode, keyPath, codec);
                    if (!res) {
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
        auto margin_container = std::make_shared<Flint::MarginContainer>();
        margin_container->set_margin_all(8);
        tab_container_->add_child(margin_container);
        tab_container_->set_tab_title(1, "URL");

        auto vbox_container = std::make_shared<Flint::VBoxContainer>();
        vbox_container->set_separation(8);
        margin_container->add_child(vbox_container);

        auto hbox_container = std::make_shared<Flint::HBoxContainer>();
        vbox_container->add_child(hbox_container);

        auto label = std::make_shared<Flint::Label>();
        label->set_text("URL:");
        hbox_container->add_child(label);

        url_edit_ = std::make_shared<Flint::TextEdit>();
        url_edit_->set_editable(true);
        url_edit_->set_text("udp://239.0.0.1:1234");
        url_edit_->container_sizing.expand_h = true;
        url_edit_->container_sizing.flag_h = Flint::ContainerSizingFlag::Fill;
        hbox_container->add_child(url_edit_);

        {
            play_url_button_ = std::make_shared<Flint::Button>();
            play_url_button_->container_sizing.expand_h = true;
            play_url_button_->container_sizing.flag_h = Flint::ContainerSizingFlag::Fill;
            update_url_start_button_looking(true);

            auto callback1 = [this] {
                bool start = play_url_button_->get_text() == "Open (F5)";

                if (start) {
                    // Hw decoding always crashes when playing URL streams.
                    GuiInterface::Instance().EmitRtpStream(url_edit_->get_text());
                } else {
                    GuiInterface::Instance().EmitUrlStreamShouldStop();
                }

                update_url_start_button_looking(!start);
            };

            play_url_button_->connect_signal("pressed", callback1);
            vbox_container->add_child(play_url_button_);
        }
    }
}

void ControlPanel::custom_input(Flint::InputEvent &event) {
    auto input_server = Flint::InputServer::get_singleton();

    if (event.type == Flint::InputEventType::Key) {
        auto key_args = event.args.key;

        if (key_args.key == Flint::KeyCode::F5) {
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
