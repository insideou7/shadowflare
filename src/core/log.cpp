#include "core/log.h"

#include <cstdio>

namespace sf::log {

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
}

}  // namespace sf::log
