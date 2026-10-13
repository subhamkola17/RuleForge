#ifndef RULEFORGE_VALIDATOR_H
#define RULEFORGE_VALIDATOR_H

#include "parser.h"

#include <cstddef>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace ruleforge {

enum class StaticType {
    Number,
    String,
    Boolean,
    Unknown,
    Invalid
};

struct ValidationDiagnostic {
    std::string message;
    std::size_t line = 0;
    std::size_t column = 0;
};

struct ValidationResult {
    bool valid = true;
    std::vector<ValidationDiagnostic> diagnostics;
};

class Validator {
public:
    // Provide the names and types of values expected in the input environment.
    // Example:
    // {{"marks", StaticType::Number}, {"attendance", StaticType::Number}}
    explicit Validator(std::map<std::string, StaticType> inputTypes = {})
        : inputTypes_(std::move(inputTypes)) {}

    ValidationResult validate(const ASTNode& rule) const {
        ValidationResult result;

        if (rule.type != NodeType::Rule) {
            addDiagnostic(result, "Expected a RULE node.");
            return finish(std::move(result));
        }

        if (rule.children.size() != 3) {
            addDiagnostic(
                result,
                "A rule must contain exactly a WHEN condition, THEN action, and ELSE action."
            );
            return finish(std::move(result));
        }

        const StaticType conditionType = inferType(*rule.children[0], result);
        if (conditionType != StaticType::Boolean &&
            conditionType != StaticType::Unknown &&
            conditionType != StaticType::Invalid) {
            addDiagnostic(
                result,
                "The WHEN condition must evaluate to a Boolean value, but it is " +
                typeName(conditionType) + "."
            );
        }

        validateAssignment(*rule.children[1], result, "THEN");
        validateAssignment(*rule.children[2], result, "ELSE");

        if (rule.children[1]->type == NodeType::Assignment &&
            rule.children[2]->type == NodeType::Assignment &&
            rule.children[1]->value != rule.children[2]->value) {
            addDiagnostic(
                result,
                "THEN and ELSE actions must assign to the same result variable."
            );
        }

        return finish(std::move(result));
    }

    static std::string typeName(StaticType type) {
        switch (type) {
            case StaticType::Number: return "Number";
            case StaticType::String: return "String";
            case StaticType::Boolean: return "Boolean";
            case StaticType::Unknown: return "Unknown";
            case StaticType::Invalid: return "Invalid";
        }
        return "Unknown";
    }

private:
    std::map<std::string, StaticType> inputTypes_;

    static ValidationResult finish(ValidationResult result) {
        result.valid = result.diagnostics.empty();
        return result;
    }

    static void addDiagnostic(
        ValidationResult& result,
        const std::string& message
    ) {
        result.diagnostics.push_back({message, 0, 0});
    }

    void validateAssignment(
        const ASTNode& action,
        ValidationResult& result,
        const std::string& branchName
    ) const {
        if (action.type != NodeType::Assignment) {
            addDiagnostic(
                result,
                branchName + " branch must be an assignment."
            );
            return;
        }

        if (action.children.size() != 1) {
            addDiagnostic(
                result,
                branchName + " assignment must contain exactly one expression."
            );
            return;
        }

        (void)inferType(*action.children[0], result);
    }

    StaticType inferType(
        const ASTNode& node,
        ValidationResult& result
    ) const {
        switch (node.type) {
            case NodeType::Literal:
                return literalType(node);

            case NodeType::Identifier: {
                const auto found = inputTypes_.find(node.value);
                if (found == inputTypes_.end()) {
                    addDiagnostic(
                        result,
                        "Variable '" + node.value +
                        "' has no declared input type. Supply its type to Validator."
                    );
                    return StaticType::Unknown;
                }
                return found->second;
            }

            case NodeType::UnaryExpression: {
                if (node.children.size() != 1) {
                    addDiagnostic(result, "Malformed unary expression.");
                    return StaticType::Invalid;
                }

                const StaticType operand = inferType(*node.children[0], result);
                if (operand == StaticType::Unknown ||
                    operand == StaticType::Invalid) {
                    return operand;
                }

                if (node.value == "NOT") {
                    if (operand != StaticType::Boolean) {
                        addDiagnostic(
                            result,
                            "Operator NOT requires a Boolean operand, not " +
                            typeName(operand) + "."
                        );
                        return StaticType::Invalid;
                    }
                    return StaticType::Boolean;
                }

                if (node.value == "-") {
                    if (operand != StaticType::Number) {
                        addDiagnostic(
                            result,
                            "Unary '-' requires a Number operand, not " +
                            typeName(operand) + "."
                        );
                        return StaticType::Invalid;
                    }
                    return StaticType::Number;
                }

                addDiagnostic(
                    result,
                    "Unsupported unary operator '" + node.value + "'."
                );
                return StaticType::Invalid;
            }

            case NodeType::BinaryExpression:
                return inferBinaryType(node, result);

            case NodeType::Assignment:
                addDiagnostic(
                    result,
                    "An assignment cannot be used inside an expression."
                );
                return StaticType::Invalid;

            case NodeType::Rule:
                addDiagnostic(
                    result,
                    "A RULE node cannot be used inside an expression."
                );
                return StaticType::Invalid;
        }

        addDiagnostic(result, "Unknown AST node type.");
        return StaticType::Invalid;
    }

    StaticType inferBinaryType(
        const ASTNode& node,
        ValidationResult& result
    ) const {
        if (node.children.size() != 2) {
            addDiagnostic(result, "Malformed binary expression.");
            return StaticType::Invalid;
        }

        const StaticType left = inferType(*node.children[0], result);
        const StaticType right = inferType(*node.children[1], result);

        if (left == StaticType::Invalid || right == StaticType::Invalid) {
            return StaticType::Invalid;
        }
        if (left == StaticType::Unknown || right == StaticType::Unknown) {
            return StaticType::Unknown;
        }

        const std::string& op = node.value;

        if (op == "AND" || op == "OR") {
            if (left != StaticType::Boolean || right != StaticType::Boolean) {
                addDiagnostic(
                    result,
                    "Operator " + op +
                    " requires Boolean operands; received " +
                    typeName(left) + " and " + typeName(right) + "."
                );
                return StaticType::Invalid;
            }
            return StaticType::Boolean;
        }

        if (op == "==" || op == "!=") {
            if (left != right) {
                addDiagnostic(
                    result,
                    "Operator " + op +
                    " requires operands of the same type; received " +
                    typeName(left) + " and " + typeName(right) + "."
                );
                return StaticType::Invalid;
            }
            return StaticType::Boolean;
        }

        if (op == ">" || op == ">=" || op == "<" || op == "<=") {
            if (left != right ||
                (left != StaticType::Number && left != StaticType::String)) {
                addDiagnostic(
                    result,
                    "Operator " + op +
                    " requires two Numbers or two Strings of the same type."
                );
                return StaticType::Invalid;
            }
            return StaticType::Boolean;
        }

        if (op == "+" || op == "-" || op == "*" || op == "/") {
            if (left != StaticType::Number || right != StaticType::Number) {
                addDiagnostic(
                    result,
                    "Arithmetic operator " + op +
                    " requires Number operands; received " +
                    typeName(left) + " and " + typeName(right) + "."
                );
                return StaticType::Invalid;
            }
            return StaticType::Number;
        }

        addDiagnostic(result, "Unsupported binary operator '" + op + "'.");
        return StaticType::Invalid;
    }

    static StaticType literalType(const ASTNode& node) {
        // The parser records whether a literal originated from a quoted
        // string token. Do not infer its type from its contents: "80" is
        // a String, while 80 is a Number.
        if (node.isStringLiteral) {
            return StaticType::String;
        }

        if (node.value == "true" || node.value == "false") {
            return StaticType::Boolean;
        }

        return StaticType::Number;
    }
};

} // namespace ruleforge

#endif // RULEFORGE_VALIDATOR_H
