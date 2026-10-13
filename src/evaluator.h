
#ifndef RULEFORGE_EVALUATOR_H
#define RULEFORGE_EVALUATOR_H

#include "parser.h"

#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace ruleforge {

using Value = std::variant<double, std::string, bool>;
using Environment = std::map<std::string, Value>;

struct EvaluationResult {
    std::string ruleName;
    bool conditionPassed = false;
    std::string resultName;
    Value resultValue = std::string{};
    std::vector<std::string> trace;
};

class Evaluator {
public:
    explicit Evaluator(Environment inputs = {})
        : environment_(std::move(inputs)) {}

    EvaluationResult execute(const ASTNode& rule) {
        if (rule.type != NodeType::Rule) {
            throw std::runtime_error(
                "Evaluator error: expected a Rule AST node."
            );
        }

        if (rule.children.size() != 3) {
            throw std::runtime_error(
                "Evaluator error: a rule must contain a condition, "
                "THEN action, and ELSE action."
            );
        }

        EvaluationResult result;
        result.ruleName = rule.value;

        result.trace.push_back("Starting rule: " + rule.value);
        result.trace.push_back("Evaluating WHEN condition:");

        const Value conditionValue =
            evaluateCondition(*rule.children[0], result.trace);

        if (!std::holds_alternative<bool>(conditionValue)) {
            throw std::runtime_error(
                "Evaluator error: the WHEN condition must evaluate "
                "to true or false."
            );
        }

        result.conditionPassed = std::get<bool>(conditionValue);

        result.trace.push_back(
            std::string("WHEN condition evaluated to ") +
            (result.conditionPassed ? "TRUE" : "FALSE")
        );

        const ASTNode& selectedAction =
            result.conditionPassed
                ? *rule.children[1]
                : *rule.children[2];

        result.trace.push_back(
            result.conditionPassed
                ? "Selected THEN action."
                : "Selected ELSE action."
        );

        executeAction(selectedAction, result);

        result.trace.push_back(
            "Assigned " + result.resultName + " = " +
            valueToString(result.resultValue)
        );

        result.trace.push_back("Rule execution completed.");

        return result;
    }

    static std::string valueToString(const Value& value) {
        if (const auto* number = std::get_if<double>(&value)) {
            std::ostringstream output;
            output << std::setprecision(15) << *number;
            return output.str();
        }

        if (const auto* text = std::get_if<std::string>(&value)) {
            return *text;
        }

        return std::get<bool>(value) ? "true" : "false";
    }

private:
    Environment environment_;

    static bool isNumericText(
        const std::string& text,
        double& value
    ) {
        if (text.empty()) {
            return false;
        }

        char* end = nullptr;
        value = std::strtod(text.c_str(), &end);

        return end != text.c_str() &&
               end != nullptr &&
               *end == '\0' &&
               std::isfinite(value);
    }

    static Value parseLiteral(const std::string& text) {
        if (text == "true") {
            return true;
        }

        if (text == "false") {
            return false;
        }

        double number = 0.0;

        if (isNumericText(text, number)) {
            return number;
        }

        // The parser stores decoded string contents without quotes.
        return text;
    }

    static std::string describeNode(const ASTNode& node) {
        switch (node.type) {
            case NodeType::Literal:
                // Preserve the original token type. A string containing
                // digits (for example "80") must still be quoted.
                if (node.isStringLiteral) {
                    return "\"" + node.value + "\"";
                }
                return node.value;

            case NodeType::Identifier:
                return node.value;

            case NodeType::UnaryExpression:
                if (node.children.size() == 1) {
                    return node.value + " (" +
                           describeNode(*node.children[0]) + ")";
                }
                return node.value;

            case NodeType::BinaryExpression:
                if (node.children.size() == 2) {
                    return "(" +
                           describeNode(*node.children[0]) + " " +
                           node.value + " " +
                           describeNode(*node.children[1]) + ")";
                }
                return node.value;

            case NodeType::Assignment:
                return node.value + " = ...";

            case NodeType::Rule:
                return "RULE " + node.value;
        }

        return "<unknown expression>";
    }

    Value evaluateNode(const ASTNode& node) {
        switch (node.type) {
            case NodeType::Literal:
                // Keep quoted numeric-looking literals as strings.
                if (node.isStringLiteral) {
                    return node.value;
                }
                return parseLiteral(node.value);

            case NodeType::Identifier: {
                const auto found = environment_.find(node.value);

                if (found == environment_.end()) {
                    throw std::runtime_error(
                        "Evaluator error: undefined variable '" +
                        node.value + "'."
                    );
                }

                return found->second;
            }

            case NodeType::UnaryExpression: {
                if (node.children.size() != 1) {
                    throw std::runtime_error(
                        "Evaluator error: malformed unary expression."
                    );
                }

                const Value operand = evaluateNode(*node.children[0]);

                if (node.value == "NOT") {
                    if (!std::holds_alternative<bool>(operand)) {
                        throw std::runtime_error(
                            "Evaluator error: NOT requires "
                            "a boolean operand."
                        );
                    }

                    return !std::get<bool>(operand);
                }

                if (node.value == "-") {
                    if (!std::holds_alternative<double>(operand)) {
                        throw std::runtime_error(
                            "Evaluator error: unary '-' requires "
                            "a numeric operand."
                        );
                    }

                    return -std::get<double>(operand);
                }

                throw std::runtime_error(
                    "Evaluator error: unsupported unary operator '" +
                    node.value + "'."
                );
            }

            case NodeType::BinaryExpression:
                return evaluateBinary(node);

            case NodeType::Assignment:
                throw std::runtime_error(
                    "Evaluator error: assignment cannot be used "
                    "as an expression."
                );

            case NodeType::Rule:
                throw std::runtime_error(
                    "Evaluator error: a rule cannot be used "
                    "as an expression."
                );
        }

        throw std::runtime_error(
            "Evaluator error: unknown AST node type."
        );
    }

    // Evaluate a condition and append an explanation of its execution.
    Value evaluateCondition(
        const ASTNode& node,
        std::vector<std::string>& trace
    ) {
        if (node.type == NodeType::UnaryExpression &&
            node.value == "NOT") {
            if (node.children.size() != 1) {
                throw std::runtime_error(
                    "Evaluator error: malformed NOT expression."
                );
            }

            const Value operand =
                evaluateCondition(*node.children[0], trace);

            if (!std::holds_alternative<bool>(operand)) {
                throw std::runtime_error(
                    "Evaluator error: NOT requires "
                    "a boolean operand."
                );
            }

            const bool value = !std::get<bool>(operand);

            trace.push_back(
                "NOT result: " + std::string(value ? "TRUE" : "FALSE")
            );

            return value;
        }

        if (node.type == NodeType::BinaryExpression &&
            node.children.size() == 2) {
            const std::string& op = node.value;

            if (op == "AND" || op == "OR") {
                const Value left =
                    evaluateCondition(*node.children[0], trace);

                if (!std::holds_alternative<bool>(left)) {
                    throw std::runtime_error(
                        "Evaluator error: " + op +
                        " requires boolean operands."
                    );
                }

                const bool leftValue = std::get<bool>(left);

                // Preserve short-circuit behavior.
                if (op == "AND" && !leftValue) {
                    trace.push_back(
                        "AND short-circuit: left condition is FALSE; "
                        "right condition was not evaluated."
                    );
                    return false;
                }

                if (op == "OR" && leftValue) {
                    trace.push_back(
                        "OR short-circuit: left condition is TRUE; "
                        "right condition was not evaluated."
                    );
                    return true;
                }

                const Value right =
                    evaluateCondition(*node.children[1], trace);

                if (!std::holds_alternative<bool>(right)) {
                    throw std::runtime_error(
                        "Evaluator error: " + op +
                        " requires boolean operands."
                    );
                }

                const bool rightValue = std::get<bool>(right);
                const bool combined =
                    op == "AND"
                        ? leftValue && rightValue
                        : leftValue || rightValue;

                trace.push_back(
                    op + " result: " +
                    (combined ? "TRUE" : "FALSE")
                );

                return combined;
            }

            const Value left = evaluateNode(*node.children[0]);
            const Value right = evaluateNode(*node.children[1]);

            trace.push_back(
                "Condition: " + describeNode(node)
            );

            trace.push_back(
                "  Left value: " + valueToString(left)
            );

            trace.push_back(
                "  Right value: " + valueToString(right)
            );

            const Value comparison = evaluateBinary(node);

            if (!std::holds_alternative<bool>(comparison)) {
                throw std::runtime_error(
                    "Evaluator error: condition expression "
                    "did not produce a boolean."
                );
            }

            trace.push_back(
                "  Result: " +
                std::string(
                    std::get<bool>(comparison) ? "TRUE" : "FALSE"
                )
            );

            return comparison;
        }

        const Value value = evaluateNode(node);

        if (!std::holds_alternative<bool>(value)) {
            throw std::runtime_error(
                "Evaluator error: a condition must evaluate "
                "to a boolean."
            );
        }

        trace.push_back(
            "Condition: " + describeNode(node) +
            " => " +
            (std::get<bool>(value) ? "TRUE" : "FALSE")
        );

        return value;
    }

    Value evaluateBinary(const ASTNode& node) {
        if (node.children.size() != 2) {
            throw std::runtime_error(
                "Evaluator error: malformed binary expression."
            );
        }

        const std::string& op = node.value;
        const Value left = evaluateNode(*node.children[0]);

        // Preserve boolean short-circuit behavior for normal evaluation.
        if (op == "AND") {
            if (!std::holds_alternative<bool>(left)) {
                throw std::runtime_error(
                    "Evaluator error: AND requires boolean operands."
                );
            }

            if (!std::get<bool>(left)) {
                return false;
            }

            const Value right = evaluateNode(*node.children[1]);

            if (!std::holds_alternative<bool>(right)) {
                throw std::runtime_error(
                    "Evaluator error: AND requires boolean operands."
                );
            }

            return std::get<bool>(right);
        }

        if (op == "OR") {
            if (!std::holds_alternative<bool>(left)) {
                throw std::runtime_error(
                    "Evaluator error: OR requires boolean operands."
                );
            }

            if (std::get<bool>(left)) {
                return true;
            }

            const Value right = evaluateNode(*node.children[1]);

            if (!std::holds_alternative<bool>(right)) {
                throw std::runtime_error(
                    "Evaluator error: OR requires boolean operands."
                );
            }

            return std::get<bool>(right);
        }

        const Value right = evaluateNode(*node.children[1]);

        if (op == "==" || op == "!=") {
            const bool equal = valuesEqual(left, right);
            return op == "==" ? equal : !equal;
        }

        if (op == ">" || op == ">=" || op == "<" || op == "<=") {
            if (left.index() != right.index()) {
                throw std::runtime_error(
                    "Evaluator error: comparison operands "
                    "must have the same type."
                );
            }

            if (std::holds_alternative<double>(left)) {
                const double a = std::get<double>(left);
                const double b = std::get<double>(right);

                if (op == ">") return a > b;
                if (op == ">=") return a >= b;
                if (op == "<") return a < b;
                return a <= b;
            }

            if (std::holds_alternative<std::string>(left)) {
                const auto& a = std::get<std::string>(left);
                const auto& b = std::get<std::string>(right);

                if (op == ">") return a > b;
                if (op == ">=") return a >= b;
                if (op == "<") return a < b;
                return a <= b;
            }

            throw std::runtime_error(
                "Evaluator error: ordering comparisons require "
                "numbers or strings."
            );
        }

        if (op == "+" || op == "-" || op == "*" || op == "/") {
            if (!std::holds_alternative<double>(left) ||
                !std::holds_alternative<double>(right)) {
                throw std::runtime_error(
                    "Evaluator error: arithmetic operators require "
                    "numeric operands."
                );
            }

            const double a = std::get<double>(left);
            const double b = std::get<double>(right);

            if (op == "+") return a + b;
            if (op == "-") return a - b;
            if (op == "*") return a * b;

            if (b == 0.0) {
                throw std::runtime_error(
                    "Evaluator error: division by zero."
                );
            }

            return a / b;
        }

        throw std::runtime_error(
            "Evaluator error: unsupported binary operator '" +
            op + "'."
        );
    }

    static bool valuesEqual(
        const Value& left,
        const Value& right
    ) {
        if (left.index() != right.index()) {
            return false;
        }

        if (std::holds_alternative<double>(left)) {
            return std::get<double>(left) ==
                   std::get<double>(right);
        }

        if (std::holds_alternative<std::string>(left)) {
            return std::get<std::string>(left) ==
                   std::get<std::string>(right);
        }

        return std::get<bool>(left) == std::get<bool>(right);
    }

    void executeAction(
        const ASTNode& action,
        EvaluationResult& result
    ) {
        if (action.type != NodeType::Assignment) {
            throw std::runtime_error(
                "Evaluator error: expected an assignment action."
            );
        }

        if (action.children.size() != 1) {
            throw std::runtime_error(
                "Evaluator error: malformed assignment action."
            );
        }

        const Value assignedValue =
            evaluateNode(*action.children[0]);

        environment_[action.value] = assignedValue;

        result.resultName = action.value;
        result.resultValue = assignedValue;
    }
};

} // namespace ruleforge

#endif // RULEFORGE_EVALUATOR_H
