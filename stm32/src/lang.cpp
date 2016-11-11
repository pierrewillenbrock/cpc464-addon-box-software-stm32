
#include "lang.hpp"

std::string Utf16ToUtf8(std::basic_string<uint16_t> const &str) {
	std::string out;
	for(auto it = str.begin(); it != str.end(); it++) {
		uint32_t codepoint = *it;
		if (codepoint >= 0xd800 && codepoint < 0xe000) {
			it++;
			codepoint = (((codepoint & 0x3ff) << 10) |
				(*it & 0x3ff)) + 0x10000;
		}
		if (codepoint < 0x80) {
			out += (char)codepoint;
		} else if (codepoint < 0x800) {
			out += (char)(0xc0 | ((codepoint & 0x7c0) >> 6));
			out += (char)(0x80 | ((codepoint & 0x03f) >> 0));
		} else if (codepoint < 0x10000) {
			out += (char)(0xe0 | ((codepoint & 0xf000) >> 12));
			out += (char)(0x80 | ((codepoint & 0x0fc0) >> 6));
			out += (char)(0x80 | ((codepoint & 0x003f) >> 0));
		} else if (codepoint < 0x200000) {
			out += (char)(0xf0 | ((codepoint & 0x1c0000) >> 18));
			out += (char)(0x80 | ((codepoint & 0x03f000) >> 12));
			out += (char)(0x80 | ((codepoint & 0x000fc0) >> 6));
			out += (char)(0x80 | ((codepoint & 0x00003f) >> 0));
		}
	}
	return out;
}

