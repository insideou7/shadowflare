#include "core/log.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <mutex>

namespace sf::log {

namespace {

std::mutex recentMutex;
std::array<std::string, 40> recentLines;
std::size_t nextLine = 0;

}  // namespace

void write(Level level, std::string_view message) {
    const char* tag = "INFO";
    switch (level) {
        case Level::Debug: tag = "DEBUG"; break;
        case Level::Info: tag = "INFO"; break;
        case Level::Warn: tag = "WARN"; break;
        case Level::Error: tag = "ERROR"; break;
    }
    std::FILE* out = level >= Level::Warn ? stderr : stdout;
    std::fprintf(out, "[%s] %.*s\n", tag, static_cast<int>(message.size()), message.data());
    std::lock_guard lock(recentMutex);
    recentLines[nextLine % recentLines.size()] = std::format("[{}] {}", tag, message);
    ++nextLine;
}

std::string recent() {
    // A crash may come while another thread holds the lock; skip the lines
    // rather than wait for ever.
    std::unique_lock lock(recentMutex, std::try_to_lock);
    if (!lock) return {};
    std::string out;
    const std::size_t count = std::min(nextLine, recentLines.size());
    for (std::size_t i = nextLine - count; i < nextLine; ++i) {
        out += recentLines[i % recentLines.size()];
        out += '\n';
    }
    return out;
}

}  // namespace sf::log
