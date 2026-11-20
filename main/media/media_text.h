#ifndef MEDIA_TEXT_H_
#define MEDIA_TEXT_H_

#include <cstdint>
#include <string>

namespace media {

// A code point that folds to more than one ASCII character, such as the
// German sharp s, which becomes "ss".
struct Expansion {
    uint32_t codepoint;
    const char* text;
};

// A range of code points that all fold by the same amount.
struct FoldRange {
    uint32_t start;
    uint32_t end;
    int32_t delta;
};

// A range of code points sharing a property.
struct Range {
    uint32_t start;
    uint32_t end;
};

// Table lookups, implemented in the generated unicode_tables.cc.
constexpr int16_t kNotLatin = -1;

// ASCII base letter of a Latin code point.
//   kNotLatin: the code point is not Latin, the general path applies.
//   0:         it is Latin but not a letter, so it separates words.
//   otherwise: the ASCII letter it folds to.
int16_t LatinFoldBase(uint32_t codepoint);
const Expansion* FindExpansion(uint32_t codepoint);
int32_t FindCaseFold(uint32_t codepoint);
bool IsSeparator(uint32_t codepoint);
bool IsDropped(uint32_t codepoint);

// Folds text into a form that can be compared with a plain substring search:
//
//   - Latin letters with diacritics become their ASCII base, so "Cancion"
//     and "Cancion" compare equal and a Spanish, French, German, Polish
//     or Vietnamese title is found whatever the accents.
//   - Letters of every other script are kept and only case folded, so a
//     Greek, Cyrillic, Hebrew, Arabic or CJK title stays reachable instead
//     of collapsing to an empty string.
//   - Spaces, punctuation and symbols become a single separator.
//   - Combining marks are dropped, so a letter written with an accent and
//     the same letter written as a base letter plus a combining mark
//     compare equal.
//
// Invalid UTF-8 bytes are treated as separators rather than propagated.
std::string FoldText(const std::string& text);

}  // namespace media

#endif  // MEDIA_TEXT_H_
