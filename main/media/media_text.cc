#include "media_text.h"

namespace media {
namespace {

// Appends a code point as UTF-8.
void AppendUtf8(std::string& out, uint32_t codepoint) {
    if (codepoint <= 0x7F) {
        out.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
}

// Decodes one UTF-8 sequence. Returns the code point and stores its length.
// Malformed bytes decode to U+FFFD with a length of one, so a broken name
// cannot derail the scan.
uint32_t DecodeUtf8(const std::string& text, size_t offset, size_t* length) {
    unsigned char first = static_cast<unsigned char>(text[offset]);
    size_t size;
    uint32_t codepoint;
    if (first < 0x80) {
        codepoint = first;
        size = 1;
    } else if ((first & 0xE0) == 0xC0) {
        codepoint = first & 0x1F;
        size = 2;
    } else if ((first & 0xF0) == 0xE0) {
        codepoint = first & 0x0F;
        size = 3;
    } else if ((first & 0xF8) == 0xF0) {
        codepoint = first & 0x07;
        size = 4;
    } else {
        *length = 1;
        return 0xFFFD;
    }
    if (offset + size > text.size()) {
        *length = 1;
        return 0xFFFD;
    }
    for (size_t i = 1; i < size; i++) {
        unsigned char continuation = static_cast<unsigned char>(text[offset + i]);
        if ((continuation & 0xC0) != 0x80) {
            *length = 1;
            return 0xFFFD;
        }
        codepoint = (codepoint << 6) | (continuation & 0x3F);
    }
    *length = size;
    return codepoint;
}

}  // namespace

std::string FoldText(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    bool need_space = false;

    auto emit = [&out, &need_space](char piece) {
        if (need_space) {
            if (!out.empty()) {
                out.push_back(' ');
            }
            need_space = false;
        }
        out.push_back(piece);
    };
    auto emit_text = [&out, &need_space](const char* piece) {
        if (need_space) {
            if (!out.empty()) {
                out.push_back(' ');
            }
            need_space = false;
        }
        out += piece;
    };
    auto emit_codepoint = [&out, &need_space](uint32_t codepoint) {
        if (need_space) {
            if (!out.empty()) {
                out.push_back(' ');
            }
            need_space = false;
        }
        AppendUtf8(out, codepoint);
    };

    size_t offset = 0;
    while (offset < text.size()) {
        size_t length = 0;
        uint32_t codepoint = DecodeUtf8(text, offset, &length);
        offset += length;

        if (codepoint < 0x80) {
            if (codepoint >= 'A' && codepoint <= 'Z') {
                emit(static_cast<char>(codepoint - 'A' + 'a'));
            } else if ((codepoint >= 'a' && codepoint <= 'z') ||
                       (codepoint >= '0' && codepoint <= '9')) {
                emit(static_cast<char>(codepoint));
            } else {
                need_space = true;
            }
            continue;
        }

        // A combining mark is the accent of a decomposed letter: it is
        // dropped, not turned into a separator.
        if (IsDropped(codepoint)) {
            continue;
        }

        // A letter that folds to more than one character, such as the German
        // sharp s, is looked up before the plain Latin table.
        const Expansion* expansion = FindExpansion(codepoint);
        if (expansion != nullptr) {
            emit_text(expansion->text);
            continue;
        }

        int16_t folded = LatinFoldBase(codepoint);
        if (folded != kNotLatin) {
            if (folded != 0) {
                emit(static_cast<char>(folded));
            } else {
                // Latin but not a letter, such as the multiplication sign.
                need_space = true;
            }
            continue;
        }

        if (IsSeparator(codepoint)) {
            need_space = true;
            continue;
        }

        // Any other letter keeps its own script and is only case folded.
        int32_t delta = FindCaseFold(codepoint);
        emit_codepoint(static_cast<uint32_t>(static_cast<int64_t>(codepoint) + delta));
    }

    return out;
}

}  // namespace media
