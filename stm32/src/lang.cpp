
#include "lang.hpp"
#include <string.h>

using namespace lang;

std::string lang::Utf16ToUtf8(std::basic_string<uint16_t> const &str) {
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

std::string lang::elide(std::string const &str, size_t len, enum ElideMode mode) {
	if(str.size() < len)
		return str;
	switch(mode) {
	case ElideLeft:
		return std::string("...") + str.substr(str.size()-len+3, len-3);
	case ElideRight:
		return str.substr(0, len-3) + std::string("...");
	case ElideMiddle: {
		size_t len_half = (len+1-3)/2;
		return str.substr(0, len_half) + std::string("...") +
		str.substr(str.size() - len + 3 + len_half, len - 3 - len_half);
	}
	default:
		return str;
	}
}

size_t lang::mbstrlen(std::string const & str){
	size_t pos = 0;
	size_t size = str.size();
	mbstate_t ps;
	memset(&ps, 0, sizeof(ps));
	size_t len = 0;
	while(pos < size) {
		ssize_t clen = mbrlen(&str[pos], size-pos, &ps);
		if(clen <= 0)
			break;
		pos += clen;
		len++;
	}
	return len;
}
