#include "settings_tab.h"

const std::string AVIATEUR_REVISION_NUM = "476912f90fad60245031d1700b97e410611e2ab9";

void open_explorer(const std::string& dir) {
#ifdef __WIN32
    ShellExecuteA(NULL, "open", dir.c_str(), NULL, NULL, SW_SHOWDEFAULT);
#else
    std::string cmd = "xdg-open \"" + dir + "\"";
    system(cmd.c_str());
#endif
}

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

    // #ifdef __WIN32

    {
        auto open_capture_folder_button = std::make_shared<Flint::MenuButton>();

        open_capture_folder_button->container_sizing.expand_h = true;
        open_capture_folder_button->container_sizing.flag_h = Flint::ContainerSizingFlag::Fill;
        vbox_container->add_child(open_capture_folder_button);
        open_capture_folder_button->set_text(FTR("open capture folder"));

        auto callback = [this]() { open_explorer(GuiInterface::GetCaptureDir()); };
        open_capture_folder_button->connect_signal("pressed", callback);
    }

    {
        auto open_appdata_button = std::make_shared<Flint::Button>();

        open_appdata_button->container_sizing.expand_h = true;
        open_appdata_button->container_sizing.flag_h = Flint::ContainerSizingFlag::Fill;
        vbox_container->add_child(open_appdata_button);
        open_appdata_button->set_text(FTR("open appdata folder"));

        auto callback = [this]() { open_explorer(GuiInterface::GetAppDataDir()); };
        open_appdata_button->connect_signal("pressed", callback);
    }

#ifdef __WIN32
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
                open_explorer(dumps_dir);
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
#endif

    {
        auto copy_version_button = std::make_shared<Flint::Button>();

        copy_version_button->container_sizing.expand_h = true;
        copy_version_button->container_sizing.flag_h = Flint::ContainerSizingFlag::Fill;
        vbox_container->add_child(copy_version_button);
        copy_version_button->set_text(FTR("copy version num"));

        auto callback = [this] {
            auto input_server = Flint::InputServer::get_singleton();
            input_server->set_clipboard(0, AVIATEUR_REVISION_NUM);
        };
        copy_version_button->connect_signal("pressed", callback);
    }
}
