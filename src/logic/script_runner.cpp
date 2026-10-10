#include "logic/script_runner.h"

namespace sf {

std::optional<ScriptRunner::ScenarioChange> ScriptRunner::run(std::int32_t sentence) {
    change_.reset();
    run(sentence, 0);
    return change_;
}

std::int32_t ScriptRunner::value(const ScenarioScript::Operand& op) const {
    if (op.type == ScenarioScript::kConstant) return op.value;
    const auto found = flags_.find({op.type, op.value});
    return found == flags_.end() ? 0 : found->second;
}

void ScriptRunner::run(std::int32_t index, int depth) {
    if (index < 0 || static_cast<std::size_t>(index) >= script_.sentences.size() || depth > 16) return;
    for (const auto& c : script_.sentences[static_cast<std::size_t>(index)]) {
        if (change_) return;
        const auto& ops = c.operands;
        switch (c.opcode) {
            case ScenarioScript::kIf: {
                if (ops.size() < 4) break;
                const std::int32_t a = value(ops[0]), b = value(ops[2]);
                bool holds = a < b;
                switch (ops[1].value) {
                    case 0: holds = a == b; break;
                    case 1: holds = a != b; break;
                    case 2: holds = a > b; break;
                    default: break;
                }
                // The branch runs, then the sentence carries on.
                if (holds) run(ops[3].value, depth + 1);
                break;
            }
            case ScenarioScript::kSetFlag:
                if (ops.size() >= 2 && ops[0].type != ScenarioScript::kConstant) flags_[{ops[0].type, ops[0].value}] = value(ops[1]);
                break;
            case ScenarioScript::kChangeScenario:
                if (ops.size() >= 2) change_ = ScenarioChange{value(ops[0]), value(ops[1])};
                break;
            default: break;
        }
    }
}

}  // namespace sf
