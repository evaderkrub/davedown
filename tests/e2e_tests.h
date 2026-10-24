#pragma once

struct ImGuiTestEngine;

namespace dd { struct Workspace; }
namespace dd::ui { class MainWindow; }

namespace dd::e2e {

struct Context {
    Workspace&      workspace;
    ui::MainWindow& ui;
};

void registerTests(ImGuiTestEngine* engine, Context& ctx);

} // namespace dd::e2e
