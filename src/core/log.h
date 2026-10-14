#pragma once

#include <format>
#include <string>
#include <string_view>

namespace sf::log {

enum class Level { Debug, Info, Warn, Error };

void write(Level level, std::string_view message);

// The last lines written, oldest first, for the crash log.
std::string recent();

// Called with every line written (crash reports keep them as breadcrumbs).
using Listener = void (*)(Level level, std::string_view message);
void setListener(Listener listener);

template <typename... Args>
void info(std::format_string<Args...> fmt, Args&&... args) {
    write(Level::Info, std::format(fmt, std::forward<Args>(args)...));
}

template <typename... Args>
void warn(std::format_string<Args...> fmt, Args&&... args) {
    write(Level::Warn, std::format(fmt, std::forward<Args>(args)...));
}

template <typename... Args>
void error(std::format_string<Args...> fmt, Args&&... args) {
    write(Level::Error, std::format(fmt, std::forward<Args>(args)...));
}

}  // namespace sf::log
