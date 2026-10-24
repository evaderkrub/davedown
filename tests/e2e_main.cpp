// Headless end-to-end harness: the real interface (src/ui) driven by the
// Dear ImGui Test Engine with no window and no renderer. Fonts are loaded
// from the staged assets folder beside this executable, exactly as the
// application does, so layout matches what a person sees.
#include "e2e_tests.h"

#include "app/Workspace.h"
#include "platform/Paths.h"
#include "ui/Fonts.h"
#include "ui/MainWindow.h"
#include "ui/Theme.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_test_engine/imgui_te_context.h"
#include "imgui_test_engine/imgui_te_engine.h"
#include "imgui_test_engine/imgui_te_exporters.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace {

void runOneFrame(ImGuiTestEngine* engine, dd::ui::MainWindow& ui)
{
    ImGuiIO& io = ImGui::GetIO();
    io.DeltaTime = 1.0f / 60.0f;
    ui.pumpAppearance(1.0f);
    ImGui::NewFrame();
    dd::ui::UiFrame frame;
    frame.nativeWindow = nullptr;
    frame.dpiScale = 1.0f;
    ui.draw(frame);
    ImGui::Render();
    ImGuiTestEngine_PostSwap(engine);
}

} // namespace

int main(int argc, char** argv)
{
    std::string filter = "all";
    std::string junit;
    bool verbose = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--junit") == 0 && i + 1 < argc) junit = argv[++i];
        else if (std::strcmp(argv[i], "--verbose") == 0) verbose = true;
        else if (argv[i][0] != '-') filter = argv[i];
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NavEnableKeyboard;
    io.DisplaySize = ImVec2(1400.0f, 900.0f);
    io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;

    std::string fontError;
    const std::string fontDir = dd::platform::resolve("assets/fonts");
    if (!dd::ui::fonts::initialize(io, fontDir, 18.0f, fontError)) {
        std::fprintf(stderr, "e2e: %s (build the davedown target first so assets are staged)\n", fontError.c_str());
        io.Fonts->AddFontDefault();
    }
    // No renderer: build the legacy atlas ourselves, as a backend would.
    unsigned char* pixels = nullptr;
    int texW = 0, texH = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &texW, &texH);
    io.Fonts->SetTexID(0);

    dd::Workspace workspace;
    dd::ui::applyAppearanceNow(0, 1.0f, 1.0f);
    dd::ui::MainWindow ui(workspace);
    ui.initialize("# Welcome\n\nThis is the welcome document.\n");

    ImGuiTestEngine* engine = ImGuiTestEngine_CreateContext();
    ImGuiTestEngineIO& eio = ImGuiTestEngine_GetIO(engine);
    eio.ConfigVerboseLevel = verbose ? ImGuiTestVerboseLevel_Debug : ImGuiTestVerboseLevel_Warning;
    eio.ConfigVerboseLevelOnError = ImGuiTestVerboseLevel_Debug;
    eio.ConfigRunSpeed = ImGuiTestRunSpeed_Fast;
    eio.ConfigNoThrottle = true;
    eio.ConfigLogToTTY = true;
    ImGuiTestEngine_Start(engine, ImGui::GetCurrentContext());
    ImGuiTestEngine_InstallDefaultCrashHandler();

    dd::e2e::Context ctxData{workspace, ui};
    dd::e2e::registerTests(engine, ctxData);

    ImGuiTestEngine_QueueTests(engine, ImGuiTestGroup_Tests, filter.c_str(), ImGuiTestRunFlags_RunFromCommandLine);

    // Warm-up frames so the dock layout exists before any test looks for windows.
    for (int i = 0; i < 3; ++i) runOneFrame(engine, ui);
    int guard = 0;
    while (!ImGuiTestEngine_IsTestQueueEmpty(engine) && guard++ < 200000)
        runOneFrame(engine, ui);

    int tested = 0, success = 0;
    ImGuiTestEngineResultSummary summary;
    ImGuiTestEngine_GetResultSummary(engine, &summary);
    tested = summary.CountTested;
    success = summary.CountSuccess;
    if (!junit.empty()) {
        eio.ExportResultsFilename = junit.c_str();
        eio.ExportResultsFormat = ImGuiTestEngineExportFormat_JUnitXml;
        ImGuiTestEngine_Export(engine);
    }
    std::printf("davedown_e2e: %d/%d tests passed\n", success, tested);

    ImGuiTestEngine_Stop(engine);
    ImGui::DestroyContext();
    ImGuiTestEngine_DestroyContext(engine);
    return (tested > 0 && success == tested) ? 0 : 1;
}
