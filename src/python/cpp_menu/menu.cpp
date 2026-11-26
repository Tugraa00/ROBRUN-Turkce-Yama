#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <windows.h>
#include "menu.h"

namespace {
ImFont* buttonFont = nullptr;
ImFont* brandFont = nullptr;

void DrawBrandMark(ImDrawList* d, ImVec2 o, float s, ImU32 ink, ImU32 accent) {
    ImVec2 left[4] = { { o.x + s * 0.04f, o.y + s * 0.08f }, { o.x + s * 0.34f, o.y + s * 0.08f }, { o.x + s * 0.50f, o.y + s * 0.94f }, { o.x + s * 0.22f, o.y + s * 0.94f } };
    ImVec2 upper[4] = { { o.x + s * 0.56f, o.y + s * 0.08f }, { o.x + s * 0.92f, o.y + s * 0.08f }, { o.x + s * 0.78f, o.y + s * 0.40f }, { o.x + s * 0.46f, o.y + s * 0.40f } };
    ImVec2 lower[4] = { { o.x + s * 0.44f, o.y + s * 0.52f }, { o.x + s * 0.72f, o.y + s * 0.52f }, { o.x + s * 0.90f, o.y + s * 0.94f }, { o.x + s * 0.62f, o.y + s * 0.94f } };
    ImVec2 gem[4] = { { o.x + s * 0.50f, o.y + s * 0.40f }, { o.x + s * 0.58f, o.y + s * 0.48f }, { o.x + s * 0.50f, o.y + s * 0.56f }, { o.x + s * 0.42f, o.y + s * 0.48f } };
    d->AddConvexPolyFilled(left, 4, ink);
    d->AddConvexPolyFilled(upper, 4, ink);
    d->AddConvexPolyFilled(lower, 4, ink);
    d->AddConvexPolyFilled(gem, 4, accent);
}

bool ActionButton(const char* text, const ImVec2& size = ImVec2(0, 44), bool enabled = true) {
    static std::unordered_map<ImGuiID, float> hoverStates;
    ImVec2 actual = size;
    const ImVec2 label = buttonFont ? buttonFont->CalcTextSizeA(16.f, 10000.f, 0.f, text) : ImGui::CalcTextSize(text);
    if (actual.x == 0) actual.x = label.x + 30;
    else if (actual.x < 0) actual.x = std::max(1.f, ImGui::GetContentRegionAvail().x + actual.x);
    if (actual.y == 0) actual.y = label.y + 18;

    ImGuiID id = ImGui::GetID(text);
    ImGui::InvisibleButton(text, actual);
    const bool hovered = enabled && ImGui::IsItemHovered();
    const bool held = enabled && ImGui::IsItemActive();
    const bool clicked = enabled && ImGui::IsItemClicked();

    float& mix = hoverStates[id];
    const float target = hovered ? 1.f : 0.f;
    mix += (target - mix) * (1.f - std::exp(-ImGui::GetIO().DeltaTime * 15.f));

    ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float pulse = .92f + .08f * std::sin(static_cast<float>(ImGui::GetTime()) * 2.1f);
    if (mix > .01f) {
        for (int i = 4; i > 0; --i) {
            draw->AddRect(ImVec2(a.x - i, a.y - i), ImVec2(b.x + i, b.y + i),
                          IM_COL32(176, 42, 54, static_cast<int>(mix * (5 - i) * 5 * pulse)), 11, 0, 1.1f);
        }
    }

    const float alpha = enabled ? (held ? .98f : .94f) : 0.45f;
    const ImU32 base = ImGui::GetColorU32(ImVec4(.055f + mix * .11f, .018f + mix * .02f, .022f + mix * .024f, alpha));
    draw->AddRectFilled(a, b, base, 10);
    draw->AddRect(a, b, IM_COL32(168, 52, 62, enabled ? static_cast<int>(50 + mix * 110) : 30), 10, 0, 1.f + mix * .6f);

    const ImU32 textCol = enabled ? IM_COL32(247, 236, 236, 255) : IM_COL32(150, 130, 130, 200);
    const ImVec2 tp(a.x + (actual.x - label.x) * .5f, a.y + (actual.y - label.y) * .5f + (held ? 1.f : 0.f));
    if (buttonFont) draw->AddText(buttonFont, 16.f, tp, textCol, text);
    else draw->AddText(tp, textCol, text);
    return clicked;
}

ImFont* AddEmbeddedFont(ImGuiIO& io, WORD resourceId, float pixels) {
    static const ImWchar turkishRanges[] = {
        0x0020, 0x00FF,
        0x0100, 0x017F,
        0x2000, 0x206F,
        0
    };
    HMODULE module = GetModuleHandleW(nullptr);
    HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(resourceId), RT_RCDATA);
    if (!resource) return nullptr;
    HGLOBAL loaded = LoadResource(module, resource);
    const DWORD bytes = SizeofResource(module, resource);
    const void* data = loaded ? LockResource(loaded) : nullptr;
    if (!data || bytes == 0) return nullptr;
    ImFontConfig config{};
    config.FontDataOwnedByAtlas = false;
    config.OversampleH = 2;
    config.OversampleV = 2;
    config.PixelSnapH = false;
    return io.Fonts->AddFontFromMemoryTTF(const_cast<void*>(data), static_cast<int>(bytes), pixels, &config, turkishRanges);
}

void DrawRootBackground(const ImVec2& size) {
    ImDrawList* d = ImGui::GetBackgroundDrawList();
    d->AddRectFilledMultiColor(ImVec2(0, 0), size, IM_COL32(8, 4, 5, 255), IM_COL32(8, 4, 5, 255),
                               IM_COL32(32, 6, 8, 255), IM_COL32(32, 6, 8, 255));
    ImVec2 glow(size.x * .5f, size.y * 1.02f);
    for (int i = 24; i > 0; --i) {
        d->AddCircleFilled(glow, 40.f + i * 18.f, IM_COL32(128, 18, 26, static_cast<int>((25 - i) * .24f)), 72);
    }
}

void DrawBrand() {
    ImVec2 pos = ImGui::GetCursorScreenPos();
    const char* name = "R O B  R U N";
    const float mark = 30.f;
    ImDrawList* d = ImGui::GetWindowDrawList();
    DrawBrandMark(d, ImVec2(pos.x + 2, pos.y + 5), mark, IM_COL32(247, 248, 248, 255), IM_COL32(186, 40, 52, 255));
    const float tx = pos.x + mark + 12.f;
    if (brandFont) d->AddText(brandFont, 27.0f, ImVec2(tx, pos.y + 4), IM_COL32(247, 248, 248, 255), name);
    else d->AddText(ImVec2(tx, pos.y + 9), IM_COL32(247, 248, 248, 255), name);
}

void DrawWindowButtons(float width) {
    ImGui::SetCursorScreenPos(ImVec2(width - 92, 15));
    if (ActionButton("—", ImVec2(34, 30))) ShowWindow(gWindow, SW_MINIMIZE);
    ImGui::SameLine(0, 7);
    if (ActionButton("×", ImVec2(34, 30))) PostMessageW(gWindow, WM_CLOSE, 0, 0);
}

} // namespace

void MenuApplyStyle() {
    ImGuiIO& io = ImGui::GetIO();
    if (ImFont* font = AddEmbeddedFont(io, 101, 17.0f)) io.FontDefault = font;
    buttonFont = AddEmbeddedFont(io, 102, 16.0f);
    brandFont = AddEmbeddedFont(io, 102, 27.0f);
    ImGui::StyleColorsDark();
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 16;
    s.ChildRounding = 14;
    s.FrameRounding = 10;
    s.WindowPadding = ImVec2(18, 16);
    s.ItemSpacing = ImVec2(12, 11);
    s.FramePadding = ImVec2(12, 10);
    s.Colors[ImGuiCol_WindowBg] = kBg;
    s.Colors[ImGuiCol_ChildBg] = kPanel;
    s.Colors[ImGuiCol_Border] = ImVec4(.42f, .20f, .22f, .34f);
    s.Colors[ImGuiCol_Text] = ImVec4(.96f, .93f, .93f, 1);
    s.Colors[ImGuiCol_TextDisabled] = kMuted;
}

void MenuShutdownAssets() {}

void DrawStartup() {
    const ImVec2 size = ImGui::GetIO().DisplaySize;
    DrawRootBackground(size);
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGui::Begin("##startup", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground);

    ImDrawList* d = ImGui::GetWindowDrawList();
    const float mark = 42.f;
    const float cy = size.y * .34f;
    DrawBrandMark(d, ImVec2((size.x - mark) * .5f, cy), mark, IM_COL32(247, 248, 248, 255), IM_COL32(186, 40, 52, 255));

    const char* brand = "R O B  R U N";
    const ImVec2 bsz = brandFont ? brandFont->CalcTextSizeA(27.0f, 10000.f, 0.f, brand) : ImGui::CalcTextSize(brand);
    const float by = cy + mark + 14.f;
    if (brandFont) d->AddText(brandFont, 27.0f, ImVec2((size.x - bsz.x) * .5f, by), IM_COL32(247, 248, 248, 255), brand);
    else d->AddText(ImVec2((size.x - bsz.x) * .5f, by), IM_COL32(247, 248, 248, 255), brand);

    const char* sub = "TÜRKÇE YAMA  •  by Tuğra A.";
    const ImVec2 ssz = ImGui::CalcTextSize(sub);
    d->AddText(ImVec2((size.x - ssz.x) * .5f, by + 34.f), IM_COL32(177, 151, 155, 255), sub);

    const float w = 220.f, x = (size.x - w) * .5f, barY = by + 72.f;
    d->AddRectFilled(ImVec2(x, barY), ImVec2(x + w, barY + 4.f), IM_COL32(45, 22, 26, 255), 2.f);
    const float prog = std::fmod(static_cast<float>(ImGui::GetTime()) * .85f, 1.f);
    d->AddRectFilled(ImVec2(x, barY), ImVec2(x + w * prog, barY + 4.f), IM_COL32(186, 40, 52, 255), 2.f);

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

void DrawDisclaimer() {
    const ImVec2 size = ImGui::GetIO().DisplaySize;
    DrawRootBackground(size);
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24, 15));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGui::Begin("##disclaimer-root", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground);

    DrawBrand();
    std::string verBadge = std::string("v") + kAppVersion + " • Tuğra A.";
    ImGui::SetCursorScreenPos(ImVec2(size.x - 24.f - 75.f - 152.f, 15.f));
    ActionButton(verBadge.c_str(), ImVec2(144, 30), false);
    DrawWindowButtons(size.x);

    ImDrawList* d = ImGui::GetWindowDrawList();
    d->AddLine(ImVec2(24, 57), ImVec2(size.x - 24, 57), IM_COL32(120, 64, 70, 75), 1.f);

    const float cardW = std::min(size.x - 96.f, 760.f);
    const ImVec2 card(cardW, 420.f);
    const ImVec2 pos((size.x - cardW) * 0.5f, (size.y - card.y) * 0.5f + 22.f);
    ImGui::SetCursorScreenPos(pos);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 16);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24, 20));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(.035f, .012f, .015f, .96f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(.72f, .20f, .24f, .75f));
    ImGui::BeginChild("##disclaimer-card", card, ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);

    const char* title = "KESİNLİKLE OKU";
    if (brandFont) ImGui::PushFont(brandFont);
    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - ImGui::CalcTextSize(title).x) * 0.5f);
    ImGui::TextColored(ImVec4(0.98f, 0.36f, 0.42f, 1.f), "%s", title);
    if (brandFont) ImGui::PopFont();

    ImGui::Dummy(ImVec2(0, 8));
    ImGui::PushTextWrapPos(ImGui::GetWindowWidth() - 24.f);
    ImGui::TextColored(ImVec4(0.97f, 0.93f, 0.93f, 1.f),
                       "Yama işlemi risklidir; ban yerseniz sorumluluk sizindir. Ama bizim uygulamamız tarafından bir veri çalınırsa ve bu tespit edilirse sorumluluk tamamen bize aittir.");
    ImGui::Dummy(ImVec2(0, 10));
    ImGui::TextColored(ImVec4(0.55f, 0.90f, 0.68f, 1.f), "Güvenlik ve Virüs Bilgilendirmesi:");
    ImGui::TextColored(ImVec4(0.80f, 0.72f, 0.73f, 1.f), "• Uygulama kesinlikle virüs, trojan veya zararlı kod içermez.");
    ImGui::TextColored(ImVec4(0.80f, 0.72f, 0.73f, 1.f), "• Steam oyun klasöründeki orijinal dosyalara müdahale etmez.");
    ImGui::TextColored(ImVec4(0.80f, 0.72f, 0.73f, 1.f), "• Hiçbir şifre, hesap bilgisi veya kişisel dosya okunmaz / gönderilmez.");
    ImGui::PopTextWrapPos();

    ImGui::Dummy(ImVec2(0, 14));
    static bool checked = false;
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.14f, 0.05f, 0.06f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(0.95f, 0.32f, 0.38f, 1.f));
    ImGui::Checkbox("Okudum, onaylıyorum.", &checked);
    ImGui::PopStyleColor(2);

    ImGui::Dummy(ImVec2(0, 10));
    const float availW = ImGui::GetContentRegionAvail().x;
    const float bw = (availW - 12.f) * 0.5f;
    if (ActionButton("DEVAM ET", ImVec2(bw, 42), checked)) {
        AcceptDisclaimer();
    }
    ImGui::SameLine(0, 12);
    if (ActionButton("KAPAT", ImVec2(bw, 42), true)) {
        PostMessageW(gWindow, WM_CLOSE, 0, 0);
    }

    ImGui::EndChild();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

void DrawMaintenance() {
    const ImVec2 size = ImGui::GetIO().DisplaySize;
    DrawRootBackground(size);
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24, 15));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGui::Begin("##maint-root", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground);

    DrawBrand();
    std::string verBadge = std::string("v") + kAppVersion + " • Tuğra A.";
    ImGui::SetCursorScreenPos(ImVec2(size.x - 24.f - 75.f - 152.f, 15.f));
    ActionButton(verBadge.c_str(), ImVec2(144, 30), false);
    DrawWindowButtons(size.x);

    ImDrawList* d = ImGui::GetWindowDrawList();
    d->AddLine(ImVec2(24, 57), ImVec2(size.x - 24, 57), IM_COL32(120, 64, 70, 75), 1.f);

    const float cardW = std::min(size.x - 96.f, 680.f);
    const ImVec2 card(cardW, 320.f);
    const ImVec2 pos((size.x - cardW) * 0.5f, (size.y - card.y) * 0.5f + 16.f);
    ImGui::SetCursorScreenPos(pos);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 16);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(26, 24));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(.035f, .012f, .015f, .96f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(.65f, .20f, .24f, .70f));
    ImGui::BeginChild("##maint-card", card, ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);

    const float availW = ImGui::GetContentRegionAvail().x;
    ImVec2 markPos = ImGui::GetCursorScreenPos();
    DrawBrandMark(ImGui::GetWindowDrawList(), ImVec2(markPos.x + (availW - 44.f) * 0.5f, markPos.y), 44.f,
                  IM_COL32(247, 248, 248, 255), IM_COL32(186, 40, 52, 255));
    ImGui::Dummy(ImVec2(0, 54));

    const char* title = "BAKIM MODU AKTİF";
    if (brandFont) ImGui::PushFont(brandFont);
    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - ImGui::CalcTextSize(title).x) * 0.5f);
    ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.40f, 1.f), "%s", title);
    if (brandFont) ImGui::PopFont();

    ImGui::Dummy(ImVec2(0, 10));
    std::string msg;
    {
        std::lock_guard<std::mutex> lock(g.mutex);
        msg = g.maintenanceMessage;
    }
    ImGui::PushTextWrapPos(ImGui::GetWindowWidth() - 26.f);
    ImGui::TextColored(ImVec4(0.92f, 0.88f, 0.88f, 1.f), "%s", msg.c_str());
    ImGui::PopTextWrapPos();

    ImGui::Dummy(ImVec2(0, 22));
    const float bw = (availW - 12.f) * 0.5f;
    if (ActionButton("TEKRAR KONTROL ET", ImVec2(bw, 42))) {
        StartRemoteCheck();
    }
    ImGui::SameLine(0, 12);
    if (ActionButton("KAPAT", ImVec2(bw, 42))) {
        PostMessageW(gWindow, WM_CLOSE, 0, 0);
    }

    ImGui::EndChild();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

void DrawMain() {
    const ImVec2 size = ImGui::GetIO().DisplaySize;
    DrawRootBackground(size);
    const float now = static_cast<float>(ImGui::GetTime());
    static float enterTime = -1.f;
    if (enterTime < 0.f) enterTime = now;
    const float phase = std::clamp((now - enterTime) / .50f, 0.f, 1.f);
    const float reveal = phase * phase * (3.f - 2.f * phase);

    ImGui::SetNextWindowPos(ImVec2(0, (1.f - reveal) * 12.f));
    ImGui::SetNextWindowSize(size);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, reveal);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24, 15));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGui::Begin("##main-root", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground);

    DrawBrand();
    std::string verBadge = std::string("v") + kAppVersion + " • Tuğra A.";
    ImGui::SetCursorScreenPos(ImVec2(size.x - 24.f - 75.f - 152.f, 15.f));
    ActionButton(verBadge.c_str(), ImVec2(144, 30), false);
    DrawWindowButtons(size.x);

    ImDrawList* d = ImGui::GetWindowDrawList();
    d->AddLine(ImVec2(24, 57), ImVec2(size.x - 24, 57), IM_COL32(120, 64, 70, 75), 1.f);

    const float cardX = 48.f, cardW = size.x - 96.f;
    const float extraY = std::max(0.f, size.y - 600.f);
    const float gap = 16.f + extraY * 0.10f;
    const float h1 = 114.f + extraY * 0.22f;
    const float h2 = 154.f + extraY * 0.28f;
    const float h3 = 110.f + extraY * 0.20f;

    auto panel = [&](float y, float h) {
        ImVec2 a(cardX, y), b(cardX + cardW, y + h);
        d->AddRectFilled(a, b, IM_COL32(24, 10, 13, 248), 12);
        d->AddRect(a, b, IM_COL32(84, 38, 45, 205), 12.f, 0, 1.f);
    };

    std::string statusCopy, stateCopy;
    {
        std::lock_guard<std::mutex> lock(g.mutex);
        statusCopy = g.statusText;
        stateCopy = g.patchState;
    }
    const bool isBusy = g.busy.load();
    const bool isErr = g.hasError.load();
    const float prog = std::clamp(g.progress.load(), 0.0f, 1.0f);

    float yOff = 82.f;

    // 1. Durum kartı
    panel(yOff, h1);
    d->AddText(ImVec2(cardX + 22, yOff + 16), IM_COL32(172, 165, 180, 255), "TÜRKÇE YAMA DURUMU");
    ImU32 stateCol = isErr ? IM_COL32(245, 95, 110, 255)
                           : (stateCopy == "KURULU VE GÜNCEL" ? IM_COL32(87, 224, 155, 255) : IM_COL32(239, 180, 75, 255));
    ImVec2 stSz = ImGui::CalcTextSize(stateCopy.c_str());
    d->AddText(ImVec2(cardX + cardW - 22 - stSz.x, yOff + 16), stateCol, stateCopy.c_str());
    d->AddText(ImVec2(cardX + 22, yOff + 46), IM_COL32(245, 242, 245, 255), statusCopy.c_str());

    ImVec2 barA(cardX + 22, yOff + h1 - 30.f), barB(cardX + cardW - 22, yOff + h1 - 18.f);
    d->AddRectFilled(barA, barB, IM_COL32(45, 22, 26, 255), 6);
    if (prog > 0.0f) {
        d->AddRectFilled(barA, ImVec2(barA.x + (barB.x - barA.x) * prog, barB.y), IM_COL32(186, 40, 52, 255), 6);
    }

    // 2. İşlem butonları
    yOff += h1 + gap;
    panel(yOff, h2);
    d->AddText(ImVec2(cardX + 22, yOff + 16), IM_COL32(172, 165, 180, 255), "İŞLEMLER");
    const float btnW = (cardW - 44.f - 14.f) * 0.5f;
    const float mainBtnH = std::min(62.f, 48.f + extraY * 0.08f);
    const float subBtnH = std::min(50.f, 38.f + extraY * 0.06f);
    ImGui::SetCursorScreenPos(ImVec2(cardX + 22, yOff + 44));
    const char* installLabel = isBusy ? "İŞLEM YAPILIYOR…"
                                      : (stateCopy == "KURULU VE GÜNCEL" ? "YAMAYI YENİDEN KUR" : "YAMAYI KUR");
    if (ActionButton(installLabel, ImVec2(btnW, mainBtnH), !isBusy)) {
        StartPatchAction(1);
    }
    ImGui::SameLine(0, 14);
    if (ActionButton("YAMAYI KALDIR", ImVec2(btnW, mainBtnH), !isBusy)) {
        StartPatchAction(2);
    }

    ImGui::SetCursorScreenPos(ImVec2(cardX + 22, yOff + 44 + mainBtnH + 12.f));
    if (isErr) {
        const float triW = (cardW - 44.f - 28.f) / 3.f;
        if (ActionButton("OYUNU BAŞLAT", ImVec2(triW, subBtnH), !isBusy)) LaunchSteamGame();
        ImGui::SameLine(0, 14);
        if (ActionButton("LOGU AÇ", ImVec2(triW, subBtnH), true)) OpenLogFile();
        ImGui::SameLine(0, 14);
        if (ActionButton("DISCORD SERVER", ImVec2(triW, subBtnH), true)) OpenDiscord();
    } else {
        if (ActionButton("OYUNU BAŞLAT (STEAM)", ImVec2(btnW, subBtnH), !isBusy)) LaunchSteamGame();
        ImGui::SameLine(0, 14);
        if (ActionButton("DISCORD SERVER", ImVec2(btnW, subBtnH), true)) OpenDiscord();
    }

    // 3. Bilgi kartı
    yOff += h2 + gap;
    panel(yOff, h3);
    ImFont* fnt = ImGui::GetFont();
    d->AddText(ImVec2(cardX + 22, yOff + 16), IM_COL32(247, 236, 238, 255), "Nasıl Çalışır?");
    const float infoWrap = cardW - 44.f;
    d->AddText(fnt, 16.5f, ImVec2(cardX + 22, yOff + 46), IM_COL32(177, 151, 155, 255),
               "• Steam oyun dosyalarına dokunmaz; PC açılışında otomatik kontrol eder.", nullptr, infoWrap);
    d->AddText(fnt, 16.5f, ImVec2(cardX + 22, yOff + 74), IM_COL32(177, 151, 155, 255),
               "• Oyunda dil ayarı English (İngilizce) seçili olmalıdır.", nullptr, infoWrap);

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}
