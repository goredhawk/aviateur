#include "src/QmlNativeAPI.h"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <player/QQuickRealTimePlayer.h>
#include "app.h"

#pragma comment(lib, "ws2_32.lib")

#ifdef DEBUG_MODE
#include <DbgHelp.h>
#pragma comment(lib, "DbgHelp.lib")
// 创建Dump文件
void CreateDumpFile(LPCWSTR lpstrDumpFilePathName, EXCEPTION_POINTERS *pException) {
    HANDLE hDumpFile = CreateFile(
        reinterpret_cast<LPCSTR>(lpstrDumpFilePathName), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    // Dump信息
    MINIDUMP_EXCEPTION_INFORMATION dumpInfo;
    dumpInfo.ExceptionPointers = pException;
    dumpInfo.ThreadId = GetCurrentThreadId();
    dumpInfo.ClientPointers = TRUE;
    // 写入Dump文件内容
    MiniDumpWriteDump(
        GetCurrentProcess(), GetCurrentProcessId(), hDumpFile, MiniDumpNormal, &dumpInfo, nullptr, nullptr);
    CloseHandle(hDumpFile);
}
// 处理Unhandled Exception的回调函数
LONG ApplicationCrashHandler(EXCEPTION_POINTERS *pException) {
    CreateDumpFile(L"dump.dmp", pException);
    return EXCEPTION_EXECUTE_HANDLER;
}

#endif


class MyNode : public Flint::Node {
    void custom_ready() override {
        auto collasping_panel = std::make_shared<Flint::CollapseContainer>();
        collasping_panel->set_position({400, 200});
        collasping_panel->set_size({500, 400});
        add_child(collasping_panel);

        auto vbox = std::make_shared<Flint::VBoxContainer>();
        collasping_panel->add_child(vbox);

        auto label = std::make_shared<Flint::Label>();
        label->set_text("This is a label");
        label->container_sizing.expand_h = true;
        label->container_sizing.flag_h = Flint::ContainerSizingFlag::Fill;
        vbox->add_child(label);

        auto collasping_panel2 = std::make_shared<Flint::CollapseContainer>();
        collasping_panel2->set_color(Flint::ColorU{201, 79, 79});
        vbox->add_child(collasping_panel2);

        auto button = std::make_shared<Flint::Button>();
        collasping_panel2->add_child(button);

        auto collasping_panel3 = std::make_shared<Flint::CollapseContainer>();
        collasping_panel3->set_color(Flint::ColorU{66, 105, 183});
        vbox->add_child(collasping_panel3);
    }
};

int main() {
#ifdef DEBUG_MODE
    SetUnhandledExceptionFilter((LPTOP_LEVEL_EXCEPTION_FILTER)ApplicationCrashHandler);
#endif

    Flint::App app({1280, 720});

    app.get_tree()->replace_root(std::make_shared<MyNode>());

    app.main_loop();

    return EXIT_SUCCESS;
}
