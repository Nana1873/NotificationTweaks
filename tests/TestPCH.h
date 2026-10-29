#pragma once

// Precompiled header for the unit tests: the plugin's pure modules (Merge,
// Markup, Text.h) without CommonLibSSE. logs:: is a no-op stand-in.

#include <Windows.h>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace logs
{
    template <class... Args>
    void info(std::format_string<Args...>, Args&&...)
    {}

    template <class... Args>
    void debug(std::format_string<Args...>, Args&&...)
    {}

    template <class... Args>
    void warn(std::format_string<Args...>, Args&&...)
    {}
}

using namespace std::literals;
