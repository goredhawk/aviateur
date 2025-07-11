#include "settings_tab.h"

const std::string AVIATEUR_VERSION = "0.1.1";

void open_explorer(const std::string& dir) {
#ifdef _WIN32
    ShellExecuteA(NULL, "open", dir.c_str(), NULL, NULL, SW_SHOWDEFAULT);
#else
    std::string cmd = "xdg-open \"" + dir + "\"";
    system(cmd.c_str());
#endif
}

void SettingsContainer::custom_ready() {
    set_margin_all(8);

    auto vbox_container = std::make_shared<revector::VBoxContainer>();
    vbox_container->set_separation(8);
    add_child(vbox_container);

    {
        auto hbox_container = std::make_shared<revector::HBoxContainer>();
        hbox_container->set_separation(8);
        vbox_container->add_child(hbox_container);

        auto label = std::make_shared<revector::Label>();
        label->set_text(FTR("lang") + ":");
        hbox_container->add_child(label);

        auto lang_menu_button = std::make_shared<revector::MenuButton>();

        lang_menu_button->container_sizing.expand_h = true;
        lang_menu_button->container_sizing.flag_h = revector::ContainerSizingFlag::Fill;
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

#ifdef AVIATEUR_ENABLE_GSTREAMER
    {
        auto decoder_backend_btn = std::make_shared<revector::CheckButton>();
        decoder_backend_btn->set_text(FTR("use gstreamer"));
        vbox_container->add_child(decoder_backend_btn);
        decoder_backend_btn->set_pressed_no_signal(GuiInterface::Instance().use_gstreamer_);
        auto callback = [this](bool toggled) { GuiInterface::Instance().use_gstreamer_ = toggled; };
        decoder_backend_btn->connect_signal("toggled", callback);
    }

#endif

    {
        auto dark_mode_btn = std::make_shared<revector::CheckButton>();
        dark_mode_btn->set_text(FTR("dark mode"));
        vbox_container->add_child(dark_mode_btn);
        dark_mode_btn->set_pressed_no_signal(GuiInterface::Instance().dark_mode_);
        auto callback = [this](bool toggled) { GuiInterface::Instance().dark_mode_ = toggled; };
        dark_mode_btn->connect_signal("toggled", callback);
    }

    {
        auto open_capture_folder_button = std::make_shared<revector::MenuButton>();

        open_capture_folder_button->container_sizing.expand_h = true;
        open_capture_folder_button->container_sizing.flag_h = revector::ContainerSizingFlag::Fill;
        vbox_container->add_child(open_capture_folder_button);
        open_capture_folder_button->set_text(FTR("open capture folder"));

        auto callback = [this]() { open_explorer(GuiInterface::GetCaptureDir()); };
        open_capture_folder_button->connect_signal("pressed", callback);
    }

    {
        auto open_appdata_button = std::make_shared<revector::Button>();

        open_appdata_button->container_sizing.expand_h = true;
        open_appdata_button->container_sizing.flag_h = revector::ContainerSizingFlag::Fill;
        vbox_container->add_child(open_appdata_button);
        open_appdata_button->set_text(FTR("open appdata folder"));

        auto callback = [this]() { open_explorer(GuiInterface::GetAppDataDir()); };
        open_appdata_button->connect_signal("pressed", callback);
    }

#ifdef _WIN32
    {
        auto open_crash_dumps_button = std::make_shared<revector::Button>();

        open_crash_dumps_button->container_sizing.expand_h = true;
        open_crash_dumps_button->container_sizing.flag_h = revector::ContainerSizingFlag::Fill;
        vbox_container->add_child(open_crash_dumps_button);
        open_crash_dumps_button->set_text(FTR("open crash dump folder"));

        auto callback = [this] {
            auto dir = GuiInterface::GetAppDataDir();
            auto path = std::filesystem::path(dir).parent_path().parent_path().parent_path();
            auto appdata_local = path.string() + "\\Local";
            auto dumps_dir = appdata_local + "\\CrashDumps";

            if (std::filesystem::exists(dumps_dir)) {
                open_explorer(dumps_dir);
            }
        };
        open_crash_dumps_button->connect_signal("pressed", callback);
    }

    {
        auto show_console_btn = std::make_shared<revector::CheckButton>();

        show_console_btn->container_sizing.expand_h = true;
        show_console_btn->container_sizing.flag_h = revector::ContainerSizingFlag::Fill;
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
#endif

    {
        auto version_label = std::make_shared<revector::Label>();

        version_label->container_sizing.expand_h = true;
        version_label->container_sizing.flag_h = revector::ContainerSizingFlag::Fill;
        vbox_container->add_child(version_label);
        version_label->set_text(FTR("version") + " " + AVIATEUR_VERSION);
    }
}
