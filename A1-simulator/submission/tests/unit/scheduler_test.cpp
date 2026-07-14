#include "test_support.h"
#include "ir/model_ir.h"
#include "runtime/scheduler.h"

#include <string>
#include <vector>

namespace {

using a1::ir::BinaryOp;
using a1::ir::EdgeSense;
using a1::ir::ExprId;
using a1::ir::ModelIR;
using a1::ir::PackedType;
using a1::ir::ProcessKind;
using a1::ir::SignalId;
using a1::ir::SignalKind;
using a1::ir::SourceSpan;
using a1::ir::StmtId;
using a1::ir::TimingSense;
using a1::ir::UnaryOp;
using a1::runtime::LogicValue;
using a1::runtime::run_model;
using a1::runtime::SimOptions;
using a1::runtime::SimResult;

constexpr PackedType kBit1{1, false, false};
constexpr PackedType kBit2{2, false, false};
constexpr PackedType kBit4{4, false, false};
constexpr PackedType kLogic1{1, false, true};
constexpr PackedType kLogic2{2, false, true};
constexpr PackedType kLogic4{4, false, true};
const SourceSpan kSource{"scheduler_test.sv", 1, 1};

StmtId assign_const(ModelIR& model, SignalId target, PackedType type,
                    const std::string& binary) {
    return model.add_blocking_assign(
        model.add_whole_signal_lvalue(target, type, kSource),
        model.add_constant(LogicValue::from_binary(binary), type, kSource), kSource);
}

StmtId assign_signal(ModelIR& model, SignalId target, PackedType target_type,
                     SignalId source, PackedType source_type) {
    return model.add_blocking_assign(
        model.add_whole_signal_lvalue(target, target_type, kSource),
        model.add_signal_ref(source, source_type, kSource), kSource);
}

std::string signal_binary(const SimResult& result, SignalId signal) {
    return result.final_store ? result.final_store->value(signal).to_binary()
                              : std::string("<no final store>");
}

}  // namespace

int main() {
    // Case 1：NBA 同槽 + $finish —— 提交已入队 NBA，不再执行后续 Active 进程。
    {
        ModelIR model;
        const auto a = model.add_signal("top.a", kLogic1, SignalKind::Variable, kSource);
        const auto b = model.add_signal("top.b", kLogic1, SignalKind::Variable, kSource);

        const auto blocking = assign_const(model, a, kLogic1, "1");
        const auto nonblocking = model.add_nonblocking_assign(
            model.add_whole_signal_lvalue(a, kLogic1, kSource),
            model.add_constant(LogicValue::from_binary("0"), kLogic1, kSource), kSource);
        const auto finish = model.add_finish(kSource);
        static_cast<void>(model.add_process(
            ProcessKind::Initial, {}, {},
            model.add_seq_block({blocking, nonblocking, finish}, kSource), kSource));
        // 更晚的 Initial 进程：$finish 后不得执行。
        static_cast<void>(model.add_process(ProcessKind::Initial, {}, {},
                                            assign_const(model, b, kLogic1, "1"),
                                            kSource));

        const auto result = run_model(model);
        A1_EXPECT(!result.error);
        A1_EXPECT(result.finished);
        A1_EXPECT(result.time == 0);
        A1_EXPECT(signal_binary(result, a) == "0");
        A1_EXPECT(signal_binary(result, b) == "x");
    }

    // Case 2：posedge DFF —— q <= d 在 clk 上升沿采样，结束时间为 2。
    {
        ModelIR model;
        const auto d = model.add_signal("top.d", kLogic1, SignalKind::Variable, kSource);
        const auto clk = model.add_signal("top.clk", kLogic1, SignalKind::Variable, kSource);
        const auto q = model.add_signal("top.q", kLogic1, SignalKind::Variable, kSource);

        const auto dff_body = model.add_nonblocking_assign(
            model.add_whole_signal_lvalue(q, kLogic1, kSource),
            model.add_signal_ref(d, kLogic1, kSource), kSource);
        static_cast<void>(model.add_process(ProcessKind::Always,
                                            {TimingSense{EdgeSense::Posedge, clk}}, {d},
                                            dff_body, kSource));

        const auto body = model.add_seq_block(
            {assign_const(model, d, kLogic1, "1"), assign_const(model, clk, kLogic1, "0"),
             model.add_delay(1, assign_const(model, clk, kLogic1, "1"), kSource),
             model.add_delay(1, model.add_finish(kSource), kSource)},
            kSource);
        static_cast<void>(model.add_process(ProcessKind::Initial, {}, {}, body, kSource));

        const auto result = run_model(model);
        A1_EXPECT(!result.error);
        A1_EXPECT(result.finished);
        A1_EXPECT(result.time == 2);
        A1_EXPECT(signal_binary(result, q) == "1");
    }

    // Case 3：#delay 推进时间。
    {
        ModelIR model;
        static_cast<void>(model.add_process(
            ProcessKind::Initial, {}, {},
            model.add_delay(5, model.add_finish(kSource), kSource), kSource));

        const auto result = run_model(model);
        A1_EXPECT(!result.error);
        A1_EXPECT(result.finished);
        A1_EXPECT(result.time == 5);
    }

    // Case 4：振荡组合环 assign a = ~a 触发 delta cycle limit。
    {
        ModelIR model;
        const auto a = model.add_signal("top.a", kBit1, SignalKind::Net, kSource);
        static_cast<void>(model.add_continuous_assign(
            model.add_whole_signal_lvalue(a, kBit1, kSource),
            model.add_unary(UnaryOp::BitwiseNot, model.add_signal_ref(a, kBit1, kSource),
                            kBit1, kSource),
            kSource));
        static_cast<void>(model.add_process(
            ProcessKind::Initial, {}, {},
            model.add_delay(1, model.add_finish(kSource), kSource), kSource));

        SimOptions options;
        options.delta_limit = 4;
        const auto result = run_model(model, options);
        A1_EXPECT(result.error.has_value());
        A1_EXPECT(result.error->find("delta cycle limit exceeded") != std::string::npos);
    }

    // Case 5：多位信号边沿只看 LSB —— MSB 变化不触发，LSB 0→1 触发。
    {
        ModelIR model;
        const auto bus = model.add_signal("top.bus", kLogic2, SignalKind::Variable, kSource);
        const auto cnt = model.add_signal("top.cnt", kBit4, SignalKind::Variable, kSource);
        const auto after_msb =
            model.add_signal("top.after_msb", kBit4, SignalKind::Variable, kSource);
        const auto after_lsb =
            model.add_signal("top.after_lsb", kBit4, SignalKind::Variable, kSource);

        const auto increment = model.add_blocking_assign(
            model.add_whole_signal_lvalue(cnt, kBit4, kSource),
            model.add_binary(BinaryOp::Add, model.add_signal_ref(cnt, kBit4, kSource),
                             model.add_constant(LogicValue::from_binary("0001"), kBit4,
                                                kSource),
                             kBit4, kBit4, kSource),
            kSource);
        static_cast<void>(model.add_process(ProcessKind::Always,
                                            {TimingSense{EdgeSense::Posedge, bus}}, {cnt},
                                            increment, kSource));

        const auto body = model.add_seq_block(
            {assign_const(model, bus, kLogic2, "00"),
             model.add_delay(1, assign_const(model, bus, kLogic2, "10"), kSource),
             model.add_delay(1, assign_signal(model, after_msb, kBit4, cnt, kBit4),
                             kSource),
             assign_const(model, bus, kLogic2, "00"),
             model.add_delay(1, assign_const(model, bus, kLogic2, "01"), kSource),
             model.add_delay(1, assign_signal(model, after_lsb, kBit4, cnt, kBit4),
                             kSource),
             model.add_finish(kSource)},
            kSource);
        static_cast<void>(model.add_process(ProcessKind::Initial, {}, {}, body, kSource));

        const auto result = run_model(model);
        A1_EXPECT(!result.error);
        A1_EXPECT(result.finished);
        A1_EXPECT(signal_binary(result, after_msb) == "0000");  // MSB-only 变化不触发
        A1_EXPECT(signal_binary(result, after_lsb) == "0001");  // LSB 0→1 触发一次
    }

    // Case 6：if 的 truth_value —— One→then；Zero/Unknown→else。
    {
        ModelIR model;
        const auto r1 = model.add_signal("top.r1", kBit2, SignalKind::Variable, kSource);
        const auto r2 = model.add_signal("top.r2", kBit2, SignalKind::Variable, kSource);
        const auto r3 = model.add_signal("top.r3", kBit2, SignalKind::Variable, kSource);

        const auto make_if = [&](SignalId target, PackedType cond_type,
                                 const std::string& cond_binary) {
            return model.add_if(
                model.add_constant(LogicValue::from_binary(cond_binary), cond_type,
                                   kSource),
                assign_const(model, target, kBit2, "01"),
                assign_const(model, target, kBit2, "10"), kSource);
        };
        const auto body = model.add_seq_block(
            {make_if(r1, kLogic1, "x"),      // 1'bx → Unknown → else
             make_if(r2, kLogic4, "1x00"),   // 存在确定 1 → then
             make_if(r3, kLogic2, "0x"),     // 无确定 1 且含 x → else
             model.add_finish(kSource)},
            kSource);
        static_cast<void>(model.add_process(ProcessKind::Initial, {}, {}, body, kSource));

        const auto result = run_model(model);
        A1_EXPECT(!result.error);
        A1_EXPECT(result.finished);
        A1_EXPECT(signal_binary(result, r1) == "10");
        A1_EXPECT(signal_binary(result, r2) == "01");
        A1_EXPECT(signal_binary(result, r3) == "10");
    }

    // Case 7：#0 与连续赋值 —— 同进程内读 wire 得旧值，#0 恢复后得新值。
    {
        ModelIR model;
        const auto a = model.add_signal("top.a", kLogic1, SignalKind::Variable, kSource);
        const auto y = model.add_signal("top.y", kLogic1, SignalKind::Net, kSource);
        const auto t1 = model.add_signal("top.t1", kLogic1, SignalKind::Variable, kSource);
        const auto t2 = model.add_signal("top.t2", kLogic1, SignalKind::Variable, kSource);
        static_cast<void>(model.add_continuous_assign(
            model.add_whole_signal_lvalue(y, kLogic1, kSource),
            model.add_unary(UnaryOp::BitwiseNot,
                            model.add_signal_ref(a, kLogic1, kSource), kLogic1, kSource),
            kSource));

        const auto body = model.add_seq_block(
            {assign_const(model, a, kLogic1, "0"),
             model.add_delay(
                 1,
                 model.add_seq_block(
                     {assign_const(model, a, kLogic1, "1"),
                      assign_signal(model, t1, kLogic1, y, kLogic1),  // 未 settle → 旧值
                      model.add_delay(
                          0,
                          model.add_seq_block(
                              {assign_signal(model, t2, kLogic1, y, kLogic1),
                               model.add_finish(kSource)},
                              kSource),
                          kSource)},
                     kSource),
                 kSource)},
            kSource);
        static_cast<void>(model.add_process(ProcessKind::Initial, {}, {}, body, kSource));

        const auto result = run_model(model);
        A1_EXPECT(!result.error);
        A1_EXPECT(result.finished);
        A1_EXPECT(signal_binary(result, t1) == "1");  // 旧值 ~0
        A1_EXPECT(signal_binary(result, t2) == "0");  // #0 恢复后的新值 ~1
    }

    // Case 8：#0 期间 NBA 不提交 —— #0 后读到旧值，$finish 收尾时提交。
    {
        ModelIR model;
        const auto a = model.add_signal("top.a", kLogic1, SignalKind::Variable, kSource);
        const auto t = model.add_signal("top.t", kLogic1, SignalKind::Variable, kSource);

        const auto nonblocking = model.add_nonblocking_assign(
            model.add_whole_signal_lvalue(a, kLogic1, kSource),
            model.add_constant(LogicValue::from_binary("1"), kLogic1, kSource), kSource);
        const auto body = model.add_seq_block(
            {assign_const(model, a, kLogic1, "0"), nonblocking,
             model.add_delay(0,
                             model.add_seq_block(
                                 {assign_signal(model, t, kLogic1, a, kLogic1),
                                  model.add_finish(kSource)},
                                 kSource),
                             kSource)},
            kSource);
        static_cast<void>(model.add_process(ProcessKind::Initial, {}, {}, body, kSource));

        const auto result = run_model(model);
        A1_EXPECT(!result.error);
        A1_EXPECT(result.finished);
        A1_EXPECT(signal_binary(result, t) == "0");  // NBA 尚未提交
        A1_EXPECT(signal_binary(result, a) == "1");  // $finish 收尾时提交
    }

    // Case 9：初始 settle 的初始化跳变（z→1）不构成 posedge —— iverilog 交叉验证 q==0。
    {
        ModelIR model;
        const auto w = model.add_signal("top.w", kLogic1, SignalKind::Net, kSource);
        const auto q = model.add_signal("top.q", kLogic1, SignalKind::Variable, kSource);
        static_cast<void>(model.add_continuous_assign(
            model.add_whole_signal_lvalue(w, kLogic1, kSource),
            model.add_constant(LogicValue::from_binary("1"), kLogic1, kSource), kSource));

        static_cast<void>(model.add_process(ProcessKind::Always,
                                            {TimingSense{EdgeSense::Posedge, w}}, {},
                                            assign_const(model, q, kLogic1, "1"), kSource));
        static_cast<void>(model.add_process(
            ProcessKind::Initial, {}, {},
            model.add_seq_block(
                {assign_const(model, q, kLogic1, "0"),
                 model.add_delay(1, model.add_finish(kSource), kSource)},
                kSource),
            kSource));

        const auto result = run_model(model);
        A1_EXPECT(!result.error);
        A1_EXPECT(result.finished);
        A1_EXPECT(signal_binary(result, q) == "0");  // 无伪初始化 posedge
    }

    // Case 10：过程赋值目标为 Net → run_model 失败关闭（返回错误而非抛异常）。
    {
        ModelIR model;
        const auto n = model.add_signal("top.n", kLogic1, SignalKind::Net, kSource);
        static_cast<void>(model.add_process(
            ProcessKind::Initial, {}, {},
            model.add_blocking_assign(
                model.add_whole_signal_lvalue(n, kLogic1, kSource),
                model.add_constant(LogicValue::from_binary("1"), kLogic1, kSource),
                kSource),
            kSource));

        const auto result = run_model(model);
        A1_EXPECT(result.error.has_value());
        A1_EXPECT(result.error->find("invalid model") != std::string::npos);
        A1_EXPECT(result.error->find("process assignment target must be a variable") !=
                  std::string::npos);
    }

    // 空模型：无进程即成功返回。
    {
        const auto result = run_model(ModelIR{});
        A1_EXPECT(!result.error);
        A1_EXPECT(!result.finished);
        A1_EXPECT(result.time == 0);
    }

    return EXIT_SUCCESS;
}
