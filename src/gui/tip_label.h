#pragma once

#include "app.h"

class TipLabel : public revector::Label {
public:
    float display_time = 1.5;
    float fade_time = 0.5;

    std::shared_ptr<revector::Timer> display_timer;
    std::shared_ptr<revector::Timer> fade_timer;

    void custom_ready() override;

    void custom_update(double dt) override;

    void show_tip(const std::string& tip);
};
