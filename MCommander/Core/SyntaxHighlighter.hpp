#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace mc {

enum class SyntaxLanguage {
    Plain,
    Cpp,
    Lua,
    Json,
    Shell,
    Markdown,
    Xml
};

enum class SyntaxTokenKind {
    Normal,
    Keyword,
    Type,
    String,
    Number,
    Comment,
    Preprocessor
};

struct SyntaxSpan {
    size_t start = 0;
    size_t length = 0;
    SyntaxTokenKind kind = SyntaxTokenKind::Normal;
};

SyntaxLanguage detectSyntaxLanguage(const std::string& fileNameOrPath);
std::vector<SyntaxSpan> tokenizeSyntaxLine(const std::string& line, SyntaxLanguage lang, bool& inBlockComment);
uint32_t syntaxTokenColor(SyntaxTokenKind kind);

} // namespace mc
