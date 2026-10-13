#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <variant>

#include "lexer.h"
#include "parser.h"
#include "validator.h"
#include "evaluator.h"

namespace {

int passed = 0;
int failed = 0;

void check(bool condition, const std::string& name) {
    if (condition) {
        ++passed;
        std::cout << "[PASS] " << name << '\n';
    } else {
        ++failed;
        std::cout << "[FAIL] " << name << '\n';
    }
}

std::unique_ptr<ruleforge::ASTNode> parseRule(
    const std::string& source
) {
    ruleforge::Lexer lexer(source);
    const auto tokens = lexer.tokenize();
    ruleforge::Parser parser(tokens);
    return parser.parse();
}

const std::string validRule = R"(
RULE scholarship_eligible
WHEN marks >= 80 AND attendance >= 75
THEN result = "Eligible"
ELSE result = "Not eligible"
END
)";

const std::map<std::string, ruleforge::StaticType> numericInputs{
    {"marks", ruleforge::StaticType::Number},
    {"attendance", ruleforge::StaticType::Number}
};

} // namespace

int main() {
    std::cout << "====================================\n";
    std::cout << "       RuleForge Unit Tests\n";
    std::cout << "====================================\n\n";

    // TEST 1: Valid rule passes validation.
    try {
        auto ast = parseRule(validRule);
        const ruleforge::Validator validator(numericInputs);
        const auto result = validator.validate(*ast);

        check(result.valid,
              "valid scholarship rule passes validation");
    } catch (const std::exception& ex) {
        check(false, std::string("valid rule: ") + ex.what());
    }

    // TEST 2: Undeclared variable is rejected.
    try {
        const std::string source = R"(
RULE unknown_variable
WHEN marks >= 80 AND attendance >= 75 AND age >= 18
THEN result = "Eligible"
ELSE result = "Not eligible"
END
)";

        auto ast = parseRule(source);
        const ruleforge::Validator validator(numericInputs);
        const auto result = validator.validate(*ast);

        check(!result.valid,
              "undeclared variable is rejected");
    } catch (const std::exception& ex) {
        check(false, std::string("unknown variable: ") + ex.what());
    }

    // TEST 3: String-versus-number comparison is rejected.
    try {
        const std::string source = R"(
RULE invalid_comparison
WHEN studentName >= 80
THEN result = "Eligible"
ELSE result = "Not eligible"
END
)";

        auto ast = parseRule(source);

        const ruleforge::Validator validator({
            {"studentName", ruleforge::StaticType::String}
        });

        const auto result = validator.validate(*ast);

        check(!result.valid,
              "invalid string-versus-number comparison is rejected");
    } catch (const std::exception& ex) {
        check(false, std::string("invalid comparison: ") + ex.what());
    }

    // TEST 4: THEN and ELSE must use the same output variable.
    try {
        const std::string source = R"(
RULE mismatched_outputs
WHEN marks >= 80
THEN result = "Eligible"
ELSE status = "Not eligible"
END
)";

        auto ast = parseRule(source);

        const ruleforge::Validator validator({
            {"marks", ruleforge::StaticType::Number}
        });

        const auto result = validator.validate(*ast);

        check(!result.valid,
              "mismatched THEN/ELSE output variables are rejected");
    } catch (const std::exception& ex) {
        check(false, std::string("mismatched outputs: ") + ex.what());
    }

    // TEST 5: Eligible inputs select THEN.
    try {
        auto ast = parseRule(validRule);

        ruleforge::Evaluator evaluator({
            {"marks", 85.0},
            {"attendance", 80.0}
        });

        const auto result = evaluator.execute(*ast);

        check(
            result.conditionPassed &&
            result.resultName == "result" &&
            std::get<std::string>(result.resultValue) == "Eligible",
            "eligible inputs return Eligible"
        );
    } catch (const std::exception& ex) {
        check(false, std::string("eligible inputs: ") + ex.what());
    }

    // TEST 6: Low attendance selects ELSE.
    try {
        auto ast = parseRule(validRule);

        ruleforge::Evaluator evaluator({
            {"marks", 85.0},
            {"attendance", 60.0}
        });

        const auto result = evaluator.execute(*ast);

        check(
            !result.conditionPassed &&
            result.resultName == "result" &&
            std::get<std::string>(result.resultValue) ==
                "Not eligible",
            "low attendance returns Not eligible"
        );
    } catch (const std::exception& ex) {
        check(false, std::string("low attendance: ") + ex.what());
    }

    // TEST 7: Missing runtime input raises an error.
    try {
        auto ast = parseRule(validRule);

        ruleforge::Evaluator evaluator({
            {"marks", 85.0}
        });

        bool threw = false;

        try {
            (void)evaluator.execute(*ast);
        } catch (const std::exception&) {
            threw = true;
        }

        check(threw,
              "missing runtime input raises an error");
    } catch (const std::exception& ex) {
        check(false, std::string("missing input setup: ") + ex.what());
    }

    // TEST 8: Malformed rule syntax is rejected.
    try {
        const std::string malformed = R"(
RULE malformed
WHEN marks >=
THEN result = "Eligible"
ELSE result = "Not eligible"
END
)";

        bool threw = false;

        try {
            (void)parseRule(malformed);
        } catch (const std::exception&) {
            threw = true;
        }

        check(threw,
              "malformed rule syntax is rejected");
    } catch (const std::exception& ex) {
        check(false, std::string("malformed syntax test: ") + ex.what());
    }

    // TEST 9: Empty source is rejected.
    try {
        bool threw = false;

        try {
            (void)parseRule("");
        } catch (const std::exception&) {
            threw = true;
        }

        check(threw,
              "empty rule source is rejected");
    } catch (const std::exception& ex) {
        check(false, std::string("empty source test: ") + ex.what());
    }

    // TEST 10: A valid rule produces an AST.
    try {
        auto ast = parseRule(validRule);

        check(
            ast != nullptr,
            "parser creates an AST for a valid rule"
        );
    } catch (const std::exception& ex) {
        check(false, std::string("AST creation test: ") + ex.what());
    }

    // TEST 11: Identical inputs produce identical results.
    try {
        auto ast = parseRule(validRule);

        ruleforge::Evaluator firstEvaluator({
            {"marks", 85.0},
            {"attendance", 80.0}
        });

        ruleforge::Evaluator secondEvaluator({
            {"marks", 85.0},
            {"attendance", 80.0}
        });

        const auto first = firstEvaluator.execute(*ast);
        const auto second = secondEvaluator.execute(*ast);

        check(
            first.conditionPassed == second.conditionPassed &&
            first.resultName == second.resultName &&
            first.resultValue == second.resultValue,
            "identical inputs produce identical results"
        );
    } catch (const std::exception& ex) {
        check(false, std::string("determinism test: ") + ex.what());
    }

    // TEST 12: Exact threshold values satisfy the rule.
    try {
        auto ast = parseRule(validRule);

        ruleforge::Evaluator evaluator({
            {"marks", 80.0},
            {"attendance", 75.0}
        });

        const auto result = evaluator.execute(*ast);

        check(
            result.conditionPassed &&
            std::get<std::string>(result.resultValue) == "Eligible",
            "minimum qualifying boundary values are accepted"
        );
    } catch (const std::exception& ex) {
        check(false, std::string("boundary test: ") + ex.what());
    }

    std::cout << "\n====================================\n";
    std::cout << "Results: " << passed
              << " passed, " << failed << " failed\n";
    std::cout << "====================================\n";

    return failed == 0 ? 0 : 1;
}