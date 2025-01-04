#include "app.h"
#include "player/RealTimePlayer.h"

#include <errhandlingapi.h>
#include <fileapi.h>
#include <handleapi.h>
#include <processthreadsapi.h>
#include <servers/render_server.h>
#include <winnt.h>

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
    CreateDumpFile(LPCWSTR(L"dump.dmp"), pException);
    return EXCEPTION_EXECUTE_HANDLER;
}

#endif

class MyNode : public Flint::Node {
    std::shared_ptr<RealTimePlayer> player;

    void custom_ready() override {
auto render_server = Flint::RenderServer::get_singleton();
        player = std::make_any<RealTimePlayer>(render_server->device_, render_server->queue_);
    }
};

int main() {
#ifdef DEBUG_MODE
    SetUnhandledExceptionFilter(ApplicationCrashHandler);
#endif

    Flint::App app({ 1280, 720 });

    app.get_tree()->replace_root(std::make_shared<MyNode>());

    app.main_loop();

    return EXIT_SUCCESS;
}
