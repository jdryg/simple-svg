#include <ssvg/ssvg.h>
#include <bx/bx.h>
#include <bx/string.h>
#include <bx/math.h>
#include <float.h> // FLT_MAX

BX_PRAGMA_DIAGNOSTIC_IGNORED_MSVC(4127) // conditional expression is constant

namespace ssvg
{
extern bx::AllocatorI* s_Allocator;

struct ParseAttr
{
	enum Result : uint32_t
	{
		OK = 0,
		Fail = 1,
		Unknown = 2
	};
};

struct ParserState
{
	const char* m_XMLString;
	const char* m_Ptr;
	uint32_t m_Flags;
};

struct CSSColor
{
	bx::StringView m_Name;
	uint32_t m_ABGR;
};

static const CSSColor kCSSColors[] = {
	{ "black",            0xFF000000 }, { "silver",               0xFFC0C0C0 }, { "gray",              0xFF808080 }, { "white",           0xFFFFFFFF }, 
	{ "maroon",           0xFF000080 }, { "red",                  0xFF0000FF }, { "purple",            0xFF800080 }, { "fuchsia",         0xFFFF00FF }, 
	{ "green",            0xFF008000 }, { "lime",                 0xFF00FF00 }, { "olive",             0xFF008080 }, { "yellow",          0xFF00FFFF }, 
	{ "navy",             0xFF800000 }, { "blue",                 0xFFFF0000 }, { "teal",              0xFF808000 }, { "aqua",            0xFFFFFF00 }, 
	{ "orange",           0xFF00A5FF }, { "aliceblue",            0xFFFFF8F0 }, { "antiquewhite",      0xFFD7EBFA }, { "aquamarine",      0xFFD4FF7F }, 
	{ "azure",            0xFFFFFFF0 }, { "beige",                0xFFDCF5F5 }, { "bisque",            0xFFC4E4FF }, { "blanchedalmond",  0xFFCDEBFF }, 
	{ "blueviolet",       0xFFE22B8A }, { "brown",                0xFF2A2AA5 }, { "burlywood",         0xFF87B8DE }, { "cadetblue",       0xFFA09E5F }, 
	{ "chartreuse",       0xFF00FF7F }, { "chocolate",            0xFF1E69D2 }, { "coral",             0xFF507FFF }, { "cornflowerblue",  0xFFED9564 }, 
	{ "cornsilk",         0xFFDCF8FF }, { "crimson",              0xFF3C14DC }, { "cyan",              0xFFFFFF00 }, { "darkblue",        0xFF8B0000 }, 
	{ "darkcyan",         0xFF8B8B00 }, { "darkgoldenrod",        0xFF0B86B8 }, { "darkgray",          0xFFA9A9A9 }, { "darkgreen",       0xFF006400 },
	{ "darkgrey",         0xFFA9A9A9 }, { "darkkhaki",            0xFF6BB7BD }, { "darkmagenta",       0xFF8B008B }, { "darkolivegreen",  0xFF2F6B55 }, 
	{ "darkorange",       0xFF008CFF }, { "darkorchid",           0xFFCC3299 }, { "darkred",           0xFF00008B }, { "darksalmon",      0xFF7A96E9 }, 
	{ "darkseagreen",     0xFF8FBC8F }, { "darkslateblue",        0xFF8B3D48 }, { "darkslategray",     0xFF4F4F2F }, { "darkslategrey",   0xFF4F4F2F }, 
	{ "darkturquoise",    0xFFD1CE00 }, { "darkviolet",           0xFFD30094 }, { "deeppink",          0xFF9314FF }, { "deepskyblue",     0xFFFFBF00 }, 
	{ "dimgray",          0xFF696969 }, { "dimgrey",              0xFF696969 }, { "dodgerblue",        0xFFFF901E }, { "firebrick",       0xFF2222B2 },
	{ "floralwhite",      0xFFF0FAFF }, { "forestgreen",          0xFF228B22 }, { "gainsboro",         0xFFDCDCDC }, { "ghostwhite",      0xFFFFF8F8 }, 
	{ "gold",             0xFF00D7FF }, { "goldenrod",            0xFF20A5DA }, { "greenyellow",       0xFF2FFFAD }, { "grey",            0xFF808080 }, 
	{ "honeydew",         0xFFF0FFF0 }, { "hotpink",              0xFFB469FF }, { "indianred",         0xFF5C5CCD }, { "indigo",          0xFF82004B }, 
	{ "ivory",            0xFFF0FFFF }, { "khaki",                0xFF8CE6F0 }, { "lavender",          0xFFF1E6E6 }, { "lavenderblush",   0xFFF5F0FF }, 
	{ "lawngreen",        0xFF00FC7C }, { "lemonchiffon",         0xFFCDFAFF }, { "lightblue",         0xFFE6D8AD }, { "lightcoral",      0xFF8080F0 },
	{ "lightcyan",        0xFFFFFFE0 }, { "lightgoldenrodyellow", 0xFFD2FAFA }, { "lightgray",         0xFFD3D3D3 }, { "lightgreen",      0xFF90EE90 }, 
	{ "lightgrey",        0xFFD3D3D3 }, { "lightpink",            0xFFC1B6FF }, { "lightsalmon",       0xFF7AA0FF }, { "lightseagreen",   0xFFAAB220 }, 
	{ "lightskyblue",     0xFFFACE87 }, { "lightslategray",       0xFF778899 }, { "lightslategrey",    0xFF778899 }, { "lightsteelblue",  0xFFDEC4B0 }, 
	{ "lightyellow",      0xFFE0FFFF }, { "limegreen",            0xFF32CD32 }, { "linen",             0xFFE6F0FA }, { "magenta",         0xFFFF00FF }, 
	{ "mediumaquamarine", 0xFFAACD66 }, { "mediumblue",           0xFFCD0000 }, { "mediumorchid",      0xFFD355BA }, { "mediumpurple",    0xFFDB7093 },
	{ "mediumseagreen",   0xFF71B33C }, { "mediumslateblue",      0xFFEE687B }, { "mediumspringgreen", 0xFF9AFA00 }, { "mediumturquoise", 0xFFCCD148 }, 
	{ "mediumvioletred",  0xFF8515C7 }, { "midnightblue",         0xFF701919 }, { "mintcream",         0xFFFAFFF5 }, { "mistyrose",       0xFFE1E4FF }, 
	{ "moccasin",         0xFFB5E4FF }, { "navajowhite",          0xFFADDEFF }, { "oldlace",           0xFFE6F5FD }, { "olivedrab",       0xFF238E6B }, 
	{ "orangered",        0xFF0045FF }, { "orchid",               0xFFD670DA }, { "palegoldenrod",     0xFFAAE8EE }, { "palegreen",       0xFF98FB98 }, 
	{ "paleturquoise",    0xFFEEEEAF }, { "palevioletred",        0xFF9370DB }, { "papayawhip",        0xFFF5EFFF }, { "peachpuff",       0xFFB9DAFF },
	{ "peru",             0xFF3F85CD }, { "pink",                 0xFFCBC0FF }, { "plum",              0xFFDDA0DD }, { "powderblue",      0xFFE6E0B0 }, 
	{ "rosybrown",        0xFF8F8FBC }, { "royalblue",            0xFFE16941 }, { "saddlebrown",       0xFF13458B }, { "salmon",          0xFF7280FA }, 
	{ "sandybrown",       0xFF60A4F4 }, { "seagreen",             0xFF578B2E }, { "seashell",          0xFFEEF5FF }, { "sienna",          0xFF2D52A0 }, 
	{ "skyblue",          0xFFEBCE87 }, { "slateblue",            0xFFCDA56A }, { "slategray",         0xFF908070 }, { "slategrey",       0xFF908070 }, 
	{ "snow",             0xFFFAFAFF }, { "springgreen",          0xFF7FFF00 }, { "steelblue",         0xFFB48246 }, { "tan",             0xFF8CB4D2 },
	{ "thistle",          0xFFD8BFD8 }, { "tomato",               0xFF4763FF }, { "turquoise",         0xFFD0E040 }, { "violet",          0xFFEE82EE }, 
	{ "wheat",            0xFFB3DEF5 }, { "whitesmoke",           0xFFF5F5F5 }, { "yellowgreen",       0xFF32CD9A }, { "rebeccapurple",   0xFF993366 },
};

static const uint32_t kNumCSSColors = BX_COUNTOF(kCSSColors);

static bool parseShapes(ParserState* parser, ShapeList* shapeList, const ShapeAttributes* parentAttrs, const char* closingTag, uint32_t closingTagLen);
static const char* parseCoord(const char* str, const char* end, float* coord);
static ParseAttr::Result parseGenericShapeAttribute(const bx::StringView& name, const bx::StringView& value, ShapeAttributes* attrs);

inline uint8_t charToNibble(char ch)
{
	if (ch >= '0' && ch <= '9') {
		return ch - '0';
	} else if (ch >= 'a' && ch <= 'f') {
		return 10 + (ch - 'a');
	} else if (ch >= 'A' && ch <= 'F') {
		return 10 + (ch - 'A');
	}

	SSVG_WARN(false, "Invalid hex char %c", ch);

	return 0;
}

inline const char* skipWhitespace(const char* ptr, const char* end)
{
	while (ptr != end && bx::isSpace(*ptr) && *ptr != '\0') {
		++ptr;
	}

	return ptr;
}

inline const char* skipCommaWhitespace(const char* ptr, const char* end)
{
	// comma-wsp: (wsp+ comma? wsp*) | (comma wsp*)
	ptr = skipWhitespace(ptr, end);
	if (*ptr == ',') {
		ptr = skipWhitespace(ptr + 1, end);
	}

	return ptr;
}

inline bool parserDone(ParserState* parser)
{
	return *parser->m_Ptr == 0;
}

inline void parserSkipWhitespace(ParserState* parser)
{
	parser->m_Ptr = skipWhitespace(parser->m_Ptr, nullptr);
}

static bool parserExpectingChar(ParserState* parser, char ch)
{
	parserSkipWhitespace(parser);

	if (*parser->m_Ptr == ch) {
		++parser->m_Ptr;
		return true;
	}

	return false;
}

static bool parserMatchString(ParserState* parser, const char* str, uint32_t len)
{
	parserSkipWhitespace(parser);

	return !bx::strCmp(bx::StringView(parser->m_Ptr, len), bx::StringView(str, len), (int32_t)len);
}

inline bool parserExpectingString(ParserState* parser, const char* str, uint32_t len)
{
	if (parserMatchString(parser, str, len)) {
		parser->m_Ptr += len;
		parserSkipWhitespace(parser);
		return true;
	}

	return false;
}

// Skips the currently entered tag by counting > and <.
static void parserSkipTag(ParserState* parser)
{
	uint32_t level = 0;
	uint32_t numOpenBrackets = 1;
	bool incLevelOnClose = true;
	while (!parserDone(parser)) {
		char ch = *parser->m_Ptr;
		parser->m_Ptr++;

		if (ch == '/' && *parser->m_Ptr == '>') {
			SSVG_CHECK(numOpenBrackets != 0, "Unbalanced brackets");
			parser->m_Ptr++;
			--numOpenBrackets;
		} else if (ch == '>') {
			SSVG_CHECK(numOpenBrackets != 0, "Unbalanced brackets");
			--numOpenBrackets;
			if (incLevelOnClose) {
				++level;
			}
			incLevelOnClose = true;
		} else if (ch == '<') {
			++numOpenBrackets;
			if (*parser->m_Ptr == '/') {
				SSVG_CHECK(level != 0, "Unbalanched tags");
				parser->m_Ptr++;
				--level;
				incLevelOnClose = false;
			}
		}

		if (numOpenBrackets == 0 && level == 0) {
			break;
		}
	}
}

static void parserSkipComment(ParserState* parser)
{
	while (!parserDone(parser)) {
		if (parser->m_Ptr[0] == '-' && parser->m_Ptr[1] == '-' && parser->m_Ptr[2] == '>') {
			parser->m_Ptr += 3;
			break;
		}

		parser->m_Ptr++;
	}
}

static bool parserGetTag(ParserState* parser, bx::StringView* tag)
{
	if (!parserExpectingChar(parser, '<')) {
		return false;
	}
	parserSkipWhitespace(parser); // Is it valid to have a whitespace after the < for a tag?

	const char* tagPtr = parser->m_Ptr;
	++parser->m_Ptr;

	// Search for the next whitespace or closing angle bracket
	while (!parserDone(parser) && !bx::isSpace(*parser->m_Ptr) && *parser->m_Ptr != '>') {
		++parser->m_Ptr;
	}

	if (parserDone(parser)) {
		return false;
	}

	tag->set(tagPtr, parser->m_Ptr);

	if (!bx::strCmp(*tag, "!--", 3)) {
		parserSkipComment(parser);
		return parserGetTag(parser, tag);
	}

	return true;
}

static bool parserGetAttribute(ParserState* parser, bx::StringView* name, bx::StringView* value)
{
	parserSkipWhitespace(parser);

	if (!bx::isAlpha(*parser->m_Ptr)) {
		return false;
	}

	const char* namePtr = parser->m_Ptr;

	// Skip the identifier
	while (bx::isAlphaNum(*parser->m_Ptr)
		|| *parser->m_Ptr == '-'
		|| *parser->m_Ptr == '_'
		|| *parser->m_Ptr == ':') {
		++parser->m_Ptr;
	}

	name->set(namePtr, parser->m_Ptr);

	// Check of the equal sign
	parserSkipWhitespace(parser);
	if (!parserExpectingChar(parser, '=')) {
		return false;
	}

	// Check of opening quote
	parserSkipWhitespace(parser);
	if (!parserExpectingChar(parser, '\"')) {
		return false;
	}

	const char* valuePtr = parser->m_Ptr;

	// Find the closing quote
	while (*parser->m_Ptr != '\"') {
		// Check for invalid strings (i.e. tag closes before closing quote or we reached the end of the buffer)
		char ch = *parser->m_Ptr;
		if (ch == '>' || ch == '\0') {
			return false;
		}

		++parser->m_Ptr;
	}

	value->set(valuePtr, parser->m_Ptr);

	++parser->m_Ptr;

	return true;
}

static bool parseVersion(const bx::StringView& verStr, uint16_t* maj, uint16_t* min)
{
	const float fver = (float)atof(verStr.getPtr());
	*maj = (uint16_t)bx::floor(fver);
	*min = (uint16_t)bx::floor((fver - *maj) * 10.0f);

	return true;
}

static bool parseNumber(const bx::StringView& str, float* val, float min = -FLT_MAX, float max = FLT_MAX)
{
	*val = bx::clamp<float>((float)atof(str.getPtr()), min, max);

	return true;
}

static bool parseLength(const bx::StringView& str, float* len)
{
	// TODO: Parse length units and convert to pixels based on parser state.
	return parseNumber(str, len);
}

static bool parsePaint(const bx::StringView& str, Paint* paint)
{
	// TODO: Handle more cases.
	if (!bx::strCmp(str, "none", 4)) {
		paint->m_Type = PaintType::None;
	} else if (!bx::strCmp(str, "transparent", 11)) {
		paint->m_Type = PaintType::Transparent;
	} else {
		paint->m_Type = PaintType::Color;

		const char* ptr = str.getPtr();
		paint->m_ColorABGR = 0xFF000000;
		if (*ptr == '#') {
			// Hex color
			if (str.getLength() == 7) {
				const uint8_t r = (charToNibble(ptr[1]) << 4) | charToNibble(ptr[2]);
				const uint8_t g = (charToNibble(ptr[3]) << 4) | charToNibble(ptr[4]);
				const uint8_t b = (charToNibble(ptr[5]) << 4) | charToNibble(ptr[6]);
				paint->m_ColorABGR |= ((uint32_t)r) | ((uint32_t)g << 8) | ((uint32_t)b << 16);
			} else if (str.getLength() == 4) {
				const uint8_t r = charToNibble(ptr[1]);
				const uint8_t g = charToNibble(ptr[2]);
				const uint8_t b = charToNibble(ptr[3]);
				const uint32_t rgb = ((uint32_t)r) | ((uint32_t)g << 8) | ((uint32_t)b << 16);
				paint->m_ColorABGR |= rgb | (rgb << 4);
			} else {
				SSVG_WARN(false, "Unknown hex color format %.*s", str.getLength(), str.getPtr());
			}
		} else if (!bx::strCmp(str, "rgb(", 4)) {
			// TODO: This doesn't work with percentages.
			float color[3];
			const char* end = str.getTerm();
			ptr += 4;
			ptr = parseCoord(ptr, end, &color[0]);
			ptr = parseCoord(ptr, end, &color[1]);
			ptr = parseCoord(ptr, end, &color[2]);

			paint->m_ColorABGR |= ((uint32_t)color[0]) | ((uint32_t)color[1] << 8) | ((uint32_t)color[2] << 16);
		} else if (!bx::strCmp(str, "rgba(", 5)) {
			// TODO: This doesn't work with percentages.
			float color[4];
			const char* end = str.getTerm();
			ptr += 5;
			ptr = parseCoord(ptr, end, &color[0]);
			ptr = parseCoord(ptr, end, &color[1]);
			ptr = parseCoord(ptr, end, &color[2]);
			ptr = parseCoord(ptr, end, &color[3]);

			paint->m_ColorABGR = ((uint32_t)color[0]) | ((uint32_t)color[1] << 8) | ((uint32_t)color[2] << 16) | ((uint32_t)(color[3] * 255.0f) << 24);
		} else {
			// Check if it's a known color.
			bool found = false;
			for (uint32_t i = 0; i < kNumCSSColors; ++i) {
				if (!bx::strCmp(str, kCSSColors[i].m_Name)) {
					paint->m_ColorABGR = kCSSColors[i].m_ABGR;
					found = true;
					break;
				}
			}

			if (!found) {
				SSVG_WARN(false, "Unhandled paint value: %.*s", str.getLength(), str.getPtr());
			}
		}
	}

	return true;
}

static const char* parseCoord(const char* str, const char* end, float* coord)
{
	const char* ptr = skipCommaWhitespace(str, end);

	SSVG_CHECK(ptr != end && !bx::isAlpha(*ptr), "Parse error");

	char* coordEnd;
	*coord = strtof(ptr, &coordEnd);

	SSVG_CHECK(coordEnd != nullptr, "Failed to parse coordinate");
	SSVG_CHECK(coordEnd <= end, "strtof() read past end of buffer");

	return skipCommaWhitespace(coordEnd, end);
}

static const char* parseFlag(const char* str, const char* end, float* flag)
{
	const char* ptr = skipCommaWhitespace(str, end);

	SSVG_CHECK(ptr != end && !bx::isAlpha(*ptr), "Parse error");

	if (*ptr == '0') {
		*flag = 0.0f;
	} else {
		*flag = 1.0f;
	}

	return skipCommaWhitespace(ptr + 1, end);
}

static bool parseViewBox(const bx::StringView& str, float* viewBox)
{
	const char* ptr = str.getPtr();
	const char* end = str.getTerm();

	ptr = parseCoord(ptr, end, &viewBox[0]);
	ptr = parseCoord(ptr, end, &viewBox[1]);
	ptr = parseCoord(ptr, end, &viewBox[2]);
	parseCoord(ptr, end, &viewBox[3]);

	return true;
}

// Scans str and extracts type and value. Expected format is: 
//    type '(' value ')' 
// where type is an identifier and value is any kind of text
static const char* parseTransformComponent(const char* str, const char* end, bx::StringView* type, bx::StringView* value)
{
	SSVG_CHECK(bx::isAlpha(*str), "Parse error: Excepted identifier");

	const char* ptr = str;
	while (ptr != end && bx::isAlpha(*ptr)) {
		++ptr;
	}

	if (ptr == end) {
		SSVG_CHECK(false, "Parse error: Transformation component ended early");
		return nullptr;
	}

	type->set(str, (int32_t)(ptr - str));

	ptr = skipWhitespace(ptr, end);

	if (*ptr != '(') {
		SSVG_CHECK(false, "Parse error: Expected '('");
		return nullptr;
	}

	ptr = skipWhitespace(ptr + 1, end);

	const char* valuePtr = ptr;

	// Skip until closing parenthesis
	while (ptr != end && *ptr != ')') {
		++ptr;
	}

	if (ptr == end) {
		SSVG_CHECK(false, "Parse error: Couldn't find closing parenthesis");
		return nullptr;
	}
	SSVG_CHECK(*ptr == ')', "Parse error: Expected ')'");

	const char* endPtr = ptr + 1;

	// Walk back ptr to get a tight value string (without trailing whitespaces)
	while (ptr != valuePtr && bx::isSpace(*ptr)) {
		--ptr;
	}

	value->set(valuePtr, (int32_t)(ptr - valuePtr));

	return endPtr;
}

static bool parseTransform(const bx::StringView& str, float* transform)
{
	const char* ptr = str.getPtr();
	const char* end = str.getTerm();

	transformIdentity(transform);
	ptr = skipWhitespace(ptr, end);
	while (ptr != end) {
		bx::StringView type, value;

		ptr = parseTransformComponent(ptr, end, &type, &value);
		if (!ptr) {
			SSVG_CHECK(false, "Parse error");
			return false;
		}

		ptr = skipCommaWhitespace(ptr, end);

		// Parse the transform value.
		const char* valuePtr = value.getPtr();
		const char* valueEnd = value.getTerm();

		float comp[6];
		transformIdentity(&comp[0]);
		if (!bx::strCmp(type, "matrix", 6)) {
			valuePtr = parseCoord(valuePtr, valueEnd, &comp[0]);
			valuePtr = parseCoord(valuePtr, valueEnd, &comp[1]);
			valuePtr = parseCoord(valuePtr, valueEnd, &comp[2]);
			valuePtr = parseCoord(valuePtr, valueEnd, &comp[3]);
			valuePtr = parseCoord(valuePtr, valueEnd, &comp[4]);
			valuePtr = parseCoord(valuePtr, valueEnd, &comp[5]);

		} else if (!bx::strCmp(type, "translate", 9)) {
			valuePtr = parseCoord(valuePtr, valueEnd, &comp[4]);
			if (valuePtr != valueEnd) {
				valuePtr = parseCoord(valuePtr, valueEnd, &comp[5]);
			}
		} else if (!bx::strCmp(type, "scale", 5)) {
			valuePtr = parseCoord(valuePtr, valueEnd, &comp[0]);
			if (valuePtr != valueEnd) {
				valuePtr = parseCoord(valuePtr, valueEnd, &comp[3]);
			} else {
				comp[3] = comp[0];
			}
		} else if (!bx::strCmp(type, "rotate", 6)) {
			float angle_deg;
			valuePtr = parseCoord(valuePtr, valueEnd, &angle_deg);

			const float angle_rad = bx::toRad(angle_deg);
			const float cosAngle = bx::cos(angle_rad);
			const float sinAngle = bx::sin(angle_rad);
			comp[0] = cosAngle;
			comp[1] = sinAngle;
			comp[2] = -sinAngle;
			comp[3] = cosAngle;

			if (valuePtr != valueEnd) {
				float cX, cY;
				valuePtr = parseCoord(valuePtr, valueEnd, &cX);
				valuePtr = parseCoord(valuePtr, valueEnd, &cY);

				// translate(cX, cY) rotate() translate(-cX, -cY)
				comp[4] = cX * (1.0f - cosAngle) + cY * sinAngle;
				comp[5] = cY * (1.0f - cosAngle) - cX * sinAngle;
			}
		} else if (!bx::strCmp(type, "skewX", 5)) {
			float angle_deg;
			valuePtr = parseCoord(valuePtr, valueEnd, &angle_deg);

			const float angle_rad = bx::toRad(angle_deg);
			comp[2] = bx::tan(angle_rad);
		} else if (!bx::strCmp(type, "skewY", 5)) {
			float angle_deg;
			valuePtr = parseCoord(valuePtr, valueEnd, &angle_deg);

			const float angle_rad = bx::toRad(angle_deg);
			comp[1] = bx::tan(angle_rad);
		} else {
			SSVG_WARN(false, "Unknown transform component %.*s(%.*s)", type.getLength(), type.getPtr(), value.getLength(), value.getPtr());
			valuePtr = valueEnd;
		}

		SSVG_WARN(valuePtr == valueEnd, "Incomplete transformation parsing");

		transformMultiply(transform, comp);
	}

	return true;
}

bool pathFromString(Path* path, const bx::StringView& str, uint32_t flags)
{
	const char* ptr = str.getPtr();
	const char* end = str.getTerm();
	float firstX = 0.0f;
	float firstY = 0.0f;
	float lastX = 0.0f;
	float lastY = 0.0f;
	float lastCPX = 0.0f;
	float lastCPY = 0.0f;
	char lastCommand = 0;

	while (ptr != end) {
		char ch = *ptr;

		SSVG_CHECK(!bx::isSpace(ch) && ch != ',', "Parse error");

		if (bx::isAlpha(ch)) {
			++ptr;
		} else {
			ch = lastCommand;
		}

		const char lch = bx::toLower(ch);

		if (lch == 'm') {
			// MoveTo
			PathCmd* cmd = pathAllocCommand(path, PathCmdType::MoveTo);
			ptr = parseCoord(ptr, end, &cmd->m_Data[0]);
			ptr = parseCoord(ptr, end, &cmd->m_Data[1]);

			if (ch == lch) {
				cmd->m_Data[0] += lastX;
				cmd->m_Data[1] += lastY;
			}

			firstX = lastX = cmd->m_Data[0];
			firstY = lastY = cmd->m_Data[1];

			// https://www.w3.org/TR/SVG/paths.html#PathDataMovetoCommands
			// If a moveto is followed by multiple pairs of coordinates, the subsequent pairs are treated 
			// as implicit lineto commands. Hence, implicit lineto commands will be relative if the moveto 
			// is relative, and absolute if the moveto is absolute. If a relative moveto (m) appears as the 
			// first element of the path, then it is treated as a pair of absolute coordinates. In this case, 
			// subsequent pairs of coordinates are treated as relative even though the initial moveto is 
			// interpreted as an absolute moveto.
			ch = bx::isLower(ch) ? 'l' : 'L';
		} else if (lch == 'l') {
			// LineTo abs
			PathCmd* cmd = pathAllocCommand(path, PathCmdType::LineTo);
			ptr = parseCoord(ptr, end, &cmd->m_Data[0]);
			ptr = parseCoord(ptr, end, &cmd->m_Data[1]);

			if (ch == lch) {
				cmd->m_Data[0] += lastX;
				cmd->m_Data[1] += lastY;
			}

			lastX = cmd->m_Data[0];
			lastY = cmd->m_Data[1];
		} else if (lch == 'h') {
			// Horizontal LineTo abs
			PathCmd* cmd = pathAllocCommand(path, PathCmdType::LineTo);
			ptr = parseCoord(ptr, end, &cmd->m_Data[0]);
			cmd->m_Data[1] = lastY;

			if (ch == lch) {
				cmd->m_Data[0] += lastX;
			}

			lastX = cmd->m_Data[0];
			lastY = cmd->m_Data[1];
		} else if (lch == 'v') {
			// Vertical LineTo abs
			PathCmd* cmd = pathAllocCommand(path, PathCmdType::LineTo);
			ptr = parseCoord(ptr, end, &cmd->m_Data[1]);
			cmd->m_Data[0] = lastX;

			if (ch == lch) {
				cmd->m_Data[1] += lastY;
			}

			lastX = cmd->m_Data[0];
			lastY = cmd->m_Data[1];
		} else if (lch == 'z') {
			// ClosePath
			PathCmd* cmd = pathAllocCommand(path, PathCmdType::ClosePath);
			BX_UNUSED(cmd);
			lastX = firstX;
			lastY = firstY;
			// No data
			ptr = skipCommaWhitespace(ptr, end);
		} else if (lch == 'c') {
			// CubicTo abs
			PathCmd* cmd = pathAllocCommand(path, PathCmdType::CubicTo);
			ptr = parseCoord(ptr, end, &cmd->m_Data[0]);
			ptr = parseCoord(ptr, end, &cmd->m_Data[1]);
			ptr = parseCoord(ptr, end, &cmd->m_Data[2]);
			ptr = parseCoord(ptr, end, &cmd->m_Data[3]);
			ptr = parseCoord(ptr, end, &cmd->m_Data[4]);
			ptr = parseCoord(ptr, end, &cmd->m_Data[5]);

			if (ch == lch) {
				cmd->m_Data[0] += lastX;
				cmd->m_Data[1] += lastY;
				cmd->m_Data[2] += lastX;
				cmd->m_Data[3] += lastY;
				cmd->m_Data[4] += lastX;
				cmd->m_Data[5] += lastY;
			}

			lastCPX = cmd->m_Data[2];
			lastCPY = cmd->m_Data[3];
			lastX = cmd->m_Data[4];
			lastY = cmd->m_Data[5];
		} else if (lch == 's') {
			// CubicTo abs
			PathCmd* cmd = pathAllocCommand(path, PathCmdType::CubicTo);
			ptr = parseCoord(ptr, end, &cmd->m_Data[2]);
			ptr = parseCoord(ptr, end, &cmd->m_Data[3]);
			ptr = parseCoord(ptr, end, &cmd->m_Data[4]);
			ptr = parseCoord(ptr, end, &cmd->m_Data[5]);

			// The first control point is assumed to be the reflection of the second control point on 
			// the previous command relative to the current point. (If there is no previous command or 
			// if the previous command was not an C, c, S or s, assume the first control point is 
			// coincident with the current point.) 
			const char lastCmdLower = bx::toLower(lastCommand);
			if (lastCmdLower == 'c' || lastCmdLower == 's') {
				const float dx = lastX - lastCPX;
				const float dy = lastY - lastCPY;
				cmd->m_Data[0] = lastX + dx;
				cmd->m_Data[1] = lastY + dy;
			} else {
				cmd->m_Data[0] = lastX;
				cmd->m_Data[1] = lastY;
			}

			if (ch == lch) {
				cmd->m_Data[2] += lastX;
				cmd->m_Data[3] += lastY;
				cmd->m_Data[4] += lastX;
				cmd->m_Data[5] += lastY;
			}

			lastCPX = cmd->m_Data[2];
			lastCPY = cmd->m_Data[3];
			lastX = cmd->m_Data[4];
			lastY = cmd->m_Data[5];
		} else if (lch == 'q') {
			// QuadraticTo abs
			PathCmd* cmd = pathAllocCommand(path, PathCmdType::QuadraticTo);
			ptr = parseCoord(ptr, end, &cmd->m_Data[0]);
			ptr = parseCoord(ptr, end, &cmd->m_Data[1]);
			ptr = parseCoord(ptr, end, &cmd->m_Data[2]);
			ptr = parseCoord(ptr, end, &cmd->m_Data[3]);

			if (ch == lch) {
				cmd->m_Data[0] += lastX;
				cmd->m_Data[1] += lastY;
				cmd->m_Data[2] += lastX;
				cmd->m_Data[3] += lastY;
			}

			lastCPX = cmd->m_Data[0];
			lastCPY = cmd->m_Data[1];
			lastX = cmd->m_Data[2];
			lastY = cmd->m_Data[3];

			if ((flags & ImageLoadFlags::ConvertQuadToCubicBezier) != 0) {
				pathConvertCommand(path, (uint32_t)(cmd - path->m_Commands), PathCmdType::CubicTo);
			}
		} else if (lch == 't') {
			// QuadraticTo abs
			PathCmd* cmd = pathAllocCommand(path, PathCmdType::QuadraticTo);
			ptr = parseCoord(ptr, end, &cmd->m_Data[2]);
			ptr = parseCoord(ptr, end, &cmd->m_Data[3]);

			// The control point is assumed to be the reflection of the control point on the 
			// previous command relative to the current point. (If there is no previous command 
			// or if the previous command was not a Q, q, T or t, assume the control point is 
			// coincident with the current point.)
			const char lastCmdLower = bx::toLower(lastCommand);
			if (lastCmdLower == 'q' || lastCmdLower == 't') {
				const float dx = lastX - lastCPX;
				const float dy = lastY - lastCPY;
				cmd->m_Data[0] = lastX + dx;
				cmd->m_Data[1] = lastY + dy;
			} else {
				cmd->m_Data[0] = lastX;
				cmd->m_Data[1] = lastY;
			}

			if (ch == lch) {
				cmd->m_Data[2] += lastX;
				cmd->m_Data[3] += lastY;
			}

			lastCPX = cmd->m_Data[0];
			lastCPY = cmd->m_Data[1];
			lastX = cmd->m_Data[2];
			lastY = cmd->m_Data[3];

			if ((flags & ImageLoadFlags::ConvertQuadToCubicBezier) != 0) {
				pathConvertCommand(path, (uint32_t)(cmd - path->m_Commands), PathCmdType::CubicTo);
			}
		} else if (lch == 'a') {
			// ArcTo abs
			PathCmd* cmd = pathAllocCommand(path, PathCmdType::ArcTo);
			ptr = parseCoord(ptr, end, &cmd->m_Data[0]);
			ptr = parseCoord(ptr, end, &cmd->m_Data[1]);
			ptr = parseCoord(ptr, end, &cmd->m_Data[2]);
			ptr = parseFlag(ptr, end, &cmd->m_Data[3]);
			ptr = parseFlag(ptr, end, &cmd->m_Data[4]);
			ptr = parseCoord(ptr, end, &cmd->m_Data[5]);
			ptr = parseCoord(ptr, end, &cmd->m_Data[6]);

			if (ch == lch) {
				cmd->m_Data[5] += lastX;
				cmd->m_Data[6] += lastY;
			}

			lastX = cmd->m_Data[5];
			lastY = cmd->m_Data[6];

			if ((flags & ImageLoadFlags::ConvertArcToCubicBezier) != 0) {
				pathConvertCommand(path, (uint32_t)(cmd - path->m_Commands), PathCmdType::CubicTo);
			}
		} else {
			SSVG_WARN(false, "Encountered unknown path command");
			return false;
		}

		lastCommand = ch;
	}

	pathShrinkToFit(path);

	return true;
}

bool pointListFromString(PointList* ptList, const bx::StringView& str)
{
	const char* ptr = str.getPtr();
	const char* end = str.getTerm();
	while (ptr != end) {
		float* pt = pointListAllocPoints(ptList, 1);
		ptr = parseCoord(ptr, end, &pt[0]);
		ptr = parseCoord(ptr, end, &pt[1]);
	}

	pointListShrinkToFit(ptList);

	return true;
}

static ParseAttr::Result parseStyle(const bx::StringView& str, ShapeAttributes* attrs)
{
	const char* end = str.getTerm();
	const char* ptr = skipWhitespace(str.getPtr(), end);
	while (ptr != end) {
		const char* nameStart = ptr;
		while (ptr != end && (bx::isAlpha(*ptr) || *ptr == '-')) {
			ptr++;
		}

		if (ptr == end) {
			return ParseAttr::Fail;
		}

		const bx::StringView name(nameStart, ptr);

		ptr = skipWhitespace(ptr, end);

		if (*ptr != ':') {
			return ParseAttr::Fail;
		}
		
		ptr = skipWhitespace(ptr + 1, end);

		const char* valueStart = ptr;
		while (ptr != end && *ptr != ';') {
			++ptr;
		}

		const bx::StringView value(valueStart, ptr);

		ptr = skipWhitespace(ptr + (ptr != end ? 1 : 0), end);

		if (parseGenericShapeAttribute(name, value, attrs) == ParseAttr::Fail) {
			return ParseAttr::Fail;
		}
	}

	return ParseAttr::OK;
}

static ParseAttr::Result parseGenericShapeAttribute(const bx::StringView& name, const bx::StringView& value, ShapeAttributes* attrs)
{
	if (!bx::strCmp(name, "style", 5)) {
		return parseStyle(value, attrs);
	} else if (!bx::strCmp(name, "stroke", 6)) {
		const bx::StringView partialName(name.getPtr() + 6, name.getLength() - 6);
		if (partialName.getLength() == 0) {
			attrs->m_Flags &= ~AttribFlags::StrokePaintInherit;
			return parsePaint(value, &attrs->m_StrokePaint) ? ParseAttr::OK : ParseAttr::Fail;
		} else if (!bx::strCmp(partialName, "-miterlimit", 11)) {
			attrs->m_Flags &= ~AttribFlags::StrokeMiterLimitInherit;
			return parseNumber(value, &attrs->m_StrokeMiterLimit, 1.0f) ? ParseAttr::OK : ParseAttr::Fail;
		} else if (!bx::strCmp(partialName, "-linejoin", 9)) {
			attrs->m_Flags &= ~AttribFlags::StrokeLineJoinInherit;
			if (!bx::strCmp(value, "miter", 5)) {
				attrs->m_StrokeLineJoin = LineJoin::Miter;
			} else if (!bx::strCmp(value, "round", 5)) {
				attrs->m_StrokeLineJoin = LineJoin::Round;
			} else if (!bx::strCmp(value, "bevel", 5)) {
				attrs->m_StrokeLineJoin = LineJoin::Bevel;
			} else {
				return ParseAttr::Fail;
			}

			return ParseAttr::OK;
		} else if (!bx::strCmp(partialName, "-linecap", 8)) {
			attrs->m_Flags &= ~AttribFlags::StrokeLineCapInherit;
			if (!bx::strCmp(value, "butt", 4)) {
				attrs->m_StrokeLineCap = LineCap::Butt;
			} else if (!bx::strCmp(value, "round", 5)) {
				attrs->m_StrokeLineCap = LineCap::Round;
			} else if (!bx::strCmp(value, "square", 6)) {
				attrs->m_StrokeLineCap = LineCap::Square;
			} else {
				return ParseAttr::Fail;
			}

			return ParseAttr::OK;
		} else if (!bx::strCmp(partialName, "-opacity", 8)) {
			attrs->m_Flags &= ~AttribFlags::StrokeOpacityInherit;
			return parseNumber(value, &attrs->m_StrokeOpacity, 0.0f, 1.0f) ? ParseAttr::OK : ParseAttr::Fail;
		} else if (!bx::strCmp(partialName, "-width", 6)) {
			attrs->m_Flags &= ~AttribFlags::StrokeWidthInherit;
			return parseLength(value, &attrs->m_StrokeWidth) ? ParseAttr::OK : ParseAttr::Fail;
		}
	} else if (!bx::strCmp(name, "fill", 4)) {
		const bx::StringView partialName(name.getPtr() + 4, name.getLength() - 4);
		if (partialName.getLength() == 0) {
			attrs->m_Flags &= ~AttribFlags::FillPaintInherit;
			return parsePaint(value, &attrs->m_FillPaint) ? ParseAttr::OK : ParseAttr::Fail;
		} else if (!bx::strCmp(partialName, "-opacity", 8)) {
			attrs->m_Flags &= ~AttribFlags::FillOpacityInherit;
			return parseNumber(value, &attrs->m_FillOpacity, 0.0f, 1.0f) ? ParseAttr::OK : ParseAttr::Fail;
		} else if (!bx::strCmp(partialName, "-rule", 5)) {
			attrs->m_Flags &= ~AttribFlags::FillRuleInherit;
			if (!bx::strCmp(value, "nonzero", 7)) {
				attrs->m_FillRule = FillRule::NonZero;
			} else if (!bx::strCmp(value, "evenodd", 7)) {
				attrs->m_FillRule = FillRule::EvenOdd;
			} else {
				return ParseAttr::Fail;
			}

			return ParseAttr::OK;
		}
	} else if (!bx::strCmp(name, "font", 4)) {
		const bx::StringView partialName(name.getPtr() + 4, name.getLength() - 4);
		if (!bx::strCmp(partialName, "-family", 7)) {
			attrs->m_Flags &= ~AttribFlags::FontFamilyInherit;
			shapeAttrsSetFontFamily(attrs, value);
			return ParseAttr::OK;
		} else if (!bx::strCmp(partialName, "-size", 5)) {
			attrs->m_Flags &= ~AttribFlags::FontSizeInherit;
			return parseLength(value, &attrs->m_FontSize) ? ParseAttr::OK : ParseAttr::Fail;
		}
	} else if (!bx::strCmp(name, "transform", 9)) {
		return parseTransform(value, &attrs->m_Transform[0]) ? ParseAttr::OK : ParseAttr::Fail;
	} else if (!bx::strCmp(name, "id", 2)) {
		shapeAttrsSetID(attrs, value);
		return ParseAttr::OK;
	} else if (!bx::strCmp(name, "class", 5)) {
#if SSVG_CONFIG_CLASS_MAX_LEN
		shapeAttrsSetClass(attrs, value);
#endif
		return ParseAttr::OK;
	} else if (!bx::strCmp(name, "opacity", 7)) {
		return parseNumber(value, &attrs->m_Opacity, 0.0f, 1.0f) ? ParseAttr::OK : ParseAttr::Fail;
	}

	return ParseAttr::Unknown;
}

static bool parseShape_Group(ParserState* parser, Shape* group)
{
	bool err = false;
	while (!parserDone(parser) && !err) {
		SSVG_CHECK(!(parser->m_Ptr[0] == '/' && parser->m_Ptr[1] == '>'), "Empty group element");
		if (parserExpectingChar(parser, '>')) {
			break;
		}

		bx::StringView name, value;
		if (!parserGetAttribute(parser, &name, &value)) {
			err = true;
		} else {
			// Check if this a generic attribute (i.e. styling)
			ParseAttr::Result res = parseGenericShapeAttribute(name, value, group->m_Attrs);
			if (res == ParseAttr::Fail) {
				err = true;
			} else if (res == ParseAttr::Unknown) {
				// No specific attributes for groups. Ignore it.
				SSVG_WARN(false, "Ignoring g attribute: %.*s=\"%.*s\"", name.getLength(), name.getPtr(), value.getLength(), value.getPtr());
			}
		}
	}

	if (err) {
		return false;
	}

	return parseShapes(parser, &group->m_ShapeList, group->m_Attrs, "</g>", 4);
}

static bool parseShape_Text(ParserState* parser, Shape* text)
{
	bool err = false;
	while (!parserDone(parser) && !err) {
		SSVG_CHECK(!(parser->m_Ptr[0] == '/' && parser->m_Ptr[1] == '>'), "Empty text element");
		if (parser->m_Ptr[0] == '>') {
			break;
		}

		bx::StringView name, value;
		if (!parserGetAttribute(parser, &name, &value)) {
			err = true;
		} else {
			// Check if this a generic attribute (i.e. styling)
			ParseAttr::Result res = parseGenericShapeAttribute(name, value, text->m_Attrs);
			if (res == ParseAttr::Fail) {
				err = true;
			} else if (res == ParseAttr::Unknown) {
				// Text specific attributes
				if (!bx::strCmp(name, "x", 1)) {
					err = !parseLength(value, &text->m_Text.x);
				} else if (!bx::strCmp(name, "y", 1)) {
					err = !parseLength(value, &text->m_Text.y);
				} else if (!bx::strCmp(name, "text-anchor", 11)) {
					if (!bx::strCmp(value, "start", 5)) {
						text->m_Text.m_Anchor = TextAnchor::Start;
					} else if (!bx::strCmp(value, "middle", 6)) {
						text->m_Text.m_Anchor = TextAnchor::Middle;
					} else if (!bx::strCmp(value, "end", 3)) {
						text->m_Text.m_Anchor = TextAnchor::End;
					} else {
						err = true;
					}
				} else {
					SSVG_WARN(false, "Ignoring text attribute: %.*s=\"%.*s\"", name.getLength(), name.getPtr(), value.getLength(), value.getPtr());
				}
			}
		}
	}

	if (err || parserDone(parser)) {
		return false;
	}

#if 1
	parserSkipTag(parser);
#else
	// TODO: Parse <tspan> blocks
	const char* txtPtr = parser->m_Ptr;

	while (!parserDone(parser) && *parser->m_Ptr != '<') {
		++parser->m_Ptr;
	}

	if (parserDone(parser) || !parserExpectingString(parser, "</text>", 7)) {
		return false;
	}

	const uint32_t txtLen = (uint32_t)(parser->m_Ptr - txtPtr);
	text->m_Text.m_String = (char*)BX_ALLOC(s_Allocator, sizeof(char) * (txtLen + 1));
	bx::memCopy(text->m_Text.m_String, txtPtr, txtLen);
	text->m_Text.m_String[txtLen] = 0;
#endif

	return true;
}

static bool parseShape_Path(ParserState* parser, Shape* path)
{
	bool err = false;
	bool hasContents = false;
	while (!parserDone(parser) && !err) {
		parserSkipWhitespace(parser);
		if (parser->m_Ptr[0] == '>') {
			// NOTE: Don't skip the closing bracket because parserSkipTag() expects it.
			hasContents = true;
		} else if (parser->m_Ptr[0] == '/' && parser->m_Ptr[1] == '>') {
			parser->m_Ptr += 2;
			break;
		}

		bx::StringView name, value;
		if (!parserGetAttribute(parser, &name, &value)) {
			err = true;
		} else {
			// Check if this a generic attribute (i.e. styling)
			ParseAttr::Result res = parseGenericShapeAttribute(name, value, path->m_Attrs);
			if (res == ParseAttr::Fail) {
				err = true;
			} else if (res == ParseAttr::Unknown) {
				// Path specific attributes.
				if (!bx::strCmp(name, "d", 1)) {
					err = !pathFromString(&path->m_Path, value, parser->m_Flags);
				} else {
					SSVG_WARN(false, "Ignoring path attribute: %.*s=\"%.*s\"", name.getLength(), name.getPtr(), value.getLength(), value.getPtr());
				}
			}
		}
	}

	if (hasContents) {
		parserSkipTag(parser);
	}

	return !err;
}

static bool parseShape_Rect(ParserState* parser, Shape* rect)
{
	bool err = false;
	bool hasContents = false;
	while (!parserDone(parser) && !err) {
		parserSkipWhitespace(parser);

		if (parser->m_Ptr[0] == '>') {
			// NOTE: Don't skip the closing bracket because parserSkipTag() expects it.
			hasContents = true;
			break;
		} else if (parser->m_Ptr[0] == '/' && parser->m_Ptr[1] == '>') {
			parser->m_Ptr += 2;
			break;
		}

		bx::StringView name, value;
		if (!parserGetAttribute(parser, &name, &value)) {
			err = true;
		} else {
			// Check if this a generic attribute (i.e. styling)
			ParseAttr::Result res = parseGenericShapeAttribute(name, value, rect->m_Attrs);
			if (res == ParseAttr::Fail) {
				err = true;
			} else if (res == ParseAttr::Unknown) {
				// Rect specific attributes.
				if (!bx::strCmp(name, "width", 5)) {
					err = !parseLength(value, &rect->m_Rect.width);
				} else if (!bx::strCmp(name, "height", 6)) {
					err = !parseLength(value, &rect->m_Rect.height);
				} else if (!bx::strCmp(name, "rx", 2)) {
					err = !parseLength(value, &rect->m_Rect.rx);
				} else if (!bx::strCmp(name, "ry", 2)) {
					err = !parseLength(value, &rect->m_Rect.ry);
				} else if (!bx::strCmp(name, "x", 1)) {
					err = !parseLength(value, &rect->m_Rect.x);
				} else if (!bx::strCmp(name, "y", 1)) {
					err = !parseLength(value, &rect->m_Rect.y);
				} else {
					SSVG_WARN(false, "Ignoring rect attribute: %.*s=\"%.*s\"", name.getLength(), name.getPtr(), value.getLength(), value.getPtr());
				}
			}
		}
	}

	if (hasContents) {
		parserSkipTag(parser);
	}

	return !err;
}

static bool parseShape_Circle(ParserState* parser, Shape* circle)
{
	bool err = false;
	bool hasContents = false;
	while (!parserDone(parser) && !err) {
		parserSkipWhitespace(parser);

		if (parser->m_Ptr[0] == '>') {
			// NOTE: Don't skip the closing bracket because parserSkipTag() expects it.
			hasContents = true;
			break;
		} else if (parser->m_Ptr[0] == '/' && parser->m_Ptr[1] == '>') {
			parser->m_Ptr += 2;
			break;
		}

		bx::StringView name, value;
		if (!parserGetAttribute(parser, &name, &value)) {
			err = true;
		} else {
			// Check if this a generic attribute (i.e. styling)
			ParseAttr::Result res = parseGenericShapeAttribute(name, value, circle->m_Attrs);
			if (res == ParseAttr::Fail) {
				err = true;
			} else if (res == ParseAttr::Unknown) {
				// Circle specific attributes.
				if (!bx::strCmp(name, "cx", 2)) {
					err = !parseLength(value, &circle->m_Circle.cx);
				} else if (!bx::strCmp(name, "cy", 2)) {
					err = !parseLength(value, &circle->m_Circle.cy);
				} else if (!bx::strCmp(name, "r", 1)) {
					err = !parseLength(value, &circle->m_Circle.r);
				} else {
					SSVG_WARN(false, "Ignoring circle attribute: %.*s=\"%.*s\"", name.getLength(), name.getPtr(), value.getLength(), value.getPtr());
				}
			}
		}
	}

	if (hasContents) {
		parserSkipTag(parser);
	}

	return !err;
}

static bool parseShape_Line(ParserState* parser, Shape* line)
{
	bool err = false;
	bool hasContents = false;
	while (!parserDone(parser) && !err) {
		parserSkipWhitespace(parser);

		if (parser->m_Ptr[0] == '>') {
			// NOTE: Don't skip the closing bracket because parserSkipTag() expects it.
			hasContents = true;
			break;
		} else if (parser->m_Ptr[0] == '/' && parser->m_Ptr[1] == '>') {
			parser->m_Ptr += 2;
			break;
		}

		bx::StringView name, value;
		if (!parserGetAttribute(parser, &name, &value)) {
			err = true;
		} else {
			// Check if this a generic attribute (i.e. styling)
			ParseAttr::Result res = parseGenericShapeAttribute(name, value, line->m_Attrs);
			if (res == ParseAttr::Fail) {
				err = true;
			} else if (res == ParseAttr::Unknown) {
				// Line specific attributes.
				if (!bx::strCmp(name, "x1", 2)) {
					err = !parseLength(value, &line->m_Line.x1);
				} else if (!bx::strCmp(name, "x2", 2)) {
					err = !parseLength(value, &line->m_Line.x2);
				} else if (!bx::strCmp(name, "y1", 2)) {
					err = !parseLength(value, &line->m_Line.y1);
				} else if (!bx::strCmp(name, "y2", 2)) {
					err = !parseLength(value, &line->m_Line.y2);
				} else {
					SSVG_WARN(false, "Ignoring line attribute: %.*s=\"%.*s\"", name.getLength(), name.getPtr(), value.getLength(), value.getPtr());
				}
			}
		}
	}

	if (hasContents) {
		parserSkipTag(parser);
	}

	return !err;
}

static bool parseShape_Ellipse(ParserState* parser, Shape* ellipse)
{
	bool err = false;
	bool hasContents = false;
	while (!parserDone(parser) && !err) {
		parserSkipWhitespace(parser);

		if (parser->m_Ptr[0] == '>') {
			// NOTE: Don't skip the closing bracket because parserSkipTag() expects it.
			hasContents = true;
			break;
		} else if (parser->m_Ptr[0] == '/' && parser->m_Ptr[1] == '>') {
			parser->m_Ptr += 2;
			break;
		}

		bx::StringView name, value;
		if (!parserGetAttribute(parser, &name, &value)) {
			err = true;
		} else {
			// Check if this a generic attribute (i.e. styling)
			ParseAttr::Result res = parseGenericShapeAttribute(name, value, ellipse->m_Attrs);
			if (res == ParseAttr::Fail) {
				err = true;
			} else if (res == ParseAttr::Unknown) {
				// Ellipse specific attributes.
				if (!bx::strCmp(name, "cx", 2)) {
					err = !parseLength(value, &ellipse->m_Ellipse.cx);
				} else if (!bx::strCmp(name, "cy", 2)) {
					err = !parseLength(value, &ellipse->m_Ellipse.cy);
				} else if (!bx::strCmp(name, "rx", 2)) {
					err = !parseLength(value, &ellipse->m_Ellipse.rx);
				} else if (!bx::strCmp(name, "ry", 2)) {
					err = !parseLength(value, &ellipse->m_Ellipse.ry);
				} else {
					SSVG_WARN(false, "Ignoring ellipse attribute: %.*s=\"%.*s\"", name.getLength(), name.getPtr(), value.getLength(), value.getPtr());
				}
			}
		}
	}

	if (hasContents) {
		parserSkipTag(parser);
	}

	return !err;
}

static bool parseShape_PointList(ParserState* parser, Shape* shape)
{
	bool err = false;
	bool hasContents = false;
	while (!parserDone(parser) && !err) {
		parserSkipWhitespace(parser);

		if (parser->m_Ptr[0] == '>') {
			// NOTE: Don't skip the closing bracket because parserSkipTag() expects it.
			hasContents = true;
			break;
		} else if (parser->m_Ptr[0] == '/' && parser->m_Ptr[1] == '>') {
			parser->m_Ptr += 2;
			break;
		}

		bx::StringView name, value;
		if (!parserGetAttribute(parser, &name, &value)) {
			err = true;
		} else {
			// Check if this a generic attribute (i.e. styling)
			ParseAttr::Result res = parseGenericShapeAttribute(name, value, shape->m_Attrs);
			if (res == ParseAttr::Fail) {
				err = true;
			} else if (res == ParseAttr::Unknown) {
				if (!bx::strCmp(name, "points", 6)) {
					PointList ptList;
					bx::memSet(&ptList, 0, sizeof(PointList));
					err = !pointListFromString(&ptList, value);

					if (!err && ptList.m_NumPoints >= 2 &&
						((shape->m_Type == ShapeType::Polygon && (parser->m_Flags & ImageLoadFlags::ConvertPolygonsToPaths) != 0) ||
						(shape->m_Type == ShapeType::Polyline && (parser->m_Flags & ImageLoadFlags::ConvertPolylinesToPaths) != 0))) 
					{
						const float* coords = ptList.m_Coords;

						Path* path = &shape->m_Path;
						PathCmd* cmd = pathAllocCommand(path, PathCmdType::MoveTo);
						cmd->m_Data[0] = *coords++;
						cmd->m_Data[1] = *coords++;

						for (uint32_t i = 1; i < ptList.m_NumPoints; ++i) {
							cmd = pathAllocCommand(path, PathCmdType::LineTo);
							cmd->m_Data[0] = *coords++;
							cmd->m_Data[1] = *coords++;
						}

						if (shape->m_Type == ShapeType::Polygon) {
							pathAllocCommand(path, PathCmdType::ClosePath);
						}

						pointListFree(&ptList);
						shape->m_Type = ShapeType::Path;
					} else {
						bx::memCopy(&shape->m_PointList, &ptList, sizeof(PointList));
					}
				} else {
					SSVG_WARN(false, "Ignoring polygon/polyline attribute: %.*s=\"%.*s\"", name.getLength(), name.getPtr(), value.getLength(), value.getPtr());
				}
			}
		}
	}

	if (hasContents) {
		parserSkipTag(parser);
	}

	return !err;
}

static bool parseShapes(ParserState* parser, ShapeList* shapeList, const ShapeAttributes* parentAttrs, const char* closingTag, uint32_t closingTagLen)
{
	struct ParseFunc
	{
		bx::StringView tag;
		ShapeType::Enum type;
		bool(*parseFunc)(ParserState*, Shape*);
	};

	static const ParseFunc parseFuncs[] = {
		{ bx::StringView("polyline"), ShapeType::Polyline, parseShape_PointList },
		{ bx::StringView("polygon"), ShapeType::Polygon, parseShape_PointList },
		{ bx::StringView("ellipse"), ShapeType::Ellipse, parseShape_Ellipse },
		{ bx::StringView("circle"), ShapeType::Circle, parseShape_Circle },
		{ bx::StringView("line"), ShapeType::Line, parseShape_Line },
		{ bx::StringView("rect"), ShapeType::Rect, parseShape_Rect },
		{ bx::StringView("path"), ShapeType::Path, parseShape_Path },
		{ bx::StringView("text"), ShapeType::Text, parseShape_Text },
		{ bx::StringView("g"), ShapeType::Group, parseShape_Group }
	};
	static const uint32_t numParseFuncs = BX_COUNTOF(parseFuncs);

	SSVG_WARN(numParseFuncs == ShapeType::NumTypes, "Some shapes won't be parsed");

	bool err = false;

	// Parse until the end-of-buffer
	while (!parserDone(parser)) {
		if (parserMatchString(parser, closingTag, closingTagLen)) {
			break;
		}

		bx::StringView tag;
		if (!parserGetTag(parser, &tag)) {
			err = true;
			break;
		}

		bool found = false;
		for (uint32_t i = 0; i < numParseFuncs; ++i) {
			if (!bx::strCmp(tag, parseFuncs[i].tag, parseFuncs[i].tag.getLength())) {
				Shape* shape = shapeListAllocShape(shapeList, parseFuncs[i].type, parentAttrs);
				SSVG_CHECK(shape != nullptr, "Shape allocation failed");

				err = !parseFuncs[i].parseFunc(parser, shape);
				found = true;
				break;
			}
		}

		if (err) {
			break;
		}

		if (!found) {
			SSVG_WARN(false, "Ignoring element %.*s", tag.getLength(), tag.getPtr());
			parserSkipTag(parser);
		}
	}

	if (err || parserDone(parser)) {
		return false;
	}

	shapeListShrinkToFit(shapeList);

	// Skip the closing tag.
	return parserExpectingString(parser, closingTag, closingTagLen);
}

static bool parseTag_svg(ParserState* parser, Image* img)
{
	// Parse svg tag attributes...
	bool err = false;
	while (!parserDone(parser) && !err) {
		if (parserExpectingChar(parser, '>')) {
			break;
		}

		bx::StringView name, value;
		if (!parserGetAttribute(parser, &name, &value)) {
			err = true;
		} else {
			if (!bx::strCmp(name, "version", 7)) {
				parseVersion(value, &img->m_VerMajor, &img->m_VerMinor);
			} else if (!bx::strCmp(name, "baseProfile", 11)) {
				if (!bx::strCmp(value, "full", 4)) {
					img->m_BaseProfile = BaseProfile::Full;
				} else if (!bx::strCmp(value, "basic", 5)) {
					img->m_BaseProfile = BaseProfile::Basic;
				} else if (!bx::strCmp(value, "tiny", 4)) {
					img->m_BaseProfile = BaseProfile::Tiny;
				} else {
					// Unknown base profile. Ignore.
					SSVG_WARN(false, "Unknown baseProfile \"%.*s\"", value.getLength(), value.getPtr());
				}
			} else if (!bx::strCmp(name, "width", 5)) {
				img->m_Width = (float)atof(value.getPtr());
			} else if (!bx::strCmp(name, "height", 6)) {
				img->m_Height = (float)atof(value.getPtr());
			} else if (!bx::strCmp(name, "viewBox", 7)) {
				parseViewBox(value, &img->m_ViewBox[0]);
			} else if (!bx::strCmp(name, "xmlns", 5)) {
				// Ignore. This is here in order to shut up the trace message below.
			} else {
				// Unknown attribute. Ignore it (parser has already moved forward)
				SSVG_WARN(false, "Ignoring svg attribute: %.*s=\"%.*s\"", name.getLength(), name.getPtr(), value.getLength(), value.getPtr());
			}
		}
	}

	if (err) {
		return false;
	}

	return parseShapes(parser, &img->m_ShapeList, &img->m_BaseAttrs, "</svg>", 6);
}

Image* imageLoad(const char* xmlStr, uint32_t flags, const ShapeAttributes* baseAttrs)
{
	if (!xmlStr || *xmlStr == 0) {
		return nullptr;
	}

	Image* img = imageCreate(baseAttrs);

	ParserState parser;
	parser.m_XMLString = xmlStr;
	parser.m_Ptr = xmlStr;
	parser.m_Flags = flags;

	bool err = false;
	while (!parserDone(&parser) && !err) {
		bx::StringView tag;
		if (!parserGetTag(&parser, &tag)) {
			err = true;
		} else {
			if (!bx::strCmp(tag, "?xml", 4)) {
				// Special case: Search for "?>".
				while (!parserDone(&parser)) {
					if (parser.m_Ptr[0] == '?' && parser.m_Ptr[1] == '>') {
						parser.m_Ptr += 2;
						break;
					}
					++parser.m_Ptr;
				}

				err = parserDone(&parser);
			} else if (!bx::strCmp(tag, "!DOCTYPE", 8)) {
				// Special case: Search for first '>'.
				while (!parserDone(&parser)) {
					char ch = *parser.m_Ptr++;
					if (ch == '>') {
						break;
					}
				}

				err = parserDone(&parser);
			} else if (!bx::strCmp(tag, "svg", 3)) {
				err = !parseTag_svg(&parser, img);
				if (!err && (parser.m_Flags & ImageLoadFlags::CalcShapeBounds) != 0) {
					shapeListCalcBounds(&img->m_ShapeList, &img->m_BoundingRect[0]);
				}
			} else {
				SSVG_WARN(false, "Ignoring unknown root tag %.*s", tag.getLength(), tag.getPtr());
				parserSkipTag(&parser);
			}
		}
	}

	if (err) {
		imageDestroy(img);
		img = nullptr;
	}

	return img;
}
}