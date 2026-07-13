#include "frontend/ast_probe.h"

#include <concepts>
#include <type_traits>

#include "slang/ast/ASTVisitor.h"
#include "slang/ast/Compilation.h"
#include "slang/ast/Expression.h"
#include "slang/ast/Statement.h"
#include "slang/ast/Symbol.h"
#include "slang/ast/TimingControl.h"
#include "slang/ast/expressions/CallExpression.h"

namespace a1::frontend {
namespace {

class ProbeVisitor final
    : public slang::ast::ASTVisitor<ProbeVisitor, slang::ast::VisitFlags::AllGood> {
public:
    explicit ProbeVisitor(ProbeReport& report) : report(report) {}

    template<typename T>
        requires std::derived_from<T, slang::ast::Symbol>
    void handle(const T& node) {
        report.increment(report.symbols, slang::ast::toString(node.kind));
        this->visitDefault(node);
    }

    template<typename T>
        requires std::derived_from<T, slang::ast::Statement>
    void handle(const T& node) {
        report.increment(report.statements, slang::ast::toString(node.kind));
        this->visitDefault(node);
    }

    template<typename T>
        requires std::derived_from<T, slang::ast::Expression>
    void handle(const T& node) {
        report.increment(report.expressions, slang::ast::toString(node.kind));
        report.increment(report.data_types, slang::ast::toString(node.type->kind));
        if constexpr (std::same_as<T, slang::ast::CallExpression>) {
            if (node.isSystemCall())
                report.increment(report.system_calls, node.getSubroutineName());
        }
        this->visitDefault(node);
    }

    template<typename T>
        requires std::derived_from<T, slang::ast::TimingControl>
    void handle(const T& node) {
        report.increment(report.timing_controls, slang::ast::toString(node.kind));
        this->visitDefault(node);
    }

private:
    ProbeReport& report;
};

} // namespace

ProbeReport collect_probe(slang::ast::Compilation& compilation) {
    ProbeReport report;
    ProbeVisitor visitor(report);
    for (const auto* top : compilation.getRoot().topInstances) {
        report.top_instances.emplace_back(top->getHierarchicalPath());
        top->visit(visitor);
    }
    return report;
}

} // namespace a1::frontend
