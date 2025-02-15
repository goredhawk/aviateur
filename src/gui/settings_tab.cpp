#include "settings_tab.h"

void SettingsContainer::custom_ready() {
    set_margin_all(8);

    auto vbox_container = std::make_shared<Flint::VBoxContainer>();
    vbox_container->set_separation(8);
    add_child(vbox_container);

    {
        auto hbox_container = std::make_shared<Flint::HBoxContainer>();
        hbox_container->set_separation(8);
        vbox_container->add_child(hbox_container);

        auto label = std::make_shared<Flint::Label>();
        label->set_text(FTR("lang") + ":");
        hbox_container->add_child(label);

        auto lang_menu_button = std::make_shared<Flint::MenuButton>();

        lang_menu_button->container_sizing.expand_h = true;
        lang_menu_button->container_sizing.flag_h = Flint::ContainerSizingFlag::Fill;
        hbox_container->add_child(lang_menu_button);

        if (GuiInterface::Instance().locale_ == "en") {
            lang_menu_button->set_text("English");
        }
        if (GuiInterface::Instance().locale_ == "zh") {
            lang_menu_button->set_text("中文");
        }
        if (GuiInterface::Instance().locale_ == "ru") {
            lang_menu_button->set_text("Русский");
        }

        auto menu = lang_menu_button->get_popup_menu().lock();

        menu->create_item("English");
        menu->create_item("中文");
        menu->create_item("Русский");

        auto callback = [this](uint32_t item_index) {
            GuiInterface::Instance().set_locale("en");

            if (item_index == 1) {
                GuiInterface::Instance().set_locale("zh");
            }
            if (item_index == 2) {
                GuiInterface::Instance().set_locale("ru");
            }
        };
        lang_menu_button->connect_signal("item_selected", callback);
    }

    {
        auto open_capture_folder_button = std::make_shared<Flint::MenuButton>();

        open_capture_folder_button->container_sizing.expand_h = true;
        open_capture_folder_button->container_sizing.flag_h = Flint::ContainerSizingFlag::Fill;
        vbox_container->add_child(open_capture_folder_button);
        open_capture_folder_button->set_text(FTR("open capture folder"));

        auto callback = [this]() {
            ShellExecuteA(NULL, "open", GuiInterface::GetCaptureDir().c_str(), NULL, NULL, SW_SHOWDEFAULT);
        };
        open_capture_folder_button->connect_signal("pressed", callback);
    }

    {
        auto open_appdata_button = std::make_shared<Flint::Button>();

        open_appdata_button->container_sizing.expand_h = true;
        open_appdata_button->container_sizing.flag_h = Flint::ContainerSizingFlag::Fill;
        vbox_container->add_child(open_appdata_button);
        open_appdata_button->set_text(FTR("open appdata folder"));

        auto callback = [this]() {
            auto dir = GuiInterface::GetAppDataDir();
            ShellExecuteA(NULL, "open", dir.c_str(), NULL, NULL, SW_SHOWDEFAULT);
        };
        open_appdata_button->connect_signal("pressed", callback);
    }

    {
        auto open_crash_dumps_button = std::make_shared<Flint::Button>();

        open_crash_dumps_button->container_sizing.expand_h = true;
        open_crash_dumps_button->container_sizing.flag_h = Flint::ContainerSizingFlag::Fill;
        vbox_container->add_child(open_crash_dumps_button);
        open_crash_dumps_button->set_text(FTR("open crash dump folder"));

        auto callback = [this] {
            auto dir = GuiInterface::GetAppDataDir();
            auto path = std::filesystem::path(dir).parent_path().parent_path().parent_path();
            auto appdata_local = path.string() + "\\Local";
            auto dumps_dir = appdata_local + "\\CrashDumps";

            if (std::filesystem::exists(dumps_dir)) {
                ShellExecuteA(NULL, "open", dumps_dir.c_str(), NULL, NULL, SW_SHOWDEFAULT);
            }
        };
        open_crash_dumps_button->connect_signal("pressed", callback);
    }

    {
        auto show_console_btn = std::make_shared<Flint::CheckButton>();

        show_console_btn->container_sizing.expand_h = true;
        show_console_btn->container_sizing.flag_h = Flint::ContainerSizingFlag::Fill;
        vbox_container->add_child(show_console_btn);
        show_console_btn->set_text(FTR("show console"));
        show_console_btn->set_toggle_mode(true);

        auto callback = [this](bool toggled) {
            if (toggled) {
                ShowWindow(GetConsoleWindow(), SW_RESTORE);
            } else {
                ShowWindow(GetConsoleWindow(), SW_HIDE);
            }
        };
        show_console_btn->connect_signal("toggled", callback);
    }
}
