#pragma once

#include "../gui_interface.h"
#include "app.h"

class SettingsContainer : public revector::MarginContainer {
    void custom_ready() override;
};
