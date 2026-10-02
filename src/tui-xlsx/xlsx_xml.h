#ifndef TUI_XLSX_XML_H_
#define TUI_XLSX_XML_H_

// xlsx 专用的最小 XML tokenizer（容错子集）。
// 事件：StartTag(name, attrs) / EndTag(name) / Text(content)。
// 支持：属性单双引号、自闭合、实体解码（&amp; &lt; &gt; &quot; &apos;
// &#NN; &#xHH;）、跳过 <?..?>、<!--..-->、<![CDATA[..]]>。
// UTF-8 内容透传。无异常，失败返回 false。

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace tui {

enum class XmlEvent { StartTag, EndTag, Text, Eof };

class XmlTokenizer {
 public:
	XmlTokenizer(const char* data, size_t size)
	    : p_(data), end_(data + size) {}

	// attrs 内的值已做实体解码；tag 名与属性名不含命名空间前缀拆分。
	bool Next(XmlEvent& event, std::string& name,
	          std::vector<std::pair<std::string, std::string>>& attrs,
	          std::string& text, bool& selfClosing) {
		if (p_ >= end_) {
			event = XmlEvent::Eof;
			return true;
		}
		if (*p_ != '<') {
			// 文本节点，直到下一个 '<'
			text.clear();
			while (p_ < end_ && *p_ != '<') {
				if (*p_ == '&') {
					if (!DecodeEntity(text)) return false;
				} else {
					text.push_back(*p_);
					p_++;
				}
			}
			event = XmlEvent::Text;
			return true;
		}
		// 标签或特殊段
		p_++;
		if (p_ >= end_) return false;
		if (*p_ == '?') {  // <?xml .. ?>
			while (p_ < end_ - 1 && !(p_[0] == '?' && p_[1] == '>')) p_++;
			if (p_ >= end_ - 1) return false;
			p_ += 2;
			return Next(event, name, attrs, text, selfClosing);
		}
		if (p_ + 3 <= end_ && p_[0] == '!' && p_[1] == '-' && p_[2] == '-') {  // 注释
			p_ += 3;
			while (p_ < end_ - 2 && !(p_[0] == '-' && p_[1] == '-' && p_[2] == '>')) p_++;
			if (p_ >= end_ - 2) return false;
			p_ += 3;
			return Next(event, name, attrs, text, selfClosing);
		}
		if (p_ + 8 <= end_ && p_[0] == '!' && p_[1] == '[' &&
		    std::memcmp(p_ + 2, "CDATA[", 6) == 0) {  // CDATA 作为文本
			p_ += 8;
			const char* start = p_;
			while (p_ < end_ - 2 && !(p_[0] == ']' && p_[1] == ']' && p_[2] == '>')) p_++;
			if (p_ >= end_ - 2) return false;
			text = std::string(start, p_);
			p_ += 3;
			event = XmlEvent::Text;
			return true;
		}
		// 普通标签
		const bool isEnd = (*p_ == '/');
		if (isEnd) p_++;
		name.clear();
		while (p_ < end_ && *p_ != ' ' && *p_ != '\t' && *p_ != '\r' && *p_ != '\n' &&
		       *p_ != '>' && *p_ != '/') {
			name.push_back(*p_);
			p_++;
		}
		if (name.empty()) return false;
		selfClosing = false;
		attrs.clear();
		if (!isEnd) {
			for (;;) {
				if (p_ >= end_) return false;
				if (*p_ == '/') {
					selfClosing = true;
					p_++;
					break;
				}
				if (*p_ == '>') {
					p_++;
					break;
				}
				if (*p_ != ' ' && *p_ != '\t' && *p_ != '\r' && *p_ != '\n') return false;
				p_++;
				// 属性名
				std::string attrName;
				while (p_ < end_ && *p_ != '=' && *p_ != ' ' && *p_ != '\t' &&
				       *p_ != '\r' && *p_ != '\n' && *p_ != '>' && *p_ != '/') {
					attrName.push_back(*p_);
					p_++;
				}
				if (attrName.empty()) {
					continue;  // 容错：多余空白
				}
				while (p_ < end_ && (*p_ == ' ' || *p_ == '\t')) p_++;
				if (p_ >= end_ || *p_ != '=') return false;
				p_++;
				while (p_ < end_ && (*p_ == ' ' || *p_ == '\t')) p_++;
				if (p_ >= end_ || (*p_ != '"' && *p_ != '\'')) return false;
				const char quote = *p_++;
				std::string attrVal;
				while (p_ < end_ && *p_ != quote) {
					if (*p_ == '&') {
						DecodeEntity(attrVal);
					} else {
						attrVal.push_back(*p_);
						p_++;
					}
				}
				if (p_ >= end_) return false;
				p_++;
				attrs.emplace_back(std::move(attrName), std::move(attrVal));
			}
		} else {
			while (p_ < end_ && *p_ != '>') p_++;
			if (p_ >= end_) return false;
			p_++;
		}
		event = isEnd ? XmlEvent::EndTag : XmlEvent::StartTag;
		return true;
	}

	// 便捷重载：不需要 attrs/selfClosing 的场景
	bool Next(XmlEvent& event, std::string& name, std::string& text) {
		std::vector<std::pair<std::string, std::string>> attrs;
		bool selfClosing = false;
		return Next(event, name, attrs, text, selfClosing);
	}

 private:
	bool DecodeEntity(std::string& out) {
		p_++;  // 跳过 '&'
		if (p_ >= end_) return false;
		if (*p_ == '#') {
			p_++;
			bool hex = false;
			if (p_ < end_ && (*p_ == 'x' || *p_ == 'X')) {
				hex = true;
				p_++;
			}
			uint32_t cp = 0;
			bool any = false;
			while (p_ < end_ && *p_ != ';') {
				const char c = *p_;
				uint32_t d = 0;
				if (c >= '0' && c <= '9') d = static_cast<uint32_t>(c - '0');
				else if (hex && c >= 'a' && c <= 'f') d = static_cast<uint32_t>(c - 'a' + 10);
				else if (hex && c >= 'A' && c <= 'F') d = static_cast<uint32_t>(c - 'A' + 10);
				else return false;
				cp = cp * (hex ? 16u : 10u) + d;
				if (cp > 0x10FFFF) return false;
				any = true;
				p_++;
			}
			if (!any || p_ >= end_) return false;
			p_++;  // ';'
			if (cp == 0 || (cp < 0x20 && cp != 0x9 && cp != 0xA && cp != 0xD)) {
				cp = 0xFFFD;
			}
			AppendUtf8(out, cp);
			return true;
		}
		struct Named {
			const char* name;
			char value;
		};
		static const Named kNamed[] = {
		    {"amp;", '&'}, {"lt;", '<'}, {"gt;", '>'},
		    {"quot;", '"'}, {"apos;", '\''},
		};
		for (const auto& n : kNamed) {
			const size_t len = std::strlen(n.name);
			if (end_ - p_ >= static_cast<long>(len) &&
			    std::memcmp(p_, n.name, len) == 0) {
				out.push_back(n.value);
				p_ += len;
				return true;
			}
		}
		return false;
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

	const char* p_;
	const char* end_;
};

inline void XmlEscapeText(std::string& out, const std::string& s) {
	for (const char ch : s) {
		const unsigned char c = static_cast<unsigned char>(ch);
		switch (c) {
			case '&': out += "&amp;"; break;
			case '<': out += "&lt;"; break;
			case '>': out += "&gt;"; break;
			default:
				if (c < 0x20 && c != 0x9 && c != 0xA && c != 0xD) {
					// 非法 XML 控制字符：丢弃
				} else {
					out.push_back(ch);
				}
		}
	}
}

inline void XmlEscapeAttr(std::string& out, const std::string& s) {
	for (const char ch : s) {
		const unsigned char c = static_cast<unsigned char>(ch);
		switch (c) {
			case '&': out += "&amp;"; break;
			case '<': out += "&lt;"; break;
			case '>': out += "&gt;"; break;
			case '"': out += "&quot;"; break;
			case '\'': out += "&apos;"; break;
			default:
				if (c < 0x20 && c != 0x9 && c != 0xA && c != 0xD) {
				} else {
					out.push_back(ch);
				}
		}
	}
}

}  // namespace tui

#endif  // TUI_XLSX_XML_H_
