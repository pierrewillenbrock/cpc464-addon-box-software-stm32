
#pragma once

#include <string>

namespace lang {

std::string Utf16ToUtf8(std::basic_string<uint16_t> const &str);

enum ElideMode {
	ElideMiddle,
	ElideLeft,
	ElideRight
};

std::string elide(std::string const &str, size_t len, enum ElideMode mode = ElideMiddle);

}
