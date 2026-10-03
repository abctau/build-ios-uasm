#ifndef TUI_XLSX_JSON_H_
#define TUI_XLSX_JSON_H_

// xlsx 专用的最小 JSON 子集 parser/writer。
// 仅支持 xlsx_core 需要的部分：object/array/string/number/bool/null。
// 无异常（-fno-exceptions），失败一律返回 false / nullptr。
// 只被 xlsx_core.cc include（单编译单元），绑定层不直接使用。

#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace tui {

class JsonValue {
 public:
	enum class Type { Null, Bool, Number, String, Array, Object };

	Type type = Type::Null;
	bool boolVal = false;
	double numVal = 0.0;
	std::string strVal;
	std::vector<JsonValue> items;              // Array
	std::vector<std::pair<std::string, JsonValue>> members;  // Object

	bool IsNull() const { return type == Type::Null; }
	bool IsBool() const { return type == Type::Bool; }
	bool IsNumber() const { return type == Type::Number; }
	bool IsString() const { return type == Type::String; }
	bool IsArray() const { return type == Type::Array; }
	bool IsObject() const { return type == Type::Object; }

	const JsonValue* Find(const char* key) const {
		if (type != Type::Object) return nullptr;
		for (const auto& kv : members) {
			if (kv.first == key) return &kv.second;
		}
		return nullptr;
	}
};

namespace json_detail {

struct Parser {
	const char* p;
	const char* end;
	int depth = 0;

	bool SkipWs() {
		while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
		return p < end;
	}

	static void AppendUtf8(std::string& out, uint32_t cp) {
		if (cp < 0x80) {
			out.push_back(static_cast<char>(cp));
		} else if (cp < 0x800) {
			out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
			out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
		} else if (cp < 0x10000) {
			out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
			out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
			out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
		} else {
			out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
			out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
			out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
			out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
		}
	}

	bool ParseHex4(uint32_t& out) {
		if (end - p < 4) return false;
		uint32_t v = 0;
		for (int i = 0; i < 4; i++) {
			const char c = *p++;
			v <<= 4;
			if (c >= '0' && c <= '9') v |= static_cast<uint32_t>(c - '0');
			else if (c >= 'a' && c <= 'f') v |= static_cast<uint32_t>(c - 'a' + 10);
			else if (c >= 'A' && c <= 'F') v |= static_cast<uint32_t>(c - 'A' + 10);
			else return false;
		}
		out = v;
		return true;
	}

	bool ParseString(std::string& out) {
		if (p >= end || *p != '"') return false;
		p++;
		out.clear();
		while (p < end) {
			const unsigned char c = static_cast<unsigned char>(*p);
			if (c == '"') {
				p++;
				return true;
			}
			if (c == '\\') {
				p++;
				if (p >= end) return false;
				switch (*p) {
					case '"': out.push_back('"'); p++; break;
					case '\\': out.push_back('\\'); p++; break;
					case '/': out.push_back('/'); p++; break;
					case 'b': out.push_back('\b'); p++; break;
					case 'f': out.push_back('\f'); p++; break;
					case 'n': out.push_back('\n'); p++; break;
					case 'r': out.push_back('\r'); p++; break;
					case 't': out.push_back('\t'); p++; break;
					case 'u': {
						p++;
						uint32_t cp = 0;
						if (!ParseHex4(cp)) return false;
						if (cp >= 0xD800 && cp <= 0xDBFF && end - p >= 6 &&
						    p[0] == '\\' && p[1] == 'u') {
							p += 2;
							uint32_t lo = 0;
							if (!ParseHex4(lo)) return false;
							if (lo >= 0xDC00 && lo <= 0xDFFF) {
								cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
							} else {
								AppendUtf8(out, 0xFFFD);
								cp = lo;
							}
						} else if (cp >= 0xD800 && cp <= 0xDFFF) {
							cp = 0xFFFD;  // 孤立代理
						}
						AppendUtf8(out, cp);
						break;
					}
					default:
						return false;
				}
			} else if (c < 0x20) {
				return false;  // 未转义控制字符
			} else {
				out.push_back(static_cast<char>(c));
				p++;
			}
		}
		return false;  // 未闭合
	}

	bool ParseNumber(double& out) {
		const char* start = p;
		if (p < end && *p == '-') p++;
		if (p >= end) return false;
		if (*p == '0') {
			p++;
		} else if (*p >= '1' && *p <= '9') {
			while (p < end && *p >= '0' && *p <= '9') p++;
		} else {
			return false;
		}
		if (p < end && *p == '.') {
			p++;
			if (p >= end || *p < '0' || *p > '9') return false;
			while (p < end && *p >= '0' && *p <= '9') p++;
		}
		if (p < end && (*p == 'e' || *p == 'E')) {
			p++;
			if (p < end && (*p == '+' || *p == '-')) p++;
			if (p >= end || *p < '0' || *p > '9') return false;
			while (p < end && *p >= '0' && *p <= '9') p++;
		}
		const std::string text(start, p);
		char* stop = nullptr;
		out = std::strtod(text.c_str(), &stop);
		return stop != nullptr && *stop == '\0' && text.length() > 0;
	}

	bool ParseValue(JsonValue& out) {
		if (++depth > 64) return false;
		if (!SkipWs()) return false;
		const char c = *p;
		bool ok = false;
		if (c == '{') {
			out.type = JsonValue::Type::Object;
			p++;
			if (!SkipWs()) return false;
			if (*p == '}') {
				p++;
				ok = true;
			} else {
				for (;;) {
					if (!SkipWs()) return false;
					std::string key;
					if (!ParseString(key)) return false;
					if (!SkipWs() || *p != ':') return false;
					p++;
					JsonValue val;
					if (!ParseValue(val)) return false;
					out.members.emplace_back(std::move(key), std::move(val));
					if (!SkipWs()) return false;
					if (*p == ',') {
						p++;
					} else if (*p == '}') {
						p++;
						ok = true;
						break;
					} else {
						return false;
					}
				}
			}
		} else if (c == '[') {
			out.type = JsonValue::Type::Array;
			p++;
			if (!SkipWs()) return false;
			if (*p == ']') {
				p++;
				ok = true;
			} else {
				for (;;) {
					JsonValue val;
					if (!ParseValue(val)) return false;
					out.items.push_back(std::move(val));
					if (!SkipWs()) return false;
					if (*p == ',') {
						p++;
					} else if (*p == ']') {
						p++;
						ok = true;
						break;
					} else {
						return false;
					}
				}
			}
		} else if (c == '"') {
			out.type = JsonValue::Type::String;
			ok = ParseString(out.strVal);
		} else if (c == 't') {
			if (end - p >= 4 && p[1] == 'r' && p[2] == 'u' && p[3] == 'e') {
				p += 4;
				out.type = JsonValue::Type::Bool;
				out.boolVal = true;
				ok = true;
			}
		} else if (c == 'f') {
			if (end - p >= 5 && p[1] == 'a' && p[2] == 'l' && p[3] == 's' && p[4] == 'e') {
				p += 5;
				out.type = JsonValue::Type::Bool;
				out.boolVal = false;
				ok = true;
			}
		} else if (c == 'n') {
			if (end - p >= 4 && p[1] == 'u' && p[2] == 'l' && p[3] == 'l') {
				p += 4;
				out.type = JsonValue::Type::Null;
				ok = true;
			}
		} else {
			out.type = JsonValue::Type::Number;
			ok = ParseNumber(out.numVal);
			if (ok && !std::isfinite(out.numVal)) ok = false;
		}
		depth--;
		return ok;
	}
};

void WriteString(std::string& out, const std::string& s) {
	out.push_back('"');
	for (size_t i = 0; i < s.length(); i++) {
		const unsigned char c = static_cast<unsigned char>(s[i]);
		switch (c) {
			case '"': out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\b': out += "\\b"; break;
			case '\f': out += "\\f"; break;
			case '\n': out += "\\n"; break;
			case '\r': out += "\\r"; break;
			case '\t': out += "\\t"; break;
			default:
				if (c < 0x20) {
					char buf[8];
					std::snprintf(buf, sizeof(buf), "\\u%04x", c);
					out += buf;
				} else {
					out.push_back(static_cast<char>(c));
				}
		}
	}
	out.push_back('"');
}

// 跨平台 double 最短往返格式化（iOS 15 的 libc++ 无浮点 to_chars，iOS 16.3+ 才有）
// prec 1..17 依次尝试 %.*g，第一个能 strtod 精确往返的即最短：
// 整数值输出无小数点（45123 → "45123"），小数最短（3.14 → "3.14"）
inline int FormatDoubleShortest(char* buf, size_t size, double v) {
	for (int prec = 1; prec <= 17; ++prec) {
		const int n = snprintf(buf, size, "%.*g", prec, v);
		if (n <= 0 || static_cast<size_t>(n) >= size) break;
		if (strtod(buf, nullptr) == v) return n;
	}
	return snprintf(buf, size, "%.17g", v);
}

void WriteNumber(std::string& out, double v) {
	char buf[40];
	const int n = FormatDoubleShortest(buf, sizeof(buf), v);
	if (n <= 0) {
		out += "0";
		return;
	}
	out.append(buf, static_cast<size_t>(n));
}

}  // namespace json_detail

inline bool JsonParse(const std::string& text, JsonValue& out) {
	json_detail::Parser parser{text.data(), text.data() + text.size()};
	if (!parser.ParseValue(out)) return false;
	return parser.SkipWs() ? false : true;  // 尾部只允许空白
}

inline void JsonWriteString(std::string& out, const std::string& s) {
	json_detail::WriteString(out, s);
}

inline void JsonWriteNumber(std::string& out, double v) {
	json_detail::WriteNumber(out, v);
}

}  // namespace tui

#endif  // TUI_XLSX_JSON_H_
