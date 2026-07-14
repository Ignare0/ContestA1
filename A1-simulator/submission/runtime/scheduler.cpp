#include "runtime/scheduler.h"

#include "runtime/edge.h"

#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace a1::runtime {
namespace {

enum class ProcessRunState : std::uint8_t {
    WaitingAtSensitivity,
    Running,
    TimedSuspended,
    Done,
};

struct ProcessState {
    ir::ProcessId id{0};
    // 待执行语句栈（back = 下一条）；Delay 挂起时保存续体。
    std::vector<ir::StmtId> stack;
    ProcessRunState state = ProcessRunState::WaitingAtSensitivity;
    bool live = true;
};

struct PendingNba {
    ir::LValueId target;
    LogicValue value;
};

StateDomain domain_of(const ir::PackedType& type) {
    return type.is_four_state ? StateDomain::FourState : StateDomain::TwoState;
}

class Scheduler {
public:
    Scheduler(const ir::ModelIR& model, const SimOptions& options)
        : model_(model), options_(options), store_(model) {}

    SimResult run();

private:
    void run_process(std::uint32_t index);
    void write_variable(ir::LValueId target, const LogicValue& value);
    void wake_waiters();
    void snapshot_prev();
    void enqueue_active(std::uint32_t index);
    void commit_nba();
    [[nodiscard]] SimResult fail(std::string message) const;
    [[nodiscard]] SimResult success() const;

    const ir::ModelIR& model_;
    const SimOptions& options_;
    SignalStore store_;

    std::vector<ProcessState> processes_;
    std::vector<LogicValue> prev_;
    std::deque<std::uint32_t> active_;
    std::vector<std::uint32_t> pending_zero_;
    std::vector<PendingNba> nba_;
    std::map<std::uint64_t, std::vector<std::uint32_t>> timed_;
    std::uint64_t time_ = 0;
    std::uint64_t delta_ = 0;
    bool dirty_ = false;
    bool finished_ = false;
};

SimResult Scheduler::run() {
    snapshot_prev();

    processes_.resize(model_.processes().size());
    for (std::uint32_t index = 0; index < processes_.size(); ++index) {
        processes_[index].id = ir::ProcessId{index};
        const auto& process = model_.processes()[index];
        if (process.kind == ir::ProcessKind::Initial) {
            processes_[index].stack.push_back(process.body);
            enqueue_active(index);
        }
    }

    if (auto error = ContinuousEvaluator::settle_with_limit(model_, store_,
                                                            options_.delta_limit)) {
        return fail(std::move(*error));
    }
    // 初始 settle 的初始化跳变（如 z→1）不构成边沿，重采快照且不唤醒。
    snapshot_prev();

    while (true) {
        // --- Active + settle + wake + #0 pending 循环 ---
        while (true) {
            while (!active_.empty() && !finished_) {
                const auto index = active_.front();
                active_.pop_front();
                run_process(index);
            }
            if (finished_) break;
            if (dirty_) {
                dirty_ = false;
                if (auto error = ContinuousEvaluator::settle_with_limit(
                        model_, store_, options_.delta_limit)) {
                    return fail(std::move(*error));
                }
                wake_waiters();
                snapshot_prev();
                ++delta_;
                if (delta_ > options_.delta_limit) {
                    return fail("delta cycle limit exceeded");
                }
                continue;
            }
            if (!pending_zero_.empty()) {
                for (const auto index : pending_zero_) {
                    enqueue_active(index);
                }
                pending_zero_.clear();
                continue;
            }
            break;
        }

        if (finished_) {
            // $finish 收尾：提交已入队 NBA，不再执行任何续体，不再 settle。
            commit_nba();
            return success();
        }

        if (!nba_.empty()) {
            commit_nba();
            continue;  // dirty 已置位，回到 Active 循环做 settle/唤醒。
        }

        // --- 推进 simulation time ---
        if (timed_.empty()) {
            for (std::uint32_t index = 0; index < processes_.size(); ++index) {
                if (processes_[index].live &&
                    model_.processes()[index].kind == ir::ProcessKind::Always) {
                    return fail("no pending events");
                }
            }
            return success();
        }
        const auto next = timed_.begin();
        if (next->first > options_.time_limit) {
            return fail("time limit exceeded");
        }
        time_ = next->first;
        for (const auto index : next->second) {
            enqueue_active(index);
        }
        timed_.erase(next);
        delta_ = 0;
    }
}

void Scheduler::run_process(std::uint32_t index) {
    auto& process = processes_[index];
    process.state = ProcessRunState::Running;
    while (!process.stack.empty()) {
        const auto stmt_id = process.stack.back();
        process.stack.pop_back();
        const auto& statement = model_.statements().at(stmt_id.value);
        bool stopped = false;
        std::visit(
            [&](const auto& payload) {
                using Payload = std::decay_t<decltype(payload)>;
                if constexpr (std::is_same_v<Payload, ir::BlockingAssignStmt>) {
                    write_variable(payload.target,
                                   ContinuousEvaluator::evaluate(model_, store_,
                                                                 payload.value));
                } else if constexpr (std::is_same_v<Payload, ir::NonBlockingAssignStmt>) {
                    // RHS 立即求值，提交延后到 NBA 阶段。
                    nba_.push_back({payload.target,
                                    ContinuousEvaluator::evaluate(model_, store_,
                                                                  payload.value)});
                } else if constexpr (std::is_same_v<Payload, ir::SeqBlockStmt>) {
                    for (auto child = payload.statements.rbegin();
                         child != payload.statements.rend(); ++child) {
                        process.stack.push_back(*child);
                    }
                } else if constexpr (std::is_same_v<Payload, ir::DelayStmt>) {
                    process.stack.push_back(payload.next);
                    process.state = ProcessRunState::TimedSuspended;
                    if (payload.ticks == 0) {
                        pending_zero_.push_back(index);
                    } else {
                        timed_[time_ + payload.ticks].push_back(index);
                    }
                    stopped = true;
                } else if constexpr (std::is_same_v<Payload, ir::IfStmt>) {
                    // IEEE 1364-2005 §9.4：One→then；Zero/Unknown→else 侧。
                    const auto truth = ContinuousEvaluator::evaluate(model_, store_,
                                                                     payload.condition)
                                           .truth_value();
                    if (truth == TruthValue::One) {
                        process.stack.push_back(payload.then_stmt);
                    } else if (payload.else_stmt.has_value()) {
                        process.stack.push_back(*payload.else_stmt);
                    }
                } else if constexpr (std::is_same_v<Payload, ir::FinishStmt>) {
                    finished_ = true;
                    process.state = ProcessRunState::Done;
                    process.live = false;
                    stopped = true;
                }
                // DisplayStubStmt / EmptyStmt：忽略。
            },
            statement.payload);
        if (stopped) return;
    }

    if (model_.processes()[index].kind == ir::ProcessKind::Initial) {
        process.live = false;
        process.state = ProcessRunState::Done;
    } else {
        process.state = ProcessRunState::WaitingAtSensitivity;
    }
}

void Scheduler::write_variable(ir::LValueId target, const LogicValue& value) {
    const auto& lvalue = model_.lvalues().at(target.value);
    const auto payload =
        value.coerce(lvalue.type.width, lvalue.type.is_signed, domain_of(lvalue.type));
    std::visit(
        [&](const auto& select) {
            using Select = std::decay_t<decltype(select)>;
            if constexpr (std::is_same_v<Select, ir::WholeSignalLValue>) {
                store_.set_variable(select.signal, payload);
            } else {
                store_.set_variable(
                    select.signal,
                    store_.value(select.signal).with_slice(select.bit_offset, payload));
            }
        },
        lvalue.payload);
    dirty_ = true;
}

void Scheduler::wake_waiters() {
    for (std::uint32_t index = 0; index < processes_.size(); ++index) {
        auto& process = processes_[index];
        if (!process.live || process.state != ProcessRunState::WaitingAtSensitivity) {
            continue;
        }
        const auto& description = model_.processes()[index];
        bool wake = false;
        if (description.sensitivity.empty()) {
            // @*：read_signals 任一 !exactly_equals 即唤醒；read_signals 为空则永不唤醒。
            for (const auto signal : description.read_signals) {
                if (!store_.value(signal).exactly_equals(prev_[signal.value])) {
                    wake = true;
                    break;
                }
            }
        } else {
            for (const auto& sense : description.sensitivity) {
                const auto& current = store_.value(sense.signal);
                const auto& previous = prev_[sense.signal.value];
                switch (sense.edge) {
                    case ir::EdgeSense::AnyChange:
                        wake = !current.exactly_equals(previous);
                        break;
                    case ir::EdgeSense::Posedge:
                        // 多位信号仅 LSB（IEEE §9.7.2）。
                        wake = detect_edge(previous.bit(0), current.bit(0)) ==
                               EdgeKind::Posedge;
                        break;
                    case ir::EdgeSense::Negedge:
                        wake = detect_edge(previous.bit(0), current.bit(0)) ==
                               EdgeKind::Negedge;
                        break;
                }
                if (wake) break;
            }
        }
        if (wake) {
            process.stack.clear();
            process.stack.push_back(description.body);
            enqueue_active(index);
        }
    }
}

void Scheduler::snapshot_prev() {
    prev_.clear();
    prev_.reserve(model_.signals().size());
    for (std::uint32_t signal = 0; signal < model_.signals().size(); ++signal) {
        prev_.push_back(store_.value(ir::SignalId{signal}));
    }
}

void Scheduler::enqueue_active(std::uint32_t index) {
    processes_[index].state = ProcessRunState::Running;
    active_.push_back(index);
}

void Scheduler::commit_nba() {
    for (const auto& entry : nba_) {
        write_variable(entry.target, entry.value);
    }
    nba_.clear();
}

SimResult Scheduler::fail(std::string message) const {
    return {std::move(message), time_, false, nullptr};
}

SimResult Scheduler::success() const {
    return {std::nullopt, time_, finished_, std::make_shared<SignalStore>(store_)};
}

}  // namespace

SimResult run_model(const ir::ModelIR& model, const SimOptions& options) {
    return Scheduler(model, options).run();
}

}  // namespace a1::runtime
