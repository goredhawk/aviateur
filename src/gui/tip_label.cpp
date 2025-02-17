#include "tip_label.h"

void TipLabel::custom_ready() {
    set_font_size(48);

    auto style_box = Flint::StyleBox();
    style_box.bg_color = Flint::ColorU(50, 50, 50, 200);
    style_box.corner_radius = 8;
    theme_background = style_box;

    set_text_style(Flint::TextStyle{Flint::ColorU(201, 79, 79)});

    display_timer = std::make_shared<Flint::Timer>();
    fade_timer = std::make_shared<Flint::Timer>();

    add_child(display_timer);
    add_child(fade_timer);

    auto callback = [this] { fade_timer->start_timer(fade_time); };
    display_timer->connect_signal("timeout", callback);

    auto callback2 = [this] { set_visibility(false); };
    fade_timer->connect_signal("timeout", callback2);
}

void TipLabel::custom_update(double dt) {
    if (!fade_timer->is_stopped()) {
        alpha = fade_timer->get_remaining_time() / fade_time;
    }
}

void TipLabel::show_tip(const std::string& tip) {
    if (!display_timer->is_stopped()) {
        display_timer->stop();
    }
    if (!fade_timer->is_stopped()) {
        fade_timer->stop();
    }
    set_text(tip);
    set_visibility(true);
    alpha = 1;
    display_timer->start_timer(display_time);
}
