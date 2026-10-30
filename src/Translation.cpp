#include "Translation.h"

#include "Text.h"

#include <RE/B/BSResourceNiBinaryStream.h>
#include <RE/I/INISettingCollection.h>
#include <SKSE/Translation.h>

namespace
{
    NT::Translation::Strings g_strings;

    // Our own table for the files Import() parses. Never insert into the
    // engine's Scaleform table: InitTESThread may be rebuilding it, which
    // intermittently crashed startup (design-notes, "Settings and strings").
    // Written only at kDataLoaded, read-only afterwards. Keys are stored
    // lowercased: the engine table this replaces matched case-insensitively
    // (interned BSFixedStringW), and mod files rely on that.
    std::unordered_map<std::string, std::string> g_table;

    std::string GameLanguage()
    {
        auto*       collection = RE::INISettingCollection::GetSingleton();
        const auto* setting = collection ? collection->GetSetting("sLanguage:General") : nullptr;
        if (setting && setting->GetType() == RE::Setting::Type::kString && setting->data.s && *setting->data.s) {
            return setting->data.s;
        }
        return "ENGLISH";
    }

    // BSResourceNiBinaryStream so BSA-packed files resolve like loose ones.
    std::uint32_t ReadLineW(RE::BSResourceNiBinaryStream& a_stream, wchar_t* a_dst, std::uint32_t a_dstLen, bool& a_eof)
    {
        wchar_t* iter = a_dst;
        if (a_dstLen == 0) {
            a_eof = true;
            return 0;
        }
        for (std::uint32_t i = 0; i < a_dstLen - 1; ++i) {
            wchar_t       data = 0;
            std::uint64_t read = 0;
            a_stream.stream->DoRead(&data, sizeof(data), read);
            if (read != sizeof(data)) {
                a_eof = true;
                break;
            }
            if (data == L'\n') {
                break;
            }
            *iter++ = data;
        }
        *iter = 0;
        return static_cast<std::uint32_t>(iter - a_dst);
    }

    bool ParseFile(const std::string& a_path)
    {
        RE::BSResourceNiBinaryStream fileStream{ a_path };
        if (!fileStream.good()) {
            return false;
        }
        std::uint16_t bom = 0;
        std::uint64_t read = 0;
        fileStream.stream->DoRead(&bom, sizeof(bom), read);
        if (read != sizeof(bom) || bom != 0xFEFF) {
            // Empty or not UTF-16 LE (a common packaging mistake); treat it
            // as absent so Import() can fall back to the ENGLISH file.
            logs::warn("{}: missing UTF-16 LE BOM; file skipped", a_path);
            return false;
        }
        std::size_t entries = 0;
        bool        eof = false;
        while (!eof) {
            wchar_t             buf[512];
            const std::uint32_t rawLen = ReadLineW(fileStream, buf, static_cast<std::uint32_t>(std::size(buf)), eof);
            if (rawLen == 0) {
                continue;  // blank line; EOF ends the loop via eof
            }
            std::uint32_t len = rawLen;
            if (buf[len - 1] == L'\r') {
                buf[--len] = 0;
            }
            if (len < 4 || buf[0] != L'$') {
                continue;
            }
            std::uint32_t delim = 0;
            for (std::uint32_t i = 0; i < len; ++i) {
                if (buf[i] == L'\t') {
                    delim = i;  // keys never contain a tab; values may
                    break;
                }
            }
            if (delim < 2) {
                continue;
            }
            buf[delim] = 0;
            ::CharLowerBuffW(buf, delim);
            auto key = NT::Text::ToUtf8(buf);
            auto value = NT::Text::ToUtf8(&buf[delim + 1]);
            if (!key.empty() && !value.empty() && g_table.emplace(std::move(key), std::move(value)).second) {
                ++entries;
            }
        }
        logs::info("{}: {} translation(s) imported", a_path, entries);
        return true;
    }

    std::optional<std::string> Lookup(std::string_view a_key)
    {
        if (const auto it = g_table.find(NT::Text::Lower(a_key)); it != g_table.end()) {
            return it->second;
        }

        // Then the translator's virtual Translate(), exactly what Scaleform
        // calls for "$Key" strings in menus. Only through that call: the
        // installed translator is not always a BSScaleformTranslator
        // (ScaleformTranslationPP replaces it), so reading its
        // translationMap directly walked foreign memory and crashed on a
        // key nobody translates (1.0.0, SE 1.5.97, "$qlie_..." key).
        if (std::string result; SKSE::Translation::Translate(std::string(a_key), result) && !result.empty() &&
                                result != a_key) {
            return result;
        }
        return std::nullopt;
    }
}

namespace NT::Translation
{
    void Import(std::string_view a_name)
    {
        const auto language = GameLanguage();
        const auto path = std::format("Interface\\Translations\\{}_{}.txt", a_name, language);
        if (!ParseFile(path) && language != "ENGLISH") {
            ParseFile(std::format("Interface\\Translations\\{}_ENGLISH.txt", a_name));
        }
    }

    std::string Translate(std::string_view a_key, std::string_view a_fallback)
    {
        if (a_key.empty() || a_key.front() != '$') {
            return std::string(a_key);
        }
        if (auto translated = Lookup(a_key)) {
            return *translated;
        }
        return std::string(a_fallback.empty() ? a_key.substr(1) : a_fallback);
    }

    void LoadStrings()
    {
        Strings defaults;
        g_strings.pageDisplay = Translate("$NT_PageDisplay", defaults.pageDisplay);
        g_strings.pageCompat = Translate("$NT_PageCompat", defaults.pageCompat);
        g_strings.pageHistory = Translate("$NT_PageHistory", defaults.pageHistory);
        g_strings.historyHint = Translate("$NT_HistoryHint", defaults.historyHint);
        g_strings.historyFilter = Translate("$NT_HistoryFilter", defaults.historyFilter);
        g_strings.historyClear = Translate("$NT_HistoryClear", defaults.historyClear);
        g_strings.historyEmpty = Translate("$NT_HistoryEmpty", defaults.historyEmpty);
        g_strings.historyHide = Translate("$NT_HistoryHide", defaults.historyHide);
        g_strings.historyUnhide = Translate("$NT_HistoryUnhide", defaults.historyUnhide);
        g_strings.historyHideOff = Translate("$NT_HistoryHideOff", defaults.historyHideOff);
        g_strings.historyShowAll = Translate("$NT_HistoryShowAll", defaults.historyShowAll);
        g_strings.historyTime = Translate("$NT_HistoryTime", defaults.historyTime);
        g_strings.historyTimeDefault = Translate("$NT_HistoryTimeDefault", defaults.historyTimeDefault);
        g_strings.historyTimeOff = Translate("$NT_HistoryTimeOff", defaults.historyTimeOff);
        g_strings.headAdvanced = Translate("$NT_HeadAdvanced", defaults.headAdvanced);
        g_strings.enabled = Translate("$NT_Enabled", defaults.enabled);
        g_strings.enabledTip = Translate("$NT_EnabledTip", defaults.enabledTip);
        g_strings.headPosition = Translate("$NT_HeadPosition", defaults.headPosition);
        g_strings.dragButton = Translate("$NT_DragButton", defaults.dragButton);
        g_strings.dragTip = Translate("$NT_DragTip", defaults.dragTip);
        g_strings.posX = Translate("$NT_PosX", defaults.posX);
        g_strings.posY = Translate("$NT_PosY", defaults.posY);
        g_strings.align = Translate("$NT_Align", defaults.align);
        g_strings.alignLeft = Translate("$NT_AlignLeft", defaults.alignLeft);
        g_strings.alignCenter = Translate("$NT_AlignCenter", defaults.alignCenter);
        g_strings.alignRight = Translate("$NT_AlignRight", defaults.alignRight);
        g_strings.newestOnTop = Translate("$NT_NewestOnTop", defaults.newestOnTop);
        g_strings.showInMenus = Translate("$NT_ShowInMenus", defaults.showInMenus);
        g_strings.showInMenusTip = Translate("$NT_ShowInMenusTip", defaults.showInMenusTip);
        g_strings.maxWidth = Translate("$NT_MaxWidth", defaults.maxWidth);
        g_strings.maxVisible = Translate("$NT_MaxVisible", defaults.maxVisible);
        g_strings.headStyle = Translate("$NT_HeadStyle", defaults.headStyle);
        g_strings.font = Translate("$NT_Font", defaults.font);
        g_strings.fontDefault = Translate("$NT_FontDefault", defaults.fontDefault);
        g_strings.fontSize = Translate("$NT_FontSize", defaults.fontSize);
        g_strings.fontSizeTip = Translate("$NT_FontSizeTip", defaults.fontSizeTip);
        g_strings.lineSpacing = Translate("$NT_LineSpacing", defaults.lineSpacing);
        g_strings.textColor = Translate("$NT_TextColor", defaults.textColor);
        g_strings.effect = Translate("$NT_Effect", defaults.effect);
        g_strings.effectNone = Translate("$NT_EffectNone", defaults.effectNone);
        g_strings.effectShadow = Translate("$NT_EffectShadow", defaults.effectShadow);
        g_strings.effectOutline = Translate("$NT_EffectOutline", defaults.effectOutline);
        g_strings.background = Translate("$NT_Background", defaults.background);
        g_strings.headTiming = Translate("$NT_HeadTiming", defaults.headTiming);
        g_strings.immediate = Translate("$NT_Immediate", defaults.immediate);
        g_strings.immediateTip = Translate("$NT_ImmediateTip", defaults.immediateTip);
        g_strings.queueDelay = Translate("$NT_QueueDelay", defaults.queueDelay);
        g_strings.duration = Translate("$NT_Duration", defaults.duration);
        g_strings.fadeIn = Translate("$NT_FadeIn", defaults.fadeIn);
        g_strings.fadeOut = Translate("$NT_FadeOut", defaults.fadeOut);
        g_strings.stack = Translate("$NT_Stack", defaults.stack);
        g_strings.headTest = Translate("$NT_HeadTest", defaults.headTest);
        g_strings.headMerge = Translate("$NT_HeadMerge", defaults.headMerge);
        g_strings.headIconFonts = Translate("$NT_HeadIconFonts", defaults.headIconFonts);
        g_strings.test1 = Translate("$NT_Test1", defaults.test1);
        g_strings.testMany = Translate("$NT_TestMany", defaults.testMany);
        g_strings.resetDisplay = Translate("$NT_ResetDisplay", defaults.resetDisplay);
        g_strings.resetConfirm = Translate("$NT_ResetConfirm", defaults.resetConfirm);
        g_strings.footer = Translate("$NT_Footer", defaults.footer);
        g_strings.compatHint = Translate("$NT_CompatHint", defaults.compatHint);
        g_strings.mergeYps = Translate("$NT_MergeYps", defaults.mergeYps);
        g_strings.mergeYpsTip = Translate("$NT_MergeYpsTip", defaults.mergeYpsTip);
        g_strings.mergeBos = Translate("$NT_MergeBos", defaults.mergeBos);
        g_strings.mergeBosTip = Translate("$NT_MergeBosTip", defaults.mergeBosTip);
        g_strings.mergeGeneric = Translate("$NT_MergeGeneric", defaults.mergeGeneric);
        g_strings.mergeGenericTip = Translate("$NT_MergeGenericTip", defaults.mergeGenericTip);
        g_strings.mergeWindow = Translate("$NT_MergeWindow", defaults.mergeWindow);
        g_strings.drawIconFonts = Translate("$NT_DrawIconFonts", defaults.drawIconFonts);
        g_strings.drawIconFontsTip = Translate("$NT_DrawIconFontsTip", defaults.drawIconFontsTip);
        g_strings.iconFontsFound = Translate("$NT_IconFontsFound", defaults.iconFontsFound);
        g_strings.iconFontsNone = Translate("$NT_IconFontsNone", defaults.iconFontsNone);
        g_strings.iconFontGlyphs = Translate("$NT_IconFontGlyphs", defaults.iconFontGlyphs);
        g_strings.historyIconOnly = Translate("$NT_HistoryIconOnly", defaults.historyIconOnly);
        g_strings.sample1 = Translate("$NT_Sample1", defaults.sample1);
        g_strings.sample2 = Translate("$NT_Sample2", defaults.sample2);
        g_strings.sample3 = Translate("$NT_Sample3", defaults.sample3);
        g_strings.posWindowTitle = Translate("$NT_PosWindowTitle", defaults.posWindowTitle);
        g_strings.posWindowHint = Translate("$NT_PosWindowHint", defaults.posWindowHint);
        g_strings.done = Translate("$NT_Done", defaults.done);
        g_strings.testText = Translate("$NT_TestText", defaults.testText);
        g_strings.testManyText = Translate("$NT_TestManyText", defaults.testManyText);
        logs::info("UI strings loaded ('{}')", g_strings.pageDisplay);
    }

    const Strings& S()
    {
        return g_strings;
    }
}
