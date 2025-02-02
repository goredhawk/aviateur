#pragma once

#include "../gui_interface.h"
#include "app.h"


class SettingsContainer : public Flint::MarginContainer {
    void custom_ready() override;
};
