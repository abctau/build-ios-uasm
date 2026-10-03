#include "xlsx_core.h"

#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

#include "xlsx_json.h"
#include "xlsx_xml.h"
#include "xlsx_zip.h"

namespace {

using tui::JsonValue;

constexpr size_t kMaxSheetCount = 1024;
constexpr size_t kMaxRowsRead = 100000;
constexpr size_t kMaxColsRead = 16384;
constexpr size_t kMaxCellsRead = 1000000;

// 0-based 列号 → "A".."Z","AA"..（0-based 行 + 1 拼接为单元格引用）
void ColToRef(std::string& out, size_t col) {
	char buf[8];
	size_t n = 0;
	size_t c = col + 1;
	while (c > 0) {
		buf[n++] = static_cast<char>('A' + ((c - 1) % 26));
		c = (c - 1) / 26;
	}
	for (size_t i = 0; i < n; i++) {
		out.push_back(buf[n - 1 - i]);
	}
}

void AppendNumber(std::string& out, double v) {
	char buf[40];
	const int n = tui::json_detail::FormatDoubleShortest(buf, sizeof(buf), v);
	if (n <= 0) {
		out += "0";
		return;
	}
	out.append(buf, static_cast<size_t>(n));
}

std::string XmlDecl() {
	return "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>";
}

bool BuildSheetXml(const JsonValue& rows, std::string& out) {
	out = XmlDecl();
	out +=
	    "<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/"
	    "main\"><sheetData>";
	if (rows.IsArray()) {
		for (size_t r = 0; r < rows.items.size(); r++) {
			const JsonValue& row = rows.items[r];
			if (!row.IsArray()) return false;
			bool hasCell = false;
			std::string rowXml = "<row r=\"";
			AppendNumber(rowXml, static_cast<double>(r + 1));
			rowXml += "\">";
			for (size_t c = 0; c < row.items.size(); c++) {
				const JsonValue& cell = row.items[c];
				std::string ref;
				ColToRef(ref, c);
				ref += std::to_string(r + 1);
				switch (cell.type) {
					case JsonValue::Type::Null:
						continue;
					case JsonValue::Type::String: {
						rowXml += "<c r=\"" + ref + "\" t=\"inlineStr\"><is><t>";
						tui::XmlEscapeText(rowXml, cell.strVal);
						rowXml += "</t></is></c>";
						break;
					}
					case JsonValue::Type::Number: {
						rowXml += "<c r=\"" + ref + "\"><v>";
						AppendNumber(rowXml, cell.numVal);
						rowXml += "</v></c>";
						break;
					}
					case JsonValue::Type::Bool: {
						rowXml += "<c r=\"" + ref + "\" t=\"b\"><v>";
						rowXml += cell.boolVal ? "1" : "0";
						rowXml += "</v></c>";
						break;
					}
					default:
						return false;  // 数组/对象单元格非法
				}
				hasCell = true;
			}
			if (hasCell) {
				rowXml += "</row>";
				out += rowXml;
			}
		}
	} else {
		return false;
	}
	out += "</sheetData></worksheet>";
	return true;
}

std::string BuildContentTypes(size_t sheetCount) {
	std::string out = XmlDecl();
	out +=
	    "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/"
	    "content-types\">"
	    "<Default Extension=\"rels\" "
	    "ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
	    "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
	    "<Override PartName=\"/xl/workbook.xml\" "
	    "ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml."
	    "sheet.main+xml\"/>";
	for (size_t i = 0; i < sheetCount; i++) {
		out += "<Override PartName=\"/xl/worksheets/sheet" + std::to_string(i + 1) +
		       ".xml\" ContentType=\"application/vnd.openxmlformats-officedocument."
		       "spreadsheetml.worksheet+xml\"/>";
	}
	out +=
	    "<Override PartName=\"/xl/styles.xml\" "
	    "ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml."
	    "styles+xml\"/></Types>";
	return out;
}

std::string BuildRootRels() {
	return XmlDecl() +
	       "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/"
	       "relationships\"><Relationship Id=\"rId1\" Type=\"http://schemas."
	       "openxmlformats.org/officeDocument/2006/relationships/officeDocument\" "
	       "Target=\"xl/workbook.xml\"/></Relationships>";
}

std::string BuildWorkbook(const std::vector<std::string>& names) {
	std::string out = XmlDecl();
	out +=
	    "<workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/"
	    "main\" xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/"
	    "relationships\"><sheets>";
	for (size_t i = 0; i < names.size(); i++) {
		out += "<sheet name=\"";
		tui::XmlEscapeAttr(out, names[i]);
		out += "\" sheetId=\"" + std::to_string(i + 1) + "\" r:id=\"rId" +
		       std::to_string(i + 1) + "\"/>";
	}
	out += "</sheets></workbook>";
	return out;
}

std::string BuildWorkbookRels(size_t sheetCount) {
	std::string out = XmlDecl();
	out +=
	    "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/"
	    "relationships\">";
	for (size_t i = 0; i < sheetCount; i++) {
		out +=
		    "<Relationship Id=\"rId" + std::to_string(i + 1) + "\" "
		    "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/"
		    "relationships/worksheet\" Target=\"worksheets/sheet" +
		    std::to_string(i + 1) + ".xml\"/>";
	}
	out += "</Relationships>";
	return out;
}

std::string BuildStyles() {
	// 最小可通过 Excel/WPS 校验的样式表
	return XmlDecl() +
	       "<styleSheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/"
	       "2006/main\"><fonts count=\"2\"><font><sz val=\"11\"/><name "
	       "val=\"Calibri\"/></font><font><b/><sz val=\"11\"/><name "
	       "val=\"Calibri\"/></font></fonts><fills count=\"2\"><fill><patternFill "
	       "patternType=\"none\"/></fill><fill><patternFill "
	       "patternType=\"gray125\"/></fill></fills><borders count=\"1\"><border/"
	       "></borders><cellStyleXfs count=\"1\"><xf "
	       "numFmtId=\"0\" fontId=\"0\" fillId=\"0\" "
	       "borderId=\"0\"/></cellStyleXfs><cellXfs count=\"1\"><xf "
	       "numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\" "
	       "xfId=\"0\"/></cellXfs></styleSheet>";
}

// ---- 读取辅助 ----

std::string LocalName(const std::string& raw) {
	const size_t pos = raw.rfind(':');
	return pos == std::string::npos ? raw : raw.substr(pos + 1);
}

std::string AttrValue(
    const std::vector<std::pair<std::string, std::string>>& attrs,
    const char* localKey) {
	for (const auto& kv : attrs) {
		if (LocalName(kv.first) == localKey) return kv.second;
	}
	return std::string();
}

// 解析 "A1" → (row 1-based, col 0-based)；失败返回 false
bool ParseCellRef(const std::string& ref, size_t& row, size_t& col) {
	size_t i = 0;
	size_t c = 0;
	while (i < ref.length() && ref[i] >= 'A' && ref[i] <= 'Z') {
		c = c * 26 + static_cast<size_t>(ref[i] - 'A' + 1);
		i++;
	}
	if (i == 0 || c == 0) return false;
	col = c - 1;
	if (i >= ref.length()) return false;
	char* stop = nullptr;
	const unsigned long r = std::strtoul(ref.c_str() + i, &stop, 10);
	if (stop == nullptr || *stop != '\0' || r == 0) return false;
	row = static_cast<size_t>(r);
	return true;
}

struct SheetRef {
	std::string name;
	std::string rid;
};

}  // namespace

namespace tui {

bool WriteXlsx(const std::string& sheetsJson, std::vector<uint8_t>& out) {
	JsonValue root;
	if (!JsonParse(sheetsJson, root) || !root.IsArray() || root.items.empty()) {
		return false;
	}
	if (root.items.size() > kMaxSheetCount) return false;

	std::vector<std::string> names;
	std::vector<std::string> sheetXmls;
	for (const JsonValue& sheet : root.items) {
		if (!sheet.IsObject()) return false;
		const JsonValue* nameVal = sheet.Find("name");
		if (nameVal == nullptr || !nameVal->IsString()) return false;
		for (const std::string& prev : names) {
			if (prev == nameVal->strVal) return false;  // 重名拒绝
		}
		names.push_back(nameVal->strVal);
		const JsonValue* rows = sheet.Find("rows");
		if (rows == nullptr || !rows->IsArray()) return false;
		std::string xml;
		if (!BuildSheetXml(*rows, xml)) return false;
		sheetXmls.push_back(std::move(xml));
	}

	std::vector<std::string> partNames;
	std::vector<std::vector<uint8_t>> partData;
	auto addPart = [&](const std::string& name, const std::string& content) {
		partNames.push_back(name);
		partData.emplace_back(content.begin(), content.end());
	};
	addPart("[Content_Types].xml", BuildContentTypes(sheetXmls.size()));
	addPart("_rels/.rels", BuildRootRels());
	addPart("xl/workbook.xml", BuildWorkbook(names));
	addPart("xl/_rels/workbook.xml.rels", BuildWorkbookRels(sheetXmls.size()));
	addPart("xl/styles.xml", BuildStyles());
	for (size_t i = 0; i < sheetXmls.size(); i++) {
		addPart("xl/worksheets/sheet" + std::to_string(i + 1) + ".xml",
		        sheetXmls[i]);
	}
	return ZipCreate(partNames, partData, 6, out);
}

bool ReadXlsx(const uint8_t* data, size_t size, std::string& out,
              size_t maxUncompressed) {
	if (data == nullptr || size == 0) return false;

	std::vector<std::string> entries;
	if (!ZipList(data, size, entries)) return false;

	auto hasEntry = [&](const char* name) {
		for (const std::string& n : entries) {
			if (n == name) return true;
		}
		return false;
	};

	// 1. workbook.xml：sheet 顺序 + r:id
	std::vector<uint8_t> wbXml;
	if (!hasEntry("xl/workbook.xml")) return false;
	if (!ZipReadEntry(data, size, "xl/workbook.xml", wbXml, maxUncompressed)) {
		return false;
	}

	std::vector<SheetRef> sheetRefs;
	{
		tui::XmlTokenizer tok(
		    reinterpret_cast<const char*>(wbXml.data()), wbXml.size());
		XmlEvent ev = XmlEvent::Eof;
		std::string name;
		std::string text;
		std::vector<std::pair<std::string, std::string>> attrs;
		bool selfClosing = false;
		while (tok.Next(ev, name, attrs, text, selfClosing) && ev != XmlEvent::Eof) {
			if (ev == XmlEvent::StartTag && LocalName(name) == "sheet") {
				SheetRef ref;
				ref.name = AttrValue(attrs, "name");
				ref.rid = AttrValue(attrs, "id");  // r:id
				sheetRefs.push_back(std::move(ref));
			}
		}
	}
	if (sheetRefs.empty()) return false;

	// 2. rels：rId → target
	std::vector<uint8_t> relsXml;
	std::vector<std::pair<std::string, std::string>> rels;  // rid → target
	if (hasEntry("xl/_rels/workbook.xml.rels") &&
	    ZipReadEntry(data, size, "xl/_rels/workbook.xml.rels", relsXml,
	                 maxUncompressed)) {
		tui::XmlTokenizer tok(reinterpret_cast<const char*>(relsXml.data()),
		                      relsXml.size());
		XmlEvent ev = XmlEvent::Eof;
		std::string name;
		std::string text;
		std::vector<std::pair<std::string, std::string>> attrs;
		bool selfClosing = false;
		while (tok.Next(ev, name, attrs, text, selfClosing) &&
		       ev != XmlEvent::Eof) {
			if (ev == XmlEvent::StartTag && LocalName(name) == "relationship") {
				rels.emplace_back(AttrValue(attrs, "id"), AttrValue(attrs, "target"));
			}
		}
	}

	// 3. sharedStrings（可选）
	std::vector<std::string> shared;
	if (hasEntry("xl/sharedStrings.xml")) {
		std::vector<uint8_t> ssXml;
		if (!ZipReadEntry(data, size, "xl/sharedStrings.xml", ssXml,
		                  maxUncompressed)) {
			return false;
		}
		tui::XmlTokenizer tok(reinterpret_cast<const char*>(ssXml.data()),
		                      ssXml.size());
		XmlEvent ev = XmlEvent::Eof;
		std::string name;
		std::string text;
		std::vector<std::pair<std::string, std::string>> attrs;
		bool selfClosing = false;
		bool inSi = false;
		bool skipText = false;  // rPh 等注音段
		int skipDepth = 0;
		std::string current;
		while (tok.Next(ev, name, attrs, text, selfClosing) && ev != XmlEvent::Eof) {
			const std::string local = LocalName(name);
			if (ev == XmlEvent::StartTag) {
				if (local == "si") {
					inSi = true;
					current.clear();
				} else if (inSi && (local == "rph" || local == "phoneticPr")) {
					skipText = true;
					skipDepth = 1;
				} else if (skipText && selfClosing) {
					// 自闭合不增加深度
				} else if (skipText) {
					skipDepth++;
				}
			} else if (ev == XmlEvent::EndTag) {
				if (skipText) {
					skipDepth--;
					if (skipDepth == 0) skipText = false;
				} else if (local == "si") {
					inSi = false;
					shared.push_back(current);
					current.clear();
				} else if (inSi && local == "t") {
					current += text;
				}
			}
		}
	}

	// 4. 输出 JSON
	out = "{\"sheets\":[";
	for (size_t si = 0; si < sheetRefs.size(); si++) {
		if (si > 0) out += ",";
		out += "{\"name\":";
		JsonWriteString(out, sheetRefs[si].name.empty()
		                         ? ("Sheet" + std::to_string(si + 1))
		                         : sheetRefs[si].name);
		out += ",\"rows\":[";

		// 目标路径：rid → target（相对 xl/）
		std::string target;
		for (const auto& kv : rels) {
			if (kv.first == sheetRefs[si].rid) {
				target = kv.second;
				break;
			}
		}
		std::string entryPath;
		if (!target.empty()) {
			if (!target.empty() && target[0] == '/') {
				entryPath = target.substr(1);  // 包内绝对路径
			} else {
				entryPath = "xl/" + target;
			}
		} else {
			entryPath = "xl/worksheets/sheet" + std::to_string(si + 1) + ".xml";
		}

		bool exists = false;
		for (const std::string& n : entries) {
			if (n == entryPath) {
				exists = true;
				break;
			}
		}
		if (!exists) {
			out += "]}";
			continue;  // 缺失 sheet → 空 rows
		}

		std::vector<uint8_t> sheetXml;
		if (!ZipReadEntry(data, size, entryPath, sheetXml, maxUncompressed)) {
			return false;
		}

		// 行网格：稀疏 → 紧凑
		struct RawCell {
			size_t row = 0;  // 1-based
			size_t col = 0;  // 0-based
			int kind = 0;    // 0=null 1=string 2=number 3=bool
			std::string text;
			double num = 0.0;
			bool b = false;
		};
		std::vector<RawCell> cells;
		size_t maxRow = 0;
		size_t maxCol = 0;
		{
			tui::XmlTokenizer tok(reinterpret_cast<const char*>(sheetXml.data()),
			                      sheetXml.size());
			XmlEvent ev = XmlEvent::Eof;
			std::string name;
			std::string text;
			std::vector<std::pair<std::string, std::string>> attrs;
			bool selfClosing = false;
			bool inCell = false;
			bool inInlineStr = false;
			size_t curRow = 0;
			size_t curCol = 0;
			std::string curT;
			bool isError = false;
			RawCell cur;
			std::string vText;
			bool hasV = false;
			auto finishCell = [&]() {
				if (cells.size() >= kMaxCellsRead) return false;
				if (!isError) {
					if (cur.kind == 0) {
						// 无显式类型：按 v 内容推断
						if (hasV) {
							char* stop = nullptr;
							const double d = std::strtod(vText.c_str(), &stop);
							if (stop != nullptr && *stop == '\0' && !vText.empty()) {
								cur.kind = 2;
								cur.num = d;
							} else {
								cur.kind = 1;
								cur.text = vText;
							}
						}
					} else if (cur.kind == 1 && cur.text.empty() && hasV) {
						cur.text = vText;
					}
				}
				if (cur.kind == 0) return true;  // 空单元格跳过
				if (cur.row > kMaxRowsRead || cur.col >= kMaxColsRead) return false;
				if (cur.row > maxRow) maxRow = cur.row;
				if (cur.col > maxCol) maxCol = cur.col;
				cells.push_back(cur);
				return true;
			};
			while (tok.Next(ev, name, attrs, text, selfClosing) &&
			       ev != XmlEvent::Eof) {
				const std::string local = LocalName(name);
				if (ev == XmlEvent::StartTag) {
					if (local == "row") {
						const std::string rAttr = AttrValue(attrs, "r");
						char* stop = nullptr;
						const unsigned long r =
						    rAttr.empty() ? 0 : std::strtoul(rAttr.c_str(), &stop, 10);
						curRow = (stop != nullptr && *stop == '\0' && r > 0)
						             ? static_cast<size_t>(r)
						             : curRow + 1;
						curCol = 0;
					} else if (local == "c") {
						inCell = true;
						cur = RawCell();
						isError = false;
						hasV = false;
						vText.clear();
						curT = AttrValue(attrs, "t");
						const std::string ref = AttrValue(attrs, "r");
						size_t row = 0;
						size_t col = 0;
						if (ParseCellRef(ref, row, col)) {
							cur.row = row;
							cur.col = col;
						} else {
							cur.row = curRow;  // 容错：沿用当前行
							cur.col = curCol;
						}
						if (curT == "s" || curT == "str") {
							cur.kind = 1;  // 先按字符串占位，读完 v 再分类
						} else if (curT == "b") {
							cur.kind = 3;
						} else if (curT == "e") {
							cur.kind = 0;
							isError = true;
						} else if (curT == "inlineStr") {
							cur.kind = 1;
							inInlineStr = true;
						}
					} else if (inCell && local == "v") {
						hasV = true;
						vText.clear();
					}
				} else if (ev == XmlEvent::Text) {
					if (inCell && hasV) {
						vText += text;
					} else if (inCell && inInlineStr) {
						cur.text += text;
					}
				} else if (ev == XmlEvent::EndTag) {
					if (local == "is") {
						inInlineStr = false;
					} else if (local == "c") {
						inCell = false;
						// t="s"：v 是共享字符串索引
						if (curT == "s" && hasV) {
							char* stop = nullptr;
							const unsigned long idx =
							    std::strtoul(vText.c_str(), &stop, 10);
							if (stop != nullptr && *stop == '\0' && idx < shared.size()) {
								cur.text = shared[idx];
							} else {
								isError = true;  // 索引越界 → 丢弃
							}
						} else if (curT == "b" && hasV) {
							cur.b = (vText == "1" || vText == "true");
						}
						if (!finishCell()) return false;
						curCol = cur.col + 1;
					}
				}
			}
		}

		// 组装网格：kind 值矩阵 + 稀疏值索引
		if (maxRow > 0 && !cells.empty()) {
			const size_t cols = maxCol + 1;
			std::vector<int> grid(maxRow * cols, 0);
			std::vector<const RawCell*> cellIndex(maxRow * cols, nullptr);
			for (const RawCell& c : cells) {
				if (c.row >= 1 && c.row <= maxRow && c.col < cols) {
					grid[(c.row - 1) * cols + c.col] = c.kind;
					cellIndex[(c.row - 1) * cols + c.col] = &c;
				}
			}
			for (size_t r = 0; r < maxRow; r++) {
				if (r > 0) out += ",";
				out += "[";
				for (size_t c = 0; c < cols; c++) {
					if (c > 0) out += ",";
					const RawCell* cell = cellIndex[r * cols + c];
					if (cell == nullptr || cell->kind == 0) {
						out += "null";
						continue;
					}
					switch (cell->kind) {
						case 1:
							JsonWriteString(out, cell->text);
							break;
						case 2:
							if (std::isfinite(cell->num)) {
								JsonWriteNumber(out, cell->num);
							} else {
								out += "null";
							}
							break;
						case 3:
							out += cell->b ? "true" : "false";
							break;
						default:
							out += "null";
					}
				}
				out += "]";
			}
		}
		out += "]}";
	}
	out += "]}";
	return true;
}

}  // namespace tui
