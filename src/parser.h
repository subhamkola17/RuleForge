#ifndef RULEFORGE_PARSER_H

#define RULEFORGE_PARSER_H



#include "lexer.h"



#include <cstddef>

#include <memory>

#include <stdexcept>

#include <string>

#include <utility>

#include <vector>



namespace ruleforge {



enum class NodeType {

    Rule,

    BinaryExpression,

    UnaryExpression,

    Literal,

    Identifier,

    Assignment

};



struct ASTNode {
    NodeType type;
    std::string value;
    std::vector<std::unique_ptr<ASTNode>> children;
    // Preserve literal type so traces distinguish numeric 80 from string "80".
    bool isStringLiteral = false;

    explicit ASTNode(
        NodeType nodeType,
        std::string nodeValue = "",
        bool stringLiteral = false
    )
        : type(nodeType),
          value(std::move(nodeValue)),
          isStringLiteral(stringLiteral) {}

};



class Parser {

public:

    explicit Parser(const std::vector<Token>& tokens)

        : tokens_(tokens) {

        if (tokens_.empty()) {

            throw std::runtime_error(

                "Parser error: token list is empty."

            );

        }

    }



    std::unique_ptr<ASTNode> parse() {

        auto rule = parseRule();



        consume(

            TokenType::EndOfFile,

            "Unexpected tokens after the rule."

        );



        return rule;

    }



private:

    const std::vector<Token>& tokens_;

    std::size_t current_ = 0;



    const Token& peek() const {

        return tokens_[current_];

    }



    const Token& previous() const {

        return tokens_[current_ - 1];

    }



    bool isAtEnd() const {

        return peek().type == TokenType::EndOfFile;

    }



    const Token& advance() {

        if (!isAtEnd()) {

            ++current_;

            return previous();

        }



        return peek();

    }



    // Fixed: EOF must be matchable by consume().

    bool check(TokenType type) const {

        return peek().type == type;

    }



    bool match(TokenType type) {

        if (!check(type)) {

            return false;

        }



        advance();

        return true;

    }



    const Token& consume(

        TokenType type,

        const std::string& message

    ) {

        if (check(type)) {

            return advance();

        }



        throw std::runtime_error(

            "Parser error at line " +

            std::to_string(peek().line) +

            ", column " +

            std::to_string(peek().column) +

            ": " + message

        );

    }



    std::unique_ptr<ASTNode> parseRule() {

        consume(TokenType::Rule, "Expected RULE.");



        const Token& name = consume(

            TokenType::Identifier,

            "Expected a rule name after RULE."

        );



        const std::string ruleName = name.lexeme;



        consume(

            TokenType::When,

            "Expected WHEN after the rule name."

        );



        auto condition = parseExpression();



        consume(

            TokenType::Then,

            "Expected THEN after the condition."

        );



        auto thenAction = parseAssignment();



        consume(

            TokenType::Else,

            "Expected ELSE."

        );



        auto elseAction = parseAssignment();



        consume(

            TokenType::End,

            "Expected END to close the rule."

        );



        auto rule = std::make_unique<ASTNode>(

            NodeType::Rule,

            ruleName

        );



        rule->children.push_back(std::move(condition));

        rule->children.push_back(std::move(thenAction));

        rule->children.push_back(std::move(elseAction));



        return rule;

    }



    std::unique_ptr<ASTNode> parseAssignment() {

        const Token& name = consume(

            TokenType::Identifier,

            "Expected a variable name in the assignment."

        );



        const std::string variableName = name.lexeme;



        consume(

            TokenType::Assign,

            "Expected '=' in the assignment."

        );



        auto value = parseExpression();



        auto assignment = std::make_unique<ASTNode>(

            NodeType::Assignment,

            variableName

        );



        assignment->children.push_back(std::move(value));



        return assignment;

    }



    std::unique_ptr<ASTNode> parseExpression() {

        return parseOr();

    }



    std::unique_ptr<ASTNode> parseOr() {

        auto expression = parseAnd();



        while (match(TokenType::Or)) {

            const std::string op = previous().lexeme;

            auto right = parseAnd();



            auto node = std::make_unique<ASTNode>(

                NodeType::BinaryExpression,

                op

            );



            node->children.push_back(std::move(expression));

            node->children.push_back(std::move(right));



            expression = std::move(node);

        }



        return expression;

    }



    std::unique_ptr<ASTNode> parseAnd() {

        auto expression = parseEquality();



        while (match(TokenType::And)) {

            const std::string op = previous().lexeme;

            auto right = parseEquality();



            auto node = std::make_unique<ASTNode>(

                NodeType::BinaryExpression,

                op

            );



            node->children.push_back(std::move(expression));

            node->children.push_back(std::move(right));



            expression = std::move(node);

        }



        return expression;

    }



    std::unique_ptr<ASTNode> parseEquality() {

        auto expression = parseComparison();



        while (

            match(TokenType::Equal) ||

            match(TokenType::NotEqual)

        ) {

            const std::string op = previous().lexeme;

            auto right = parseComparison();



            auto node = std::make_unique<ASTNode>(

                NodeType::BinaryExpression,

                op

            );



            node->children.push_back(std::move(expression));

            node->children.push_back(std::move(right));



            expression = std::move(node);

        }



        return expression;

    }



    std::unique_ptr<ASTNode> parseComparison() {

        auto expression = parseTerm();



        while (

            match(TokenType::Greater) ||

            match(TokenType::GreaterEqual) ||

            match(TokenType::Less) ||

            match(TokenType::LessEqual)

        ) {

            const std::string op = previous().lexeme;

            auto right = parseTerm();



            auto node = std::make_unique<ASTNode>(

                NodeType::BinaryExpression,

                op

            );



            node->children.push_back(std::move(expression));

            node->children.push_back(std::move(right));



            expression = std::move(node);

        }



        return expression;

    }



    std::unique_ptr<ASTNode> parseTerm() {

        auto expression = parseFactor();



        while (

            match(TokenType::Plus) ||

            match(TokenType::Minus)

        ) {

            const std::string op = previous().lexeme;

            auto right = parseFactor();



            auto node = std::make_unique<ASTNode>(

                NodeType::BinaryExpression,

                op

            );



            node->children.push_back(std::move(expression));

            node->children.push_back(std::move(right));



            expression = std::move(node);

        }



        return expression;

    }



    std::unique_ptr<ASTNode> parseFactor() {

        auto expression = parseUnary();



        while (

            match(TokenType::Star) ||

            match(TokenType::Slash)

        ) {

            const std::string op = previous().lexeme;

            auto right = parseUnary();



            auto node = std::make_unique<ASTNode>(

                NodeType::BinaryExpression,

                op

            );



            node->children.push_back(std::move(expression));

            node->children.push_back(std::move(right));



            expression = std::move(node);

        }



        return expression;

    }



    std::unique_ptr<ASTNode> parseUnary() {

        if (

            match(TokenType::Not) ||

            match(TokenType::Minus)

        ) {

            const std::string op = previous().lexeme;



            auto node = std::make_unique<ASTNode>(

                NodeType::UnaryExpression,

                op

            );



            node->children.push_back(parseUnary());



            return node;

        }



        return parsePrimary();

    }



    std::unique_ptr<ASTNode> parsePrimary() {

        if (match(TokenType::Number)) {

            return std::make_unique<ASTNode>(

                NodeType::Literal,

                previous().lexeme

            );

        }



        if (match(TokenType::String)) {
            return std::make_unique<ASTNode>(
                NodeType::Literal,
                previous().lexeme,
                true
            );

        }



        if (match(TokenType::True)) {

            return std::make_unique<ASTNode>(

                NodeType::Literal,

                "true"

            );

        }



        if (match(TokenType::False)) {

            return std::make_unique<ASTNode>(

                NodeType::Literal,

                "false"

            );

        }



        if (match(TokenType::Identifier)) {

            return std::make_unique<ASTNode>(

                NodeType::Identifier,

                previous().lexeme

            );

        }



        if (match(TokenType::LeftParen)) {

            auto expression = parseExpression();



            consume(

                TokenType::RightParen,

                "Expected ')' after the expression."

            );



            return expression;

        }



        throw std::runtime_error(

            "Parser error at line " +

            std::to_string(peek().line) +

            ", column " +

            std::to_string(peek().column) +

            ": Expected an expression."

        );

    }

};



} // namespace ruleforge



#endif // RULEFORGE_PARSER_H