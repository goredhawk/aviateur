#pragma once

#include "../gui_interface.h"
#include "app.h"

constexpr auto DEFAULT_KEY_NAME = "gs.key";

class ControlPanel : public Flint::Panel {
public:
    std::shared_ptr<Flint::MenuButton> dongle_menu_button_;
    std::shared_ptr<Flint::MenuButton> channel_button_;
    std::shared_ptr<Flint::MenuButton> channel_width_button_;
    std::shared_ptr<Flint::Button> refresh_dongle_button_;

    std::string vidPid;
    uint32_t channel = 173;
    uint32_t channelWidthMode = 0;
    std::string keyPath = DEFAULT_KEY_NAME;
    std::string codec = "AUTO";

    std::shared_ptr<Flint::Button> play_button_;

    std::shared_ptr<Flint::Button> play_url_button_;
    std::shared_ptr<Flint::TextEdit> url_edit_;

    std::shared_ptr<Flint::TabContainer> tab_container_;

    void update_dongle_list();

    void update_adapter_start_button_looking(bool start_status) const;

    void update_url_start_button_looking(bool start_status) const;

    void custom_ready() override;

    void custom_input(Flint::InputEvent &event) override;
};
