#include <cctype>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "lexer.h"
#include "parser.h"
#include "validator.h"
#include "evaluator.h"

namespace {

using Environment = ruleforge::Environment;
using StaticType = ruleforge::StaticType;

constexpr const char* VERSION = "0.5.1";


std::string readFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);

    if (!file) {
        throw std::runtime_error("Cannot open file: " + path);
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();

    if (file.bad()) {
        throw std::runtime_error(
            "Failed while reading file: " + path
        );
    }

    return buffer.str();
}

// ------------------------------------------------------------
// Minimal JSON input parser.
// Supported values: numbers, strings, and booleans.
// Arrays, nested objects, null, and Unicode escapes are not
// supported by this initial parser.
// ------------------------------------------------------------

class JsonInputParser {
public:
    explicit JsonInputParser(const std::string& text)
        : text_(text) {}

    Environment parse() {
        Environment values;

        skipWhitespace();
        expect('{');
        skipWhitespace();

        if (consume('}')) {
            skipWhitespace();
            ensureEnd();
            return values;
        }

        while (true) {
            skipWhitespace();

            const std::string key = parseString();

            if (values.find(key) != values.end()) {
                fail("Duplicate JSON key: " + key);
            }

            skipWhitespace();
            expect(':');
            skipWhitespace();

            values.emplace(key, parseValue());

            skipWhitespace();

            if (consume('}')) {
                break;
            }

            expect(',');
        }

        skipWhitespace();
        ensureEnd();

        return values;
    }

private:
    const std::string& text_;
    std::size_t position_ = 0;

    void skipWhitespace() {
        while (
            position_ < text_.size() &&
            std::isspace(
                static_cast<unsigned char>(text_[position_])
            )
        ) {
            ++position_;
        }
    }

    bool consume(char expected) {
        if (
            position_ < text_.size() &&
            text_[position_] == expected
        ) {
            ++position_;
            return true;
        }

        return false;
    }

    void expect(char expected) {
        if (!consume(expected)) {
            fail(std::string("Expected '") + expected + "'.");
        }
    }

    [[noreturn]] void fail(const std::string& message) const {
        throw std::runtime_error(
            "JSON error at character " +
            std::to_string(position_ + 1) + ": " + message
        );
    }

    void ensureEnd() {
        if (position_ != text_.size()) {
            fail("Unexpected content after the JSON object.");
        }
    }

    std::string parseString() {
        expect('"');

        std::string result;

        while (position_ < text_.size()) {
            const char c = text_[position_++];

            if (c == '"') {
                return result;
            }

            if (static_cast<unsigned char>(c) < 0x20) {
                fail("Unescaped control character in string.");
            }

            if (c != '\\') {
                result += c;
                continue;
            }

            if (position_ >= text_.size()) {
                fail("Incomplete string escape.");
            }

            const char escaped = text_[position_++];

            switch (escaped) {
                case '"':
                    result += '"';
                    break;
                case '\\':
                    result += '\\';
                    break;
                case '/':
                    result += '/';
                    break;
                case 'b':
                    result += '\b';
                    break;
                case 'f':
                    result += '\f';
                    break;
                case 'n':
                    result += '\n';
                    break;
                case 'r':
                    result += '\r';
                    break;
                case 't':
                    result += '\t';
                    break;
                case 'u':
                    fail(
                        "Unicode \\u escapes are not supported by "
                        "this minimal JSON reader. Use UTF-8 "
                        "characters directly."
                    );
                default:
                    fail("Invalid string escape.");
            }
        }

        fail("Unterminated JSON string.");
    }

    ruleforge::Value parseValue() {
        if (position_ >= text_.size()) {
            fail("Expected a JSON value.");
        }

        if (text_[position_] == '"') {
            return parseString();
        }

        if (
            text_[position_] == 't' &&
            text_.compare(position_, 4, "true") == 0
        ) {
            position_ += 4;
            return true;
        }

        if (
            text_[position_] == 'f' &&
            text_.compare(position_, 5, "false") == 0
        ) {
            position_ += 5;
            return false;
        }

        if (
            text_[position_] == '-' ||
            std::isdigit(
                static_cast<unsigned char>(text_[position_])
            )
        ) {
            return parseNumber();
        }

        fail(
            "Supported values are numbers, strings, and booleans. "
            "Arrays, nested objects, and null are not supported."
        );
    }

    double parseNumber() {
        const std::size_t start = position_;

        consume('-');

        if (consume('0')) {
            if (
                position_ < text_.size() &&
                std::isdigit(
                    static_cast<unsigned char>(text_[position_])
                )
            ) {
                fail("Leading zero in JSON number.");
            }
        } else {
            if (
                position_ >= text_.size() ||
                !std::isdigit(
                    static_cast<unsigned char>(text_[position_])
                )
            ) {
                fail("Invalid JSON number.");
            }

            while (
                position_ < text_.size() &&
                std::isdigit(
                    static_cast<unsigned char>(text_[position_])
                )
            ) {
                ++position_;
            }
        }

        if (consume('.')) {
            const std::size_t fractionStart = position_;

            while (
                position_ < text_.size() &&
                std::isdigit(
                    static_cast<unsigned char>(text_[position_])
                )
            ) {
                ++position_;
            }

            if (fractionStart == position_) {
                fail("Expected digits after decimal point.");
            }
        }

        if (
            position_ < text_.size() &&
            (text_[position_] == 'e' ||
             text_[position_] == 'E')
        ) {
            ++position_;

            if (
                position_ < text_.size() &&
                (text_[position_] == '+' ||
                 text_[position_] == '-')
            ) {
                ++position_;
            }

            const std::size_t exponentStart = position_;

            while (
                position_ < text_.size() &&
                std::isdigit(
                    static_cast<unsigned char>(text_[position_])
                )
            ) {
                ++position_;
            }

            if (exponentStart == position_) {
                fail("Invalid number exponent.");
            }
        }

        const std::string numberText =
            text_.substr(start, position_ - start);

        std::size_t consumed = 0;
        double value = 0.0;

        try {
            value = std::stod(numberText, &consumed);
        } catch (const std::exception&) {
            fail("Number is outside the supported range.");
        }

        if (consumed != numberText.size()) {
            fail("Invalid number.");
        }

        if (!std::isfinite(value)) {
            fail("Number must be finite.");
        }

        return value;
    }
};

// ------------------------------------------------------------
// Type helpers and AST output
// ------------------------------------------------------------

StaticType getStaticType(const ruleforge::Value& value) {
    if (std::holds_alternative<double>(value)) {
        return StaticType::Number;
    }

    if (std::holds_alternative<std::string>(value)) {
        return StaticType::String;
    }

    if (std::holds_alternative<bool>(value)) {
        return StaticType::Boolean;
    }

    return StaticType::Unknown;
}

void printAST(const ruleforge::ASTNode& node, int depth = 0) {
    for (int i = 0; i < depth; ++i) {
        std::cout << "  ";
    }

    using ruleforge::NodeType;

    switch (node.type) {
        case NodeType::Rule:
            std::cout << "Rule: ";
            break;
        case NodeType::BinaryExpression:
            std::cout << "BinaryExpression: ";
            break;
        case NodeType::UnaryExpression:
            std::cout << "UnaryExpression: ";
            break;
        case NodeType::Literal:
            std::cout << "Literal: ";
            break;
        case NodeType::Identifier:
            std::cout << "Identifier: ";
            break;
        case NodeType::Assignment:
            std::cout << "Assignment: ";
            break;
    }

    std::cout << node.value << '\n';

    for (const auto& child : node.children) {
        printAST(*child, depth + 1);
    }
}

// ------------------------------------------------------------
// JSON output helpers
// ------------------------------------------------------------

std::string jsonEscape(const std::string& value) {
    std::ostringstream out;
    for (unsigned char c : value) {
        switch (c) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\b': out << "\\b"; break;
            case '\f': out << "\\f"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (c < 0x20) {
                    const char* hex = "0123456789abcdef";
                    out << "\\u00" << hex[(c >> 4) & 0x0f]
                        << hex[c & 0x0f];
                } else {
                    out << static_cast<char>(c);
                }
        }
    }
    return out.str();
}

std::string trim(const std::string& value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::vector<std::string> extractTrace(const std::string& report) {
    std::vector<std::string> trace;
    std::istringstream input(report);
    std::string line;
    bool inTrace = false;
    while (std::getline(input, line)) {
        if (line == "Execution trace:") {
            inTrace = true;
            continue;
        }
        if (inTrace && line.find("========================================") != std::string::npos) {
            break;
        }
        if (inTrace) {
            const std::string item = trim(line);
            if (!item.empty()) trace.push_back(item);
        }
    }
    return trace;
}

std::string extractLineValue(const std::string& report, const std::string& prefix) {
    std::istringstream input(report);
    std::string line;
    std::string lastMatch;
    while (std::getline(input, line)) {
        const auto pos = line.find(prefix);
        if (pos != std::string::npos) {
            lastMatch = trim(line.substr(pos + prefix.size()));
        }
    }
    return lastMatch;
}

std::string valueToJson(const ruleforge::Value& value) {
    if (const auto* number = std::get_if<double>(&value)) {
        if (!std::isfinite(*number)) {
            throw std::runtime_error("Cannot serialize a non-finite number to JSON.");
        }
        std::ostringstream out;
        out << std::setprecision(15) << *number;
        return out.str();
    }
    if (const auto* text = std::get_if<std::string>(&value)) {
        return "\"" + jsonEscape(*text) + "\"";
    }
    return std::get<bool>(value) ? "true" : "false";
}

void printJsonReport(
    const std::string& status,
    const std::string& mode,
    const std::string& rulePath,
    const std::string& inputPath,
    const std::string& report,
    const ruleforge::EvaluationResult* evaluation = nullptr,
    const std::string& error = {}
) {
    std::cout << "{\n"
              << "  \"engine\": \"RuleForge\",\n"
              << "  \"version\": \"" << VERSION << "\",\n"
              << "  \"status\": \"" << jsonEscape(status) << "\",\n"
              << "  \"mode\": \"" << jsonEscape(mode) << "\",\n"
              << "  \"ruleFile\": \"" << jsonEscape(rulePath) << "\",\n"
              << "  \"inputFile\": \"" << jsonEscape(inputPath) << "\"";

    if (!error.empty()) {
        std::cout << ",\n  \"error\": \"" << jsonEscape(error) << "\"";
    }
    if (!report.empty()) {
        const std::string rule = evaluation
            ? evaluation->ruleName
            : extractLineValue(report, "Rule: ");

        if (!rule.empty()) {
            std::cout << ",\n  \"rule\": \"" << jsonEscape(rule) << "\"";
        }

        if (evaluation != nullptr) {
            std::cout << ",\n  \"conditionPassed\": "
                      << (evaluation->conditionPassed ? "true" : "false")
                      << ",\n  \"result\": {\n"
                      << "    \"name\": \"" << jsonEscape(evaluation->resultName) << "\",\n"
                      << "    \"value\": " << valueToJson(evaluation->resultValue) << "\n"
                      << "  }";
        } else {
            const std::string condition = extractLineValue(report, "Condition: ");
            if (!condition.empty()) {
                std::cout << ",\n  \"conditionPassed\": "
                          << (condition == "TRUE" ? "true" : "false");
            }
        }

        const std::vector<std::string> trace = evaluation != nullptr
            ? evaluation->trace
            : extractTrace(report);

        std::cout << ",\n  \"trace\": [";
        for (std::size_t i = 0; i < trace.size(); ++i) {
            if (i) std::cout << ',';
            std::cout << "\n    \"" << jsonEscape(trace[i]) << "\"";
        }
        if (!trace.empty()) std::cout << '\n' << "  ";
        std::cout << "]";
        std::cout << ",\n  \"textReport\": \"" << jsonEscape(report) << "\"";
    }
    std::cout << "\n}\n";
}

// ------------------------------------------------------------
// CLI help
// ------------------------------------------------------------

void printUsage(const char* executable) {
    std::cout
        << "RuleForge v" << VERSION << '\n'
        << "A local rule validation and execution engine.\n\n"
        << "Usage:\n"
        << "  " << executable
        << " run <rule-file.rf> <input-file.json> [--format json]\n"
        << "  " << executable
        << " validate <rule-file.rf> <input-file.json> [--format json]\n"
        << "  " << executable << " --help\n"
        << "  " << executable << " --version\n\n"
        << "Commands:\n"
        << "  run       Validate and execute a rule.\n"
        << "  validate  Validate a rule without executing it.\n"
        << "  --format json  Emit a structured JSON report.\n\n"
        << "Examples:\n"
        << "  " << executable
        << " run examples/scholarship.rf "
        << "examples/scholarship.json\n"
        << "  " << executable
        << " validate examples/tariff.rf "
        << "examples/tariff.json\n\n"
        << "Backward compatibility:\n"
        << "  " << executable
        << " examples/scholarship.rf "
        << "examples/scholarship.json\n";
}

// ------------------------------------------------------------
// Main pipeline
//
// executeRule = true:
//   Lex -> Parse -> Validate -> Execute
//
// executeRule = false:
//   Lex -> Parse -> Validate -> Stop
// ------------------------------------------------------------

int runRuleFileHuman(
    const std::string& rulePath,
    const std::string& inputPath,
    bool executeRule,
    ruleforge::EvaluationResult* structuredResult = nullptr
) {
    const std::string source = readFile(rulePath);
    const std::string jsonText = readFile(inputPath);

    if (source.empty()) {
        throw std::runtime_error("The rule file is empty.");
    }

    if (source.size() > 1024 * 1024) {
        throw std::runtime_error(
            "Rule file exceeds the 1 MiB input limit."
        );
    }

    if (jsonText.size() > 1024 * 1024) {
        throw std::runtime_error(
            "JSON input file exceeds the 1 MiB input limit."
        );
    }

    std::cout
        << "========================================\n"
        << "            RuleForge v" << VERSION << '\n'
        << (executeRule
                ? "             Rule Execution\n"
                : "              Rule Validation\n")
        << "========================================\n";

    std::cout << "\nRule file: " << rulePath << '\n';
    std::cout << "Input file: " << inputPath << '\n';

    JsonInputParser jsonParser(jsonText);
    const Environment inputs = jsonParser.parse();

    std::cout << "\nLoaded " << inputs.size()
              << " input value(s).\n";

    std::cout << "\n[1/3] Lexical analysis...\n";

    ruleforge::Lexer lexer(source);
    const std::vector<ruleforge::Token> tokens = lexer.tokenize();

    std::cout << "Lexing successful: ";

    if (!tokens.empty()) {
        std::cout << tokens.size() - 1;
    } else {
        std::cout << 0;
    }

    std::cout << " token(s).\n";

    std::cout << "\n[2/3] Parsing...\n";

    ruleforge::Parser parser(tokens);
    const auto ast = parser.parse();

    if (!ast) {
        throw std::runtime_error("Parser returned an empty AST.");
    }

    std::cout << "Parsing successful.\n";

    std::map<std::string, StaticType> inputTypes;

    for (const auto& [name, value] : inputs) {
        inputTypes.emplace(name, getStaticType(value));
    }

    std::cout << "\n[3/3] Static validation...\n";

    const ruleforge::Validator validator(inputTypes);
    const ruleforge::ValidationResult validation =
        validator.validate(*ast);

    if (!validation.valid) {
        std::cerr << "\nRULE VALIDATION FAILED\n";

        for (const auto& diagnostic : validation.diagnostics) {
            std::cerr << "  - " << diagnostic.message << '\n';
        }

        std::cerr
            << "\nOperation stopped. Fix the rule or JSON inputs.\n";

        return 1;
    }

    std::cout << "Rule validation successful.\n";

    if (!executeRule) {
        std::cout
            << "\n========================================\n"
            << "       VALIDATION PASSED\n"
            << "========================================\n"
            << "The rule passed static validation.\n"
            << "Execution was not performed.\n"
            << "========================================\n";

        return 0;
    }

    std::cout << "\nAbstract Syntax Tree:\n";
    printAST(*ast);

    std::cout << "\nInput values:\n";

    for (const auto& [name, value] : inputs) {
        std::cout << "  " << name << " = "
                  << ruleforge::Evaluator::valueToString(value)
                  << '\n';
    }

    std::cout << "\n[4/4] Executing rule...\n";

    ruleforge::Evaluator evaluator(inputs);
    const ruleforge::EvaluationResult result =
        evaluator.execute(*ast);

    if (structuredResult != nullptr) {
        *structuredResult = result;
    }

    std::cout << "\nExecution trace:\n";

    for (const auto& entry : result.trace) {
        std::cout << "  " << entry << '\n';
    }

    std::cout
        << "\n========================================\n"
        << "              FINAL RESULT\n"
        << "========================================\n";

    std::cout << "Rule: " << result.ruleName << '\n';

    std::cout << "Condition: "
              << (result.conditionPassed ? "TRUE" : "FALSE")
              << '\n';

    std::cout << result.resultName << " = "
              << ruleforge::Evaluator::valueToString(
                     result.resultValue
                 )
              << '\n';

    std::cout << "========================================\n";

    return 0;
}


int runRuleFile(
    const std::string& rulePath,
    const std::string& inputPath,
    bool executeRule,
    bool jsonOutput
) {
    if (!jsonOutput) return runRuleFileHuman(rulePath, inputPath, executeRule);

    std::ostringstream capturedOut;
    std::ostringstream capturedErr;
    auto* oldOut = std::cout.rdbuf(capturedOut.rdbuf());
    auto* oldErr = std::cerr.rdbuf(capturedErr.rdbuf());
    int code = 1;
    std::string error;
    ruleforge::EvaluationResult structuredResult;
    bool hasEvaluationResult = false;
    try {
        code = runRuleFileHuman(
            rulePath,
            inputPath,
            executeRule,
            executeRule ? &structuredResult : nullptr
        );
        hasEvaluationResult = (code == 0 && executeRule);
    } catch (const std::exception& ex) {
        error = ex.what();
    }
    std::cout.rdbuf(oldOut);
    std::cerr.rdbuf(oldErr);

    const std::string report = capturedOut.str();
    if (code != 0 && error.empty()) {
        error = trim(capturedErr.str());
        if (error.empty()) error = "Rule validation or execution failed.";
    }
    printJsonReport(
        code == 0 ? "success" : "error",
        executeRule ? "run" : "validate",
        rulePath,
        inputPath,
        report,
        hasEvaluationResult ? &structuredResult : nullptr,
        error
    );
    return code;
}

} // namespace

// ------------------------------------------------------------
// CLI entry point
// ------------------------------------------------------------

int main(int argc, char* argv[]) {
    try {
        if (argc == 2) {
            const std::string argument = argv[1];
            if (argument == "--help" || argument == "-h") {
                printUsage(argv[0]);
                return 0;
            }
            if (argument == "--version" || argument == "-v") {
                std::cout << "RuleForge v" << VERSION << '\n';
                return 0;
            }
        }

        if (argc == 4 || argc == 6) {
            const std::string command = argv[1];
            bool jsonOutput = false;
            if (argc == 6) {
                if (std::string(argv[4]) != "--format" ||
                    std::string(argv[5]) != "json") {
                    std::cerr << "Invalid output option. Use --format json.\n";
                    return 2;
                }
                jsonOutput = true;
            }
            if (command == "run")
                return runRuleFile(argv[2], argv[3], true, jsonOutput);
            if (command == "validate")
                return runRuleFile(argv[2], argv[3], false, jsonOutput);
            std::cerr << "Unknown command: " << command << "\n\n";
            printUsage(argv[0]);
            return 2;
        }

        // Backward-compatible positional invocation.
        if (argc == 3)
            return runRuleFile(argv[1], argv[2], true, false);

        printUsage(argv[0]);
        return 2;
    } catch (const std::exception& ex) {
        std::cerr << "\nRuleForge error:\n" << ex.what() << '\n';
        return 1;
    }
}
