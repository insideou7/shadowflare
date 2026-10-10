#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <utility>

#include "formats/scenario_script.h"

namespace sf {

// Story flags shared by scripts: (operand type, id) -> value.
using ScriptFlags = std::map<std::pair<std::int32_t, std::int32_t>, std::int32_t>;

// A small part of the scenario script interpreter: IF, SETFLAG and
// CHANGESCENARIO, enough to follow exits guarded by story flags. Other
// commands are skipped. No SDL, so it can be unit tested.
class ScriptRunner {
public:
    struct ScenarioChange {
        int scenario = 0;
        int entry = 0;
    };

    ScriptRunner(const ScenarioScript& script, ScriptFlags& flags) : script_(script), flags_(flags) {}

    // Runs a sentence; returns the scenario change it asked for, if any
    // (the rest of the sentence is skipped once one is requested).
    std::optional<ScenarioChange> run(std::int32_t sentence);

    std::int32_t value(const ScenarioScript::Operand& op) const;

private:
    void run(std::int32_t sentence, int depth);

    const ScenarioScript& script_;
    ScriptFlags& flags_;
    std::optional<ScenarioChange> change_;
};

}  // namespace sf
