#include "pch.h"
#ifdef MCCINFO_MONITOR_PLATFORM_WINDOWS
#include "Application.h"

bool g_ApplicationRunning = true;
void *g_Instance = nullptr;

namespace mccinfo {
int main(int argc, char **argv) {
    // mcctp::core::Log::Init();
    mccinfo::core::log::init();

    while (g_ApplicationRunning) {
        //MI_CORE_TRACE("Initialized");
        mccinfo::Application *app = new mccinfo::Application();
        g_Instance = app;
        //  // mcctp_CORE_TRACE("Initialized");
        //  // mcctp_PROFILE_BEGIN_SESSION("Runtime",
        //  "mcctpProfile-Runtime.json");
        app->Run();
        //  // mcctp_PROFILE_END_SESSION();
        delete app;
    }

    return 0;
}
} // namespace mccinfo

#ifdef MCCINFO_MONITOR_RELEASE
#include <Windows.h>
int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance,
                      _In_ LPWSTR lpCmdLine, _In_ int nCmdShow) {
    UNREFERENCED_PARAMETER(hInstance);
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);
    UNREFERENCED_PARAMETER(nCmdShow);

    AllocConsole();

    freopen("CONIN$", "r", stdin);
    freopen("CONOUT$", "w", stdout);
    freopen("CONOUT$", "w", stderr);

    return mccinfo::main(__argc, __argv);
}
#else
int main(int argc, char **argv) {
    return mccinfo::main(__argc, __argv);
}
#endif // MCCTP_EXAMPLE_RELEASE
#endif // MCCTP_EXAMPLE_PLATFORM_WINDOWS