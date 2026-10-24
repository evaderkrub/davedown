#include "ui/Widgets.h"

#include "ui/Fonts.h"
#include "ui/Theme.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <string>

namespace dd::ui {

MonoScope::MonoScope()
{
    if (ImFont* mono = fonts::code()) {
        ImGui::PushFont(mono, 0.0f);
        pushed_ = true;
    }
}

MonoScope::~MonoScope()
{
    if (pushed_) ImGui::PopFont();
}

namespace {

ImU32 fillFor(const Palette& p, Kind k)
{
    switch (k) {
    case KindOk:     return p.successFill;
    case KindWarn:   return p.warningFill;
    case KindError:  return p.dangerFill;
    case KindAccent: return p.accentFill;
    default:         return p.neutralFill;
    }
}

ImU32 strokeFor(const Palette& p, Kind k)
{
    switch (k) {
    case KindOk:     return p.success;
    case KindWarn:   return p.warning;
    case KindError:  return p.danger;
    case KindAccent: return p.accent;
    default:         return p.neutral;
    }
}

void textV(ImU32 col, const char* fmt, va_list args)
{
    ImGui::PushStyleColor(ImGuiCol_Text, col);
    ImGui::TextV(fmt, args);
    ImGui::PopStyleColor();
}

} // namespace

bool beginCard(const char* id, ImVec2 size, ImGuiWindowFlags flags)
{
    const Palette& p = palette();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, p.surface);
    ImGui::PushStyleColor(ImGuiCol_Border, p.border);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, ImGui::GetStyle().ChildRounding);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImGui::GetStyle().WindowPadding);
    const bool open = ImGui::BeginChild(id, size, ImGuiChildFlags_Borders, flags);
    if (!open) {
        // BeginChild always needs its EndChild, but the caller only calls
        // endCard() when we return true -- so unwind here.
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(2);
    }
    return open;
}

void endCard()
{
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

void sectionHeader(const char* text)
{
    const Palette& p = palette();
    ImGui::PushFont(fonts::uiBold(), 0.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, p.heading);
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::Separator();
}

void statusPill(const char* label, Kind kind)
{
    const Palette& p = palette();
    const ImVec2 pad(8.0f, 2.0f);
    const ImVec2 sz = ImGui::CalcTextSize(label);
    const ImVec2 mn = ImGui::GetCursorScreenPos();
    const ImVec2 mx(mn.x + sz.x + pad.x * 2.0f, mn.y + sz.y + pad.y * 2.0f);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float r = (mx.y - mn.y) * 0.5f;
    dl->AddRectFilled(mn, mx, fillFor(p, kind), r);
    dl->AddRect(mn, mx, strokeFor(p, kind), r);
    dl->AddText(ImVec2(mn.x + pad.x, mn.y + pad.y), strokeFor(p, kind), label);
    ImGui::Dummy(ImVec2(mx.x - mn.x, mx.y - mn.y));
}

bool accentButton(const char* label, ImVec2 size)
{
    const Palette& p = palette();
    ImGui::PushStyleColor(ImGuiCol_Button, p.accent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, p.accentHovered);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, p.accentActive);
    ImGui::PushStyleColor(ImGuiCol_Text, p.accentText);
    const bool b = ImGui::Button(label, size);
    ImGui::PopStyleColor(4);
    return b;
}

bool dangerButton(const char* label, ImVec2 size)
{
    const Palette& p = palette();
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, p.dangerFill);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, p.danger);
    ImGui::PushStyleColor(ImGuiCol_Text, p.danger);
    ImGui::PushStyleColor(ImGuiCol_Border, p.danger);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    const bool b = ImGui::Button(label, size);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(5);
    return b;
}

bool iconButton(const char* id, const char* icon, const char* tooltip, bool enabled)
{
    const Palette& p = palette();
    char label[128];
    std::snprintf(label, sizeof(label), "%s###%s", icon ? icon : "", id);
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, p.surfaceHovered);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, p.surfaceActive);
    ImGui::PushStyleColor(ImGuiCol_Text, enabled ? p.text : p.textFaint);
    ImGui::BeginDisabled(!enabled);
    const bool clicked = ImGui::Button(label);
    ImGui::EndDisabled();
    ImGui::PopStyleColor(4);
    if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", tooltip);
    return clicked;
}

void mutedText(const char* fmt, ...)
{
    va_list a; va_start(a, fmt);
    textV(palette().textMuted, fmt, a);
    va_end(a);
}

void faintText(const char* fmt, ...)
{
    va_list a; va_start(a, fmt);
    textV(palette().textFaint, fmt, a);
    va_end(a);
}

bool segmentedControl(const char* id, const char* const* icons, const char* const* labels,
                      const char* const* tooltips, int count, int* selected, Kind kind)
{
    if (count <= 0 || !selected) return false;
    const Palette& p = palette();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float pad = 3.0f;
    bool changed = false;

    ImVector<ImVec2> mins, maxs;
    mins.resize(count);
    maxs.resize(count);

    ImGui::PushID(id);
    dl->ChannelsSplit(2);
    dl->ChannelsSetCurrent(1);
    ImGui::BeginGroup();
    for (int i = 0; i < count; ++i) {
        if (i > 0) ImGui::SameLine(0.0f, 2.0f);
        char itemId[16];
        std::snprintf(itemId, sizeof(itemId), "%d", i);
        ImGui::PushID(itemId);   // string ID so a test can address "mode/1/item"
        const bool isSel = (*selected == i);
        ImGui::PushStyleColor(ImGuiCol_Button, isSel ? p.surfaceActive : IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, p.surfaceHovered);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, p.surfaceActive);
        ImGui::PushStyleColor(ImGuiCol_Text, isSel ? p.text : p.textMuted);
        std::string text;
        if (icons && icons[i]) text += icons[i];
        if (labels && labels[i] && labels[i][0]) {
            if (!text.empty()) text += " ";
            text += labels[i];
        }
        text += "###item";
        const bool clicked = ImGui::Button(text.c_str());
        ImGui::PopStyleColor(4);
        mins[i] = ImGui::GetItemRectMin();
        maxs[i] = ImGui::GetItemRectMax();
        if (tooltips && tooltips[i] && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("%s", tooltips[i]);
        if (clicked && *selected != i) { *selected = i; changed = true; }
        ImGui::PopID();
    }
    ImGui::EndGroup();

    ImVec2 gMin = ImGui::GetItemRectMin();
    ImVec2 gMax = ImGui::GetItemRectMax();
    gMin.x -= pad; gMin.y -= pad;
    gMax.x += pad; gMax.y += pad;

    dl->ChannelsSetCurrent(0);
    dl->AddRectFilled(gMin, gMax, fillFor(p, kind), 7.0f);
    dl->ChannelsMerge();
    dl->AddRect(gMin, gMax, withAlpha(strokeFor(p, kind), 140), 7.0f, 0, 1.0f);

    if (*selected >= 0 && *selected < count)
        dl->AddRect(mins[*selected], maxs[*selected], withAlpha(p.accent, 220), 5.0f, 0, 1.5f);

    ImGui::PopID();
    return changed;
}

void centeredLabel(const char* text)
{
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(text);
}

void hairline()
{
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    ImGui::GetWindowDrawList()->AddLine(p0, ImVec2(p0.x + w, p0.y), palette().border, 1.0f);
    ImGui::Dummy(ImVec2(w, 1.0f));
}

} // namespace dd::ui
