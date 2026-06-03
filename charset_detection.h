#ifndef CHARSET_DETECTION_H
#define CHARSET_DETECTION_H

#include <string>
#include <string_view>

std::u16string convert_eucjp_to_utf16(std::string_view const &s);
std::u16string convert_iso2022jp_to_utf16(std::string_view s);
std::string detect_charaset(std::string_view v);

#endif // CHARSET_DETECTION_H
