#pragma once

#include "app.h"

class TipLabel : public Flint::Label {
public:
    float display_time = 1;
    float fade_time = 0.5;

    std::shared_ptr<Flint::Timer> display_timer;
    std::shared_ptr<Flint::Timer> fade_timer;

    void custom_ready() override;

    void custom_update(double dt) override;

    void show_tip(const std::string& tip);
};
