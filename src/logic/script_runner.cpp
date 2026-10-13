#include "logic/script_runner.h"

#include <cctype>
#include <cstdio>

namespace sf {

namespace {

using S = ScenarioScript;

// Operand types that hold a value the script can change.
// 10 is WARP (the warp gates found) and 13 EXSTRG (a word list on the
// player, + 0x55c, its use not traced).
bool writable(std::int32_t type) {
    return type == S::kTemp || type == S::kNet || type == S::kSave || type == S::kQuest || type == 10 || type == 13;
}

}  // namespace

bool ScriptRunner::implemented(std::int32_t opcode) {
    switch (opcode) {
        case S::kIf: case S::kSetFlag: case S::kTalk: case S::kTalkPartnerStatus: case S::kIdentify:
        case S::kTrade: case S::kInitTrade: case S::kHealLife: case S::kHealMental: case S::kRepair:
        case S::kCreateItem: case S::kAdd: case S::kSub: case S::kMul: case S::kDiv: case S::kRem:
        case S::kPlaySound: case S::kChangeScenario: case S::kStop: case S::kMovable: case S::kSetAngleTo:
        case S::kSetActive: case S::kSetInactive: case S::kCreateItemTable: case S::kMessageBoxValue:
        case S::kMessageBox: case S::kGetRandom: case S::kStorage: case S::kGetPartnerKind:
        case S::kChangePartner: case S::kDrawQuestName: case S::kSetCompasses: case S::kGetEntryPoint:
        case S::kSetMessageDefine: case S::kGetRepairPrice: case S::kGetTotalGold: case S::kPayGold:
        case S::kCheckUnopinionItem: case S::kGetPlayerSex: case S::kGetPlayerLevel: case S::kSetQuestFlag:
        case S::kGetPartnerLife: case S::kEnableClassChange: case S::kGetClass:
        case S::kRoutine: case S::kCheckEnemyLive: case S::kCheckEnemyDead: case S::kCheckItemExist:
        case S::kDeleteItem: case S::kGetPlayerLife: case S::kGetPlayerMental: case S::kGetExperience:
        case S::kSetItemDirect: case S::kAbsoluteObjectFlag: case S::kSetUnlockSwitch:
        // Network play only: single play never reaches them.
        case S::kRoutineNet:
            return true;
        default: return false;
    }
}

std::optional<ScriptRunner::ScenarioChange> ScriptRunner::run(std::int32_t sentence) {
    change_.reset();
    run(sentence, 0);
    return change_;
}

std::int32_t ScriptRunner::value(const ScenarioScript::Operand& op) const {
    switch (op.type) {
        case S::kOperator:
        case S::kConstant:
        case S::kMessage: return op.value;
        case S::kCharacterX: return host_ ? host_->characterX(op.value) : 0;
        case S::kCharacterY: return host_ ? host_->characterY(op.value) : 0;
        case S::kPlayMode: return 0;  // single player
        case S::kObjectCheck: return host_ && host_->workingObject(op.value) ? 1 : 0;
        case S::kEnemyAlive: {
            // ELIV n: 1 while enemy 14,000,000 + n is alive, 0 once dead;
            // 0 too for one the scenario does not have (0x4346b0).
            const auto alive = host_ ? host_->enemyAlive(14000000 + op.value) : std::nullopt;
            return alive && *alive ? 1 : 0;
        }
        default: break;
    }
    const auto found = flags_.find({op.type, op.value});
    return found == flags_.end() ? 0 : found->second;
}

void ScriptRunner::store(const ScenarioScript::Operand& op, std::int32_t value) {
    if (writable(op.type)) flags_[{op.type, op.value}] = value;
}

std::string ScriptRunner::format(const std::string& text) const {
    std::string out;
    int arg = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '%' || i + 1 >= text.size()) {
            out += text[i];
            continue;
        }
        // %[width](d|s), as the original hands the text to sprintf.
        std::size_t j = i + 1;
        std::string spec = "%";
        while (j < text.size() && (std::isdigit(static_cast<unsigned char>(text[j])) || text[j] == '-')) spec += text[j++];
        if (j >= text.size()) break;
        const char kind = text[j];
        if (kind == '%') {
            out += '%';
        } else {
            const auto type = flags_.find({kMessageArgType, arg * 2});
            const auto val = flags_.find({kMessageArgType, arg * 2 + 1});
            const ScenarioScript::Operand op{type == flags_.end() ? S::kConstant : type->second, val == flags_.end() ? 0 : val->second};
            ++arg;
            char buf[64];
            if (kind == 's') {
                const auto msg = script_.messages.find(op.value);
                spec += 's';
                const std::string s = op.type == S::kMessage && msg != script_.messages.end() ? msg->second : std::string();
                std::snprintf(buf, sizeof buf, spec.c_str(), s.c_str());
            } else {
                spec += 'd';
                std::snprintf(buf, sizeof buf, spec.c_str(), value(op));
            }
            out += buf;
        }
        i = j;
    }
    return out;
}

void ScriptRunner::run(std::int32_t index, int depth) {
    if (index < 0 || static_cast<std::size_t>(index) >= script_.sentences.size() || depth > 32) return;
    for (const auto& c : script_.sentences[static_cast<std::size_t>(index)]) {
        if (change_) return;
        const auto& ops = c.operands;
        auto arg = [&](std::size_t i) { return i < ops.size() ? value(ops[i]) : -1; };
        switch (c.opcode) {
            case S::kIf: {
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
            case S::kSetFlag:
                if (ops.size() >= 2) store(ops[0], value(ops[1]));
                break;
            case S::kAdd:
            case S::kSub:
            case S::kMul:
            case S::kDiv:
            case S::kRem: {
                if (ops.size() < 2) break;
                const std::int32_t a = value(ops[0]), b = value(ops[1]);
                std::int32_t r = a;
                switch (c.opcode) {
                    case S::kAdd: r = a + b; break;
                    case S::kSub: r = a - b; break;
                    case S::kMul: r = a * b; break;
                    case S::kDiv: r = b ? a / b : a; break;
                    default: r = b ? a % b : a; break;
                }
                store(ops[0], r);
                break;
            }
            case S::kChangeScenario:
                if (ops.size() >= 2) change_ = ScenarioChange{value(ops[0]), value(ops[1])};
                break;
            case S::kSetActive:
            case S::kSetInactive:
                // The three NET flags of the character or object (0x430ffe
                // cases 0x16 and 0x17); the game shows and hides by them.
                if (!ops.empty()) {
                    for (const std::int32_t base : kStateFlagBases) {
                        flags_[{S::kNet, base + value(ops[0])}] = c.opcode == S::kSetActive ? 1 : 0;
                    }
                }
                break;
            case S::kRoutine:
                // ROUTIN n runs the event 6 triggers for n there and then
                // (0x4309f0), and the sentence carries on.
                if (!ops.empty()) {
                    const std::int32_t n = value(ops[0]);
                    for (const auto& t : script_.triggers) {
                        if (t.event == S::kOnRoutine && t.character == n) run(t.sentence, depth + 1);
                    }
                }
                break;
            case S::kSetMessageDefine:
                for (std::size_t i = 0; i < ops.size() && i < 20; ++i) {
                    flags_[{kMessageArgType, static_cast<std::int32_t>(i * 2)}] = ops[i].type;
                    flags_[{kMessageArgType, static_cast<std::int32_t>(i * 2 + 1)}] = ops[i].value;
                }
                break;
            case S::kGetRandom:
                if (ops.size() >= 3 && host_) {
                    const std::int32_t lo = value(ops[0]), hi = value(ops[1]);
                    store(ops[2], lo + (hi >= lo ? host_->random(hi - lo + 1) : 0));
                }
                break;
            case S::kSetQuestFlag: {
                // SETQUESTFLAG quest state ?: 1 takes the mission on, 2
                // clears it, but only one that was taken (0x433f29). The
                // third operand only matters in network play.
                if (ops.empty()) break;
                const std::int32_t quest = arg(0), state = arg(1);
                auto& flag = flags_[{S::kQuest, quest}];
                if (state != 2) {
                    flag = state;
                } else if (rules_.missionsClearOnlyWhenTaken) {
                    if (flag == 1) flag = 2;
                } else {
                    // The original clears a mission once at most: the first
                    // clear is spent even when the mission was not taken.
                    auto& cleared = flags_[{kQuestClearedType, quest}];
                    if (cleared == 0) {
                        cleared = 1;
                        if (flag == 1) flag = 2;
                    }
                }
                break;
            }
            default: break;
        }
        if (!host_) continue;
        if (!implemented(c.opcode)) host_->skippedCommand(c.opcode);
        switch (c.opcode) {
            case S::kTalk: {
                if (ops.size() < 2) break;
                // The message is a MSG id, or a variable holding one.
                const auto msg = script_.messages.find(value(ops[0]));
                if (msg == script_.messages.end()) break;
                ScriptHost::Talk t;
                t.text = format(msg->second);
                t.answer = ops[1];
                t.defaultChoice = arg(3);
                t.speaker = arg(4) == -1 ? character_ : arg(4);
                host_->talk(t);
                break;
            }
            case S::kInitTrade: host_->initTrade(arg(0), arg(1)); break;
            case S::kTrade: host_->trade(arg(0)); break;
            case S::kHealLife: host_->healLife(); break;
            case S::kHealMental: host_->healMental(); break;
            case S::kCreateItem: {
                if (ops.size() < 6) break;
                const int lo = arg(4), hi = arg(5);
                const int amount = hi > lo ? lo + host_->random(hi - lo + 1) : lo;
                host_->createItem(arg(0), arg(1), arg(2), arg(3), amount);
                break;
            }
            case S::kCreateItemTable:
                if (ops.size() >= 3) host_->createItemTable(arg(0), arg(1), arg(2));
                break;
            case S::kStop: host_->stopCharacter(arg(0), true); break;
            case S::kMovable: host_->stopCharacter(arg(0), false); break;
            case S::kSetAngleTo: host_->faceCharacter(arg(0), arg(1)); break;
            case S::kSetActive: host_->setActive(arg(0), true); break;
            case S::kAbsoluteObjectFlag: host_->setObjectStates(arg(0), arg(1), arg(2), arg(3)); break;
            case S::kSetUnlockSwitch: host_->setUnlockSwitch(arg(0) != 0); break;
            case S::kSetInactive: host_->setActive(arg(0), false); break;
            case S::kCheckEnemyLive:
            case S::kCheckEnemyDead: {
                // first last var: the first enemy in first..last that is
                // alive (or dead), else -1.
                if (ops.size() < 3) break;
                const bool wantAlive = c.opcode == S::kCheckEnemyLive;
                std::int32_t found = -1;
                for (std::int32_t id = arg(0), last = arg(1); id <= last && found < 0; ++id) {
                    if (const auto alive = host_->enemyAlive(id); alive && *alive == wantAlive) found = id;
                }
                store(ops[2], found);
                break;
            }
            case S::kCheckItemExist:
                if (ops.size() >= 3) store(ops[2], host_->hasItem(arg(0), arg(1)) ? 1 : 0);
                break;
            case S::kDeleteItem:
                if (ops.size() >= 2) host_->deleteItem(arg(0), arg(1));
                break;
            case S::kGetPlayerLife:
            case S::kGetPlayerMental: {
                if (ops.size() < 2) break;
                int now = 0, most = 0;
                if (c.opcode == S::kGetPlayerLife) {
                    host_->playerLife(now, most);
                } else {
                    host_->playerMental(now, most);
                }
                store(ops[0], now);
                store(ops[1], most);
                break;
            }
            case S::kGetExperience: host_->giveExperiencePercent(arg(0)); break;
            case S::kSetItemDirect:
                if (ops.size() >= 2) host_->giveItem(arg(0), arg(1));
                break;
            case S::kDrawQuestName: host_->showQuestName(arg(0)); break;
            case S::kSetCompasses:
                if (ops.empty()) break;
                if (const auto msg = script_.messages.find(value(ops[0])); msg != script_.messages.end()) {
                    host_->showAreaName(msg->second);
                }
                break;
            case S::kGetTotalGold:
                if (!ops.empty()) store(ops[0], static_cast<std::int32_t>(host_->gold()));
                break;
            case S::kPayGold: host_->payGold(arg(0)); break;
            case S::kGetPlayerLevel:
                if (!ops.empty()) store(ops[0], host_->playerLevel());
                break;
            case S::kGetEntryPoint:
                if (!ops.empty()) store(ops[0], host_->entryPoint());
                break;
            case S::kEnableClassChange:
                if (arg(0) >= 1 && arg(0) <= 3) host_->offerClass(arg(0));
                break;
            case S::kGetClass:
                if (!ops.empty()) store(ops[0], host_->offeredClass());
                break;
            case S::kGetPlayerSex:
                if (!ops.empty()) store(ops[0], host_->playerSex());
                break;
            case S::kStorage: host_->openStorage(); break;
            case S::kMessageBox:
            case S::kMessageBoxValue: {
                // character dx dy text r g b [duration]; the text is a MSG id
                // or a variable holding one, or for MESSAGEBOXVALUE a number.
                if (ops.size() < 7) break;
                ScriptHost::Label l;
                l.character = arg(0);
                l.dx = arg(1);
                l.dy = arg(2);
                const std::int32_t v = value(ops[3]);
                const auto msg = script_.messages.find(v);
                l.text = c.opcode == S::kMessageBox && msg != script_.messages.end() ? format(msg->second) : std::to_string(v);
                l.r = static_cast<std::uint8_t>(arg(4));
                l.g = static_cast<std::uint8_t>(arg(5));
                l.b = static_cast<std::uint8_t>(arg(6));
                if (ops.size() >= 8) l.duration = arg(7);
                host_->showLabel(l);
                break;
            }
            case S::kIdentify: host_->identifyItems(); break;
            case S::kCheckUnopinionItem:
                if (!ops.empty()) store(ops[0], host_->hasUnidentifiedItems() ? 1 : 0);
                break;
            case S::kRepair: host_->repair(arg(0)); break;
            case S::kGetRepairPrice:
                if (ops.size() >= 2) store(ops[1], host_->repairPrice(arg(0)));
                break;
            case S::kGetPartnerKind:
                if (!ops.empty()) store(ops[0], host_->partnerKind());
                break;
            case S::kChangePartner: host_->changePartner(arg(0)); break;
            case S::kGetPartnerLife: {
                int hp = 0, max = 0;
                if (ops.size() >= 2 && host_->partnerLife(hp, max)) {
                    store(ops[0], hp);
                    store(ops[1], max);
                }
                break;
            }
            case S::kTalkPartnerStatus: {
                // kind answer: a talk with no options over the speaker.
                if (ops.size() < 2) break;
                ScriptHost::Talk t;
                t.text = host_->partnerStatus(arg(0));
                if (t.text.empty()) break;
                t.answer = ops[1];
                t.speaker = character_;
                host_->talk(t);
                break;
            }
            default: break;
        }
    }
}

}  // namespace sf
