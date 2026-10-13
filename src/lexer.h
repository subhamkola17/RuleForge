#ifndef RULEFORGE_LEXER_H
#define RULEFORGE_LEXER_H

#include <cctype>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace ruleforge {

enum class TokenType {
    Identifier,
    Number,
    String,

    Rule,
    When,
    And,
    Or,
    Not,
    Then,
    Else,
    End,
    True,
    False,

    Assign,       // =
    Equal,        // ==
    NotEqual,     // !=
    Less,         // <
    LessEqual,    // <=
    Greater,      // >
    GreaterEqual, // >=

    Plus,         // +
    Minus,        // -
    Star,         // *
    Slash,        // /

    LeftParen,    // (
    RightParen,   // )
    LeftBrace,    // {
    RightBrace,   // }
    Comma,        // ,
    Dot,          // .

    EndOfFile
};

struct Token {
    TokenType type;
    std::string lexeme;
    std::size_t line;
    std::size_t column;

    Token(
        TokenType tokenType,
        std::string tokenLexeme,
        std::size_t tokenLine,
        std::size_t tokenColumn
    )
        : type(tokenType),
          lexeme(std::move(tokenLexeme)),
          line(tokenLine),
          column(tokenColumn) {}
};

class Lexer {
public:
    explicit Lexer(const std::string& source)
        : source_(source) {}

    std::vector<Token> tokenize() {
        std::vector<Token> tokens;

        while (!isAtEnd()) {
            skipWhitespaceAndComments();

            if (isAtEnd()) {
                break;
            }

            const std::size_t start = current_;
            const std::size_t startLine = line_;
            const std::size_t startColumn = column_;

            const char c = advance();

            if (isAlpha(c) || c == '_') {
                scanIdentifier(tokens, start, startLine, startColumn);
            } else if (isDigit(c)) {
                scanNumber(tokens, start, startLine, startColumn);
            } else {
                switch (c) {
                    case '"':
                        scanString(tokens, startLine, startColumn);
                        break;

                    case '=':
                        addToken(
                            tokens,
                            match('=') ? TokenType::Equal
                                       : TokenType::Assign,
                            start,
                            startLine,
                            startColumn
                        );
                        break;

                    case '!':
                        if (match('=')) {
                            addToken(tokens, TokenType::NotEqual,
                                     start, startLine, startColumn);
                        } else {
                            error("Expected '=' after '!'.",
                                  startLine, startColumn);
                        }
                        break;

                    case '<':
                        addToken(
                            tokens,
                            match('=') ? TokenType::LessEqual
                                       : TokenType::Less,
                            start, startLine, startColumn
                        );
                        break;

                    case '>':
                        addToken(
                            tokens,
                            match('=') ? TokenType::GreaterEqual
                                       : TokenType::Greater,
                            start, startLine, startColumn
                        );
                        break;

                    case '+':
                        addToken(tokens, TokenType::Plus,
                                 start, startLine, startColumn);
                        break;

                    case '-':
                        addToken(tokens, TokenType::Minus,
                                 start, startLine, startColumn);
                        break;

                    case '*':
                        addToken(tokens, TokenType::Star,
                                 start, startLine, startColumn);
                        break;

                    case '/':
                        addToken(tokens, TokenType::Slash,
                                 start, startLine, startColumn);
                        break;

                    case '(':
                        addToken(tokens, TokenType::LeftParen,
                                 start, startLine, startColumn);
                        break;

                    case ')':
                        addToken(tokens, TokenType::RightParen,
                                 start, startLine, startColumn);
                        break;

                    case '{':
                        addToken(tokens, TokenType::LeftBrace,
                                 start, startLine, startColumn);
                        break;

                    case '}':
                        addToken(tokens, TokenType::RightBrace,
                                 start, startLine, startColumn);
                        break;

                    case ',':
                        addToken(tokens, TokenType::Comma,
                                 start, startLine, startColumn);
                        break;

                    case '.':
                        addToken(tokens, TokenType::Dot,
                                 start, startLine, startColumn);
                        break;

                    default:
                        error(
                            std::string("Unexpected character '") +
                                c + "'.",
                            startLine,
                            startColumn
                        );
                }
            }
        }

        tokens.emplace_back(
            TokenType::EndOfFile,
            "",
            line_,
            column_
        );

        return tokens;
    }

private:
    const std::string& source_;

    std::size_t current_ = 0;
    std::size_t line_ = 1;
    std::size_t column_ = 1;

    bool isAtEnd() const {
        return current_ >= source_.size();
    }

    char peek() const {
        return isAtEnd() ? '\0' : source_[current_];
    }

    char peekNext() const {
        return current_ + 1 >= source_.size()
            ? '\0'
            : source_[current_ + 1];
    }

    char advance() {
        const char c = source_[current_++];

        if (c == '\n') {
            ++line_;
            column_ = 1;
        } else {
            ++column_;
        }

        return c;
    }

    bool match(char expected) {
        if (isAtEnd() || source_[current_] != expected) {
            return false;
        }

        advance();
        return true;
    }

    static bool isAlpha(char c) {
        return std::isalpha(static_cast<unsigned char>(c)) != 0;
    }

    static bool isDigit(char c) {
        return std::isdigit(static_cast<unsigned char>(c)) != 0;
    }

    static bool isAlphaNumeric(char c) {
        return isAlpha(c) || isDigit(c);
    }

    void skipWhitespaceAndComments() {
        while (!isAtEnd()) {
            const char c = peek();

            if (c == ' ' || c == '\r' ||
                c == '\t' || c == '\n') {
                advance();
            } else if (c == '#') {
                while (!isAtEnd() && peek() != '\n') {
                    advance();
                }
            } else if (c == '/' && peekNext() == '/') {
                advance();
                advance();

                while (!isAtEnd() && peek() != '\n') {
                    advance();
                }
            } else {
                break;
            }
        }
    }

    void scanIdentifier(
        std::vector<Token>& tokens,
        std::size_t start,
        std::size_t startLine,
        std::size_t startColumn
    ) {
        while (!isAtEnd() &&
               (isAlphaNumeric(peek()) || peek() == '_')) {
            advance();
        }

        const std::string text =
            source_.substr(start, current_ - start);

        addToken(
            tokens,
            keywordType(text),
            start,
            startLine,
            startColumn
        );
    }

    void scanNumber(
        std::vector<Token>& tokens,
        std::size_t start,
        std::size_t startLine,
        std::size_t startColumn
    ) {
        while (!isAtEnd() && isDigit(peek())) {
            advance();
        }

        if (peek() == '.' && isDigit(peekNext())) {
            advance();

            while (!isAtEnd() && isDigit(peek())) {
                advance();
            }
        }

        addToken(tokens, TokenType::Number,
                 start, startLine, startColumn);
    }

    void scanString(
        std::vector<Token>& tokens,
        std::size_t startLine,
        std::size_t startColumn
    ) {
        std::string value;

        while (!isAtEnd() && peek() != '"') {
            const char c = advance();

            if (c == '\n') {
                error(
                    "String literals cannot contain an unescaped newline.",
                    startLine,
                    startColumn
                );
            }

            if (c != '\\') {
                value += c;
                continue;
            }

            if (isAtEnd()) {
                error("Unterminated escape sequence.",
                      startLine, startColumn);
            }

            const char escaped = advance();

            switch (escaped) {
                case 'n':
                    value += '\n';
                    break;

                case 't':
                    value += '\t';
                    break;

                case '"':
                    value += '"';
                    break;

                case '\\':
                    value += '\\';
                    break;

                default:
                    error(
                        std::string("Unsupported escape sequence: \\") +
                            escaped,
                        startLine,
                        startColumn
                    );
            }
        }

        if (isAtEnd()) {
            error("Unterminated string literal.",
                  startLine, startColumn);
        }

        advance(); // Consume the closing quotation mark.

        tokens.emplace_back(
            TokenType::String,
            value,
            startLine,
            startColumn
        );
    }

    static TokenType keywordType(const std::string& text) {
        if (text == "RULE")  return TokenType::Rule;
        if (text == "WHEN")  return TokenType::When;
        if (text == "AND")   return TokenType::And;
        if (text == "OR")    return TokenType::Or;
        if (text == "NOT")   return TokenType::Not;
        if (text == "THEN")  return TokenType::Then;
        if (text == "ELSE")  return TokenType::Else;
        if (text == "END")   return TokenType::End;
        if (text == "true")  return TokenType::True;
        if (text == "false") return TokenType::False;

        return TokenType::Identifier;
    }

    void addToken(
        std::vector<Token>& tokens,
        TokenType type,
        std::size_t start,
        std::size_t startLine,
        std::size_t startColumn
    ) const {
        tokens.emplace_back(
            type,
            source_.substr(start, current_ - start),
            startLine,
            startColumn
        );
    }

    [[noreturn]] static void error(
        const std::string& message,
        std::size_t line,
        std::size_t column
    ) {
        throw std::runtime_error(
            "Lexer error at line " + std::to_string(line) +
            ", column " + std::to_string(column) +
            ": " + message
        );
    }
};

} // namespace ruleforge

#endif // RULEFORGE_LEXER_H