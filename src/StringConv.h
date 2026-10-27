#pragma once

// UTF-8 <-> UTF-16 for the Win32 Unicode APIs (translation-map keys,
// CharLowerBuffW/CharUpperBuffW case mapping).
namespace NT::Text
{
    inline std::wstring ToWide(std::string_view a_utf8)
    {
        if (a_utf8.empty()) {
            return {};
        }
        const auto size = MultiByteToWideChar(CP_UTF8, 0, a_utf8.data(), static_cast<int>(a_utf8.size()), nullptr, 0);
        std::wstring result(static_cast<std::size_t>(size), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, a_utf8.data(), static_cast<int>(a_utf8.size()), result.data(), size);
        return result;
    }

    inline std::string ToUtf8(std::wstring_view a_wide)
    {
        if (a_wide.empty()) {
            return {};
        }
        const auto size =
            WideCharToMultiByte(CP_UTF8, 0, a_wide.data(), static_cast<int>(a_wide.size()), nullptr, 0, nullptr, nullptr);
        std::string result(static_cast<std::size_t>(size), '\0');
        WideCharToMultiByte(CP_UTF8, 0, a_wide.data(), static_cast<int>(a_wide.size()), result.data(), size, nullptr, nullptr);
        return result;
    }
}
