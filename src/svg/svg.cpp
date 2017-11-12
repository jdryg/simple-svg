#include "svg.h"
#include <bx/string.h>
#include <bx/math.h>
#include <float.h> // FLT_MAX

namespace ssvg
{
static bx::AllocatorI* s_Allocator = nullptr;
static ShapeAttributes s_DefaultAttrs;

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
};

static bool parseShapes(ParserState* parser, ShapeList* shapeList, const ShapeAttributes* parentAttrs, const char* closingTag, uint32_t closingTagLen);

inline uint8_t charToNibble(char ch)
{
	if (ch >= '0' && ch <= '9') {
		return ch - '0';
	} else if (ch >= 'a' && ch <= 'f') {
		return 10 + (ch - 'a');
	} else if (ch >= 'A' && ch <= 'F') {
		return 10 + (ch - 'A');
	}

	SVG_WARN(false, "Invalid hex char %c", ch);

	return 0;
}

void transformIdentity(float* transform)
{
	bx::memSet(transform, 0, sizeof(float) * 6);
	transform[0] = 1.0f;
	transform[3] = 1.0f;
}

// a = a * b;
void transformMultiply(float* a, const float* b)
{
	float res[6];
	res[0] = a[0] * b[0] + a[2] * b[1];
	res[1] = a[1] * b[0] + a[3] * b[1];
	res[2] = a[0] * b[2] + a[2] * b[3];
	res[3] = a[1] * b[2] + a[3] * b[3];
	res[4] = a[0] * b[4] + a[2] * b[5] + a[4];
	res[5] = a[1] * b[4] + a[3] * b[5] + a[5];
	bx::memCopy(a, res, sizeof(float) * 6);
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

Shape* shapeListAllocShape(ShapeList* shapeList, ShapeType::Enum type, const ShapeAttributes* parentAttrs)
{
	SVG_CHECK(shapeList->m_NumShapes <= shapeList->m_Capacity, "Trying to expand a read-only shape list?");

	if (shapeList->m_NumShapes + 1 > shapeList->m_Capacity) {
		const uint32_t oldCapacity = shapeList->m_Capacity;

		// TODO: Since shapes are faily large objects, check if allocating a constant amount each time
		// somehow helps.
		shapeList->m_Capacity = oldCapacity ? (oldCapacity * 3) / 2 : 4;
		shapeList->m_Shapes = (Shape*)BX_REALLOC(s_Allocator, shapeList->m_Shapes, sizeof(Shape) * shapeList->m_Capacity);
		bx::memSet(&shapeList->m_Shapes[oldCapacity], 0, sizeof(Shape) * (shapeList->m_Capacity - oldCapacity));
	}

	Shape* shape = &shapeList->m_Shapes[shapeList->m_NumShapes++];
	shape->m_Type = type;
	
	// Copy parent attributes (except from id and transform)
	if (parentAttrs) {
		bx::memCopy(&shape->m_Attrs, parentAttrs, sizeof(ShapeAttributes));
		shape->m_Attrs.m_ID[0] = '\0';
		transformIdentity(&shape->m_Attrs.m_Transform[0]);
	}

	return shape;
}

void shapeListShrinkToFit(ShapeList* shapeList)
{
	SVG_CHECK(shapeList->m_NumShapes <= shapeList->m_Capacity, "Trying to shrink a read-only shape list?");

	if (!shapeList->m_NumShapes && shapeList->m_Capacity) {
		BX_FREE(s_Allocator, shapeList->m_Shapes);
		shapeList->m_Shapes = nullptr;
		shapeList->m_Capacity = 0;
	} else if (shapeList->m_NumShapes != shapeList->m_Capacity) {
		shapeList->m_Shapes = (Shape*)BX_REALLOC(s_Allocator, shapeList->m_Shapes, sizeof(Shape) * shapeList->m_NumShapes);
		shapeList->m_Capacity = shapeList->m_NumShapes;
	}
}

void shapeListFree(ShapeList* shapeList)
{
	SVG_CHECK(shapeList->m_NumShapes <= shapeList->m_Capacity, "Trying to free a read-only shape list?");

	BX_FREE(s_Allocator, shapeList->m_Shapes);
	shapeList->m_Shapes = nullptr;
	shapeList->m_Capacity = 0;
	shapeList->m_NumShapes = 0;
}

void shapeListReserve(ShapeList* shapeList, uint32_t capacity)
{
	const uint32_t oldCapacity = shapeList->m_Capacity;
	if (oldCapacity <= capacity) {
		return;
	}

	shapeList->m_Capacity = capacity;
	shapeList->m_Shapes = (Shape*)BX_REALLOC(s_Allocator, shapeList->m_Shapes, sizeof(Shape) * shapeList->m_Capacity);
	bx::memSet(&shapeList->m_Shapes[oldCapacity], 0, sizeof(Shape) * (shapeList->m_Capacity - oldCapacity));
}

PathCmd* pathAllocCommands(Path* path, uint32_t n)
{
	if (path->m_NumCommands + n > path->m_Capacity) {
		const uint32_t oldCapacity = path->m_Capacity;
		const uint32_t newCapacity = oldCapacity ? (oldCapacity * 3) / 2 : 4;

		path->m_Capacity = bx::max<uint32_t>(newCapacity, oldCapacity + n);
		path->m_Commands = (PathCmd*)BX_REALLOC(s_Allocator, path->m_Commands, sizeof(PathCmd) * path->m_Capacity);
		bx::memSet(&path->m_Commands[oldCapacity], 0, sizeof(PathCmd) * (path->m_Capacity - oldCapacity));
	}

	PathCmd* firstCmd = &path->m_Commands[path->m_NumCommands];
	path->m_NumCommands += n;

	return firstCmd;
}

PathCmd* pathAllocCommand(Path* path, PathCmdType::Enum type)
{
	PathCmd* cmd = pathAllocCommands(path, 1);
	cmd->m_Type = type;

	return cmd;
}

void pathShrinkToFit(Path* path)
{
	if (!path->m_NumCommands && path->m_Capacity) {
		BX_FREE(s_Allocator, path->m_Commands);
		path->m_Commands = nullptr;
		path->m_Capacity = 0;
	} else if (path->m_NumCommands != path->m_Capacity) {
		path->m_Commands = (PathCmd*)BX_REALLOC(s_Allocator, path->m_Commands, sizeof(PathCmd) * path->m_NumCommands);
		path->m_Capacity = path->m_NumCommands;
	}
}

void pathFree(Path* path)
{
	BX_FREE(s_Allocator, path->m_Commands);
	path->m_Commands = nullptr;
	path->m_NumCommands = 0;
	path->m_Capacity = 0;
}

float* pointListAllocPoints(PointList* ptList, uint32_t n)
{
	SVG_CHECK(n != 0, "Requested invalid number of points");

	if (ptList->m_NumPoints + n > ptList->m_Capacity) {
		const uint32_t oldCapacity = ptList->m_Capacity;
		const uint32_t newCapacity = oldCapacity ? (oldCapacity * 3) / 2 : 8;

		ptList->m_Capacity = bx::max<uint32_t>(newCapacity, oldCapacity + n);
		ptList->m_Coords = (float*)BX_REALLOC(s_Allocator, ptList->m_Coords, sizeof(float) * 2 * ptList->m_Capacity);
	}

	float* coords = &ptList->m_Coords[ptList->m_NumPoints << 1];
	ptList->m_NumPoints += n;

	return coords;
}

void pointListShrinkToFit(PointList* ptList)
{
	if (!ptList->m_NumPoints && ptList->m_Capacity) {
		BX_FREE(s_Allocator, ptList->m_Coords);
		ptList->m_Coords = nullptr;
		ptList->m_Capacity = 0;
	} else if (ptList->m_NumPoints != ptList->m_Capacity) {
		ptList->m_Coords = (float*)BX_REALLOC(s_Allocator, ptList->m_Coords, sizeof(float) * 2 * ptList->m_NumPoints);
		ptList->m_Capacity = ptList->m_NumPoints;
	}
}

void pointListFree(PointList* ptList)
{
	BX_FREE(s_Allocator, ptList->m_Coords);
	ptList->m_Coords = 0;
	ptList->m_NumPoints = 0;
	ptList->m_Capacity = 0;
}

void shapeAttrsSetID(ShapeAttributes* attrs, const bx::StringView& value)
{
	uint32_t maxLen = bx::min<uint32_t>(SSVG_CONFIG_ID_MAX_LEN - 1, value.getLength());
	SVG_WARN((int32_t)maxLen >= value.getLength(), "id \"%.*s\" truncated to %d characters", value.getLength(), value.getPtr(), maxLen);
	bx::memCopy(&attrs->m_ID[0], value.getPtr(), maxLen);
	attrs->m_ID[maxLen] = '\0';
}

void shapeAttrsSetFontFamily(ShapeAttributes* attrs, const bx::StringView& value)
{
	uint32_t maxLen = bx::min<uint32_t>(SSVG_CONFIG_FONT_FAMILY_MAX_LEN - 1, value.getLength());
	SVG_WARN((int32_t)maxLen >= value.getLength(), "font-family \"%.*s\" truncated to %d characters", value.getLength(), value.getPtr(), maxLen);
	bx::memCopy(&attrs->m_FontFamily[0], value.getPtr(), maxLen);
	attrs->m_FontFamily[maxLen] = '\0';
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
			SVG_CHECK(numOpenBrackets != 0, "Unbalanced brackets");
			parser->m_Ptr++;
			--numOpenBrackets;
		} else if (ch == '>') {
			SVG_CHECK(numOpenBrackets != 0, "Unbalanced brackets");
			--numOpenBrackets;
			if (incLevelOnClose) {
				++level;
			}
			incLevelOnClose = true;
		} else if (ch == '<') {
			++numOpenBrackets;
			if (*parser->m_Ptr == '/') {
				SVG_CHECK(level != 0, "Unbalanched tags");
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
	while (bx::isAlpha(*parser->m_Ptr) 
		|| *parser->m_Ptr == '-' 
		|| *parser->m_Ptr == '_'
		|| *parser->m_Ptr == ':') 
	{
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
	*maj = (uint16_t)bx::ffloor(fver);
	*min = (uint16_t)bx::ffloor((fver - *maj) * 10.0f);

	return true;
}

static bool parseNumber(const bx::StringView& str, float* val, float min = -FLT_MAX, float max = FLT_MAX)
{
	*val = bx::fclamp((float)atof(str.getPtr()), min, max);

	return true;
}

static bool parseLength(ParserState* parser, const bx::StringView& str, float* len)
{
	// TODO: Parse length units and convert to pixels based on parser state.
	BX_UNUSED(parser);
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
				SVG_WARN(false, "Unknown hex color format %.*s", str.getLength(), str.getPtr());
			}
		} else {
			SVG_WARN(false, "Unhandled paint value: %.*s", str.getLength(), str.getPtr());
		}
	}

	return true;
}

// TODO: More tests!
static const char* parseCoord(const char* str, const char* end, float* coord)
{
	const char* ptr = skipCommaWhitespace(str, end);

	SVG_CHECK(ptr != end && !bx::isAlpha(*ptr), "Parse error");

	*coord = (float)atof(ptr);

	// Skip optional sign
	if (*ptr == '-' || *ptr == '+') {
		++ptr;
	}

	// Skip number
	bool dotFound = false;
	bool expFound = false;
	BX_UNUSED(dotFound); // Used only in debug mode.
	while (ptr != end) {
		const char ch = *ptr;
		if (!bx::isNumeric(ch)) {
			if (ch == '.') {
				SVG_CHECK(!dotFound, "Parse error: Multiple decimal places in coord!");
				dotFound = true;
			} else if (ch == 'e') {
				expFound = true;
				++ptr;
				break;
			} else {
				break;
			}
		}

		++ptr;
	}

	if (expFound) {
		// Parse the exponent.
		// Skip optional sign
		if (*ptr == '-' || *ptr == '+') {
			++ptr;
		}

		while (ptr != end && bx::isNumeric(*ptr)) {
			++ptr;
		}
	}

	return skipCommaWhitespace(ptr, end);
}

static const char* parseTransformComponent(const char* str, const char* end, bx::StringView* type, bx::StringView* value)
{
	SVG_CHECK(bx::isAlpha(*str), "Parse error: Excepted identifier");

	const char* ptr = str;
	while (ptr != end && bx::isAlpha(*ptr)) {
		++ptr;
	}

	if (ptr == end) {
		SVG_CHECK(false, "Parse error: Transformation component ended early");
		return nullptr;
	}

	type->set(str, (int32_t)(ptr - str));

	ptr = skipWhitespace(ptr, end);
	
	if (*ptr != '(') {
		SVG_CHECK(false, "Parse error: Expected '('");
		return nullptr;
	}

	ptr = skipWhitespace(ptr + 1, end);

	const char* valuePtr = ptr;

	// Skip until closing parenthesis
	while (ptr != end && *ptr != ')') {
		++ptr;
	}

	if (ptr == end) {
		SVG_CHECK(false, "Parse error: Couldn't find closing parenthesis");
		return nullptr;
	}
	SVG_CHECK(*ptr == ')', "Parse error: Expected ')'");

	const char* endPtr = ptr + 1;

	// Walk back ptr to get a tight value string (without trailing whitespaces)
	while (ptr != valuePtr && bx::isSpace(*ptr)) {
		--ptr;
	}

	value->set(valuePtr, (int32_t)(ptr - valuePtr));

	return endPtr;
}

static bool parseTransform(ParserState* parser, const bx::StringView& str, float* transform)
{
	BX_UNUSED(parser);

	const char* ptr = str.getPtr();
	const char* end = str.getTerm();

	transformIdentity(transform);
	ptr = skipWhitespace(ptr, end);
	while (ptr != end) {
		bx::StringView type, value;
		
		ptr = parseTransformComponent(ptr, end, &type, &value);
		if (!ptr) {
			SVG_CHECK(false, "Parse error");
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
			const float cosAngle = bx::fcos(angle_rad);
			const float sinAngle = bx::fsin(angle_rad);
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
			comp[2] = bx::ftan(angle_rad);
		} else if (!bx::strCmp(type, "skewY", 5)) {
			float angle_deg;
			valuePtr = parseCoord(valuePtr, valueEnd, &angle_deg);

			const float angle_rad = bx::toRad(angle_deg);
			comp[1] = bx::ftan(angle_rad);
		} else {
			SVG_WARN(false, "Unknown transform component %.*s(%.*s)", type.getLength(), type.getPtr(), value.getLength(), value.getPtr());
			valuePtr = valueEnd;
		}

		SVG_WARN(valuePtr == valueEnd, "Incomplete transformation parsing");

		transformMultiply(transform, comp);
	}

	return true;
}

bool pathFromString(Path* path, const bx::StringView& str)
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

		SVG_CHECK(!bx::isSpace(ch) && ch != ',', "Parse error");

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
		} else if (lch == 'a') {
			// ArcTo abs
			PathCmd* cmd = pathAllocCommand(path, PathCmdType::ArcTo);
			ptr = parseCoord(ptr, end, &cmd->m_Data[0]);
			ptr = parseCoord(ptr, end, &cmd->m_Data[1]);
			ptr = parseCoord(ptr, end, &cmd->m_Data[2]);
			ptr = parseCoord(ptr, end, &cmd->m_Data[3]);
			ptr = parseCoord(ptr, end, &cmd->m_Data[4]);
			ptr = parseCoord(ptr, end, &cmd->m_Data[5]);
			ptr = parseCoord(ptr, end, &cmd->m_Data[6]);

			if (ch == lch) {
				cmd->m_Data[5] += lastX;
				cmd->m_Data[6] += lastY;
			}

			lastX = cmd->m_Data[5];
			lastY = cmd->m_Data[6];
		} else {
			SVG_WARN(false, "Encountered unknown path command");
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

static ParseAttr::Result parseGenericShapeAttribute(ParserState* parser, const bx::StringView& name, const bx::StringView& value, ShapeAttributes* attrs)
{
	SVG_WARN(bx::strCmp(name, "style", 5), "style attribute not supported");

	if (!bx::strCmp(name, "stroke", 6)) {
		const bx::StringView partialName(name.getPtr() + 6, name.getLength() - 6);
		if (partialName.getLength() == 0) {
			return parsePaint(value, &attrs->m_StrokePaint) ? ParseAttr::OK : ParseAttr::Fail;
		} else if (!bx::strCmp(partialName, "-miterlimit", 11)) {
			return parseNumber(value, &attrs->m_StrokeMiterLimit, 1.0f) ? ParseAttr::OK : ParseAttr::Fail;
		} else if (!bx::strCmp(partialName, "-linejoin", 9)) {
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
			return parseNumber(value, &attrs->m_StrokeOpacity, 0.0f, 1.0f) ? ParseAttr::OK : ParseAttr::Fail;
		} else if (!bx::strCmp(partialName, "-width", 6)) {
			return parseLength(parser, value, &attrs->m_StrokeWidth) ? ParseAttr::OK : ParseAttr::Fail;
		}
	} else if (!bx::strCmp(name, "fill", 4)) {
		const bx::StringView partialName(name.getPtr() + 4, name.getLength() - 4);
		if (partialName.getLength() == 0) {
			return parsePaint(value, &attrs->m_FillPaint) ? ParseAttr::OK : ParseAttr::Fail;
		} else if (!bx::strCmp(partialName, "-opacity", 8)) {
			return parseNumber(value, &attrs->m_FillOpacity, 0.0f, 1.0f) ? ParseAttr::OK : ParseAttr::Fail;
		}
	} else if (!bx::strCmp(name, "font", 4)) {
		const bx::StringView partialName(name.getPtr() + 4, name.getLength() - 4);
		if (!bx::strCmp(partialName, "-family", 7)) {
			shapeAttrsSetFontFamily(attrs, value);
			return ParseAttr::OK;
		} else if (!bx::strCmp(partialName, "-size", 5)) {
			return parseLength(parser, value, &attrs->m_FontSize) ? ParseAttr::OK : ParseAttr::Fail;
		}
	} else if (!bx::strCmp(name, "transform", 9)) {
		return parseTransform(parser, value, &attrs->m_Transform[0]) ? ParseAttr::OK : ParseAttr::Fail;
	} else if (!bx::strCmp(name, "id", 2)) {
		shapeAttrsSetID(attrs, value);
		return ParseAttr::OK;
	}

	return ParseAttr::Unknown;
}

static bool parseShape_Group(ParserState* parser, Shape* group)
{
	bool err = false;
	while (!parserDone(parser) && !err) {
		SVG_CHECK(!(parser->m_Ptr[0] == '/' && parser->m_Ptr[1] == '>'), "Empty group element");
		if (parserExpectingChar(parser, '>')) {
			break;
		}

		bx::StringView name, value;
		if (!parserGetAttribute(parser, &name, &value)) {
			err = true;
		} else {
			// Check if this a generic attribute (i.e. styling)
			ParseAttr::Result res = parseGenericShapeAttribute(parser, name, value, &group->m_Attrs);
			if (res == ParseAttr::Fail) {
				err = true;
			} else if (res == ParseAttr::Unknown) {
				// No specific attributes for groups. Ignore it.
				SVG_WARN(false, "Ignoring g attribute: %.*s=\"%.*s\"", name.getLength(), name.getPtr(), value.getLength(), value.getPtr());
			}
		}
	}

	if (err) {
		return false;
	}

	return parseShapes(parser, &group->m_ShapeList, &group->m_Attrs, "</g>", 4);
}

static bool parseShape_Text(ParserState* parser, Shape* text)
{
	bool err = false;
	while (!parserDone(parser) && !err) {
		SVG_CHECK(!(parser->m_Ptr[0] == '/' && parser->m_Ptr[1] == '>'), "Empty text element");
		if (parserExpectingChar(parser, '>')) {
			break;
		}

		bx::StringView name, value;
		if (!parserGetAttribute(parser, &name, &value)) {
			err = true;
		} else {
			// Check if this a generic attribute (i.e. styling)
			ParseAttr::Result res = parseGenericShapeAttribute(parser, name, value, &text->m_Attrs);
			if (res == ParseAttr::Fail) {
				err = true;
			} else if (res == ParseAttr::Unknown) {
				// Text specific attributes
				if (!bx::strCmp(name, "x", 1)) {
					err = !parseLength(parser, value, &text->m_Text.x);
				} else if (!bx::strCmp(name, "y", 1)) {
					err = !parseLength(parser, value, &text->m_Text.y);
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
					SVG_WARN(false, "Ignoring text attribute: %.*s=\"%.*s\"", name.getLength(), name.getPtr(), value.getLength(), value.getPtr());
				}
			}
		}
	}

	if (err || parserDone(parser)) {
		return false;
	}

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
			ParseAttr::Result res = parseGenericShapeAttribute(parser, name, value, &path->m_Attrs);
			if (res == ParseAttr::Fail) {
				err = true;
			} else if (res == ParseAttr::Unknown) {
				// Path specific attributes.
				if (!bx::strCmp(name, "d", 1)) {
					err = !pathFromString(&path->m_Path, value);
				} else {
					SVG_WARN(false, "Ignoring path attribute: %.*s=\"%.*s\"", name.getLength(), name.getPtr(), value.getLength(), value.getPtr());
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
			ParseAttr::Result res = parseGenericShapeAttribute(parser, name, value, &rect->m_Attrs);
			if (res == ParseAttr::Fail) {
				err = true;
			} else if (res == ParseAttr::Unknown) {
				// Rect specific attributes.
				if (!bx::strCmp(name, "width", 5)) {
					err = !parseLength(parser, value, &rect->m_Rect.width);
				} else if (!bx::strCmp(name, "height", 6)) {
					err = !parseLength(parser, value, &rect->m_Rect.height);
				} else if (!bx::strCmp(name, "rx", 2)) {
					err = !parseLength(parser, value, &rect->m_Rect.rx);
				} else if (!bx::strCmp(name, "ry", 2)) {
					err = !parseLength(parser, value, &rect->m_Rect.ry);
				} else if (!bx::strCmp(name, "x", 1)) {
					err = !parseLength(parser, value, &rect->m_Rect.x);
				} else if (!bx::strCmp(name, "y", 1)) {
					err = !parseLength(parser, value, &rect->m_Rect.y);
				} else {
					SVG_WARN(false, "Ignoring rect attribute: %.*s=\"%.*s\"", name.getLength(), name.getPtr(), value.getLength(), value.getPtr());
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
			ParseAttr::Result res = parseGenericShapeAttribute(parser, name, value, &circle->m_Attrs);
			if (res == ParseAttr::Fail) {
				err = true;
			} else if (res == ParseAttr::Unknown) {
				// Circle specific attributes.
				if (!bx::strCmp(name, "cx", 2)) {
					err = !parseLength(parser, value, &circle->m_Circle.cx);
				} else if (!bx::strCmp(name, "cy", 2)) {
					err = !parseLength(parser, value, &circle->m_Circle.cy);
				} else if (!bx::strCmp(name, "r", 1)) {
					err = !parseLength(parser, value, &circle->m_Circle.r);
				} else {
					SVG_WARN(false, "Ignoring circle attribute: %.*s=\"%.*s\"", name.getLength(), name.getPtr(), value.getLength(), value.getPtr());
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
			ParseAttr::Result res = parseGenericShapeAttribute(parser, name, value, &line->m_Attrs);
			if (res == ParseAttr::Fail) {
				err = true;
			} else if (res == ParseAttr::Unknown) {
				// Line specific attributes.
				if (!bx::strCmp(name, "x1", 2)) {
					err = !parseLength(parser, value, &line->m_Line.x1);
				} else if (!bx::strCmp(name, "x2", 2)) {
					err = !parseLength(parser, value, &line->m_Line.x2);
				} else if (!bx::strCmp(name, "y1", 2)) {
					err = !parseLength(parser, value, &line->m_Line.y1);
				} else if (!bx::strCmp(name, "y2", 2)) {
					err = !parseLength(parser, value, &line->m_Line.y2);
				} else {
					SVG_WARN(false, "Ignoring line attribute: %.*s=\"%.*s\"", name.getLength(), name.getPtr(), value.getLength(), value.getPtr());
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
			ParseAttr::Result res = parseGenericShapeAttribute(parser, name, value, &ellipse->m_Attrs);
			if (res == ParseAttr::Fail) {
				err = true;
			} else if (res == ParseAttr::Unknown) {
				// Ellipse specific attributes.
				if (!bx::strCmp(name, "cx", 2)) {
					err = !parseLength(parser, value, &ellipse->m_Ellipse.cx);
				} else if (!bx::strCmp(name, "cy", 2)) {
					err = !parseLength(parser, value, &ellipse->m_Ellipse.cy);
				} else if (!bx::strCmp(name, "rx", 2)) {
					err = !parseLength(parser, value, &ellipse->m_Ellipse.rx);
				} else if (!bx::strCmp(name, "ry", 2)) {
					err = !parseLength(parser, value, &ellipse->m_Ellipse.ry);
				} else {
					SVG_WARN(false, "Ignoring ellipse attribute: %.*s=\"%.*s\"", name.getLength(), name.getPtr(), value.getLength(), value.getPtr());
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
			ParseAttr::Result res = parseGenericShapeAttribute(parser, name, value, &shape->m_Attrs);
			if (res == ParseAttr::Fail) {
				err = true;
			} else if (res == ParseAttr::Unknown) {
				// Ellipse specific attributes.
				if (!bx::strCmp(name, "points", 6)) {
					err = !pointListFromString(&shape->m_PointList, value);
				} else {
					SVG_WARN(false, "Ignoring polygon/polyline attribute: %.*s=\"%.*s\"", name.getLength(), name.getPtr(), value.getLength(), value.getPtr());
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

	SVG_WARN(numParseFuncs == ShapeType::NumTypes, "Some shapes won't be parsed");

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
				SVG_CHECK(shape != nullptr, "Shape allocation failed");

				err = !parseFuncs[i].parseFunc(parser, shape);
				found = true;

				break;
			}
		}

		if (err) {
			break;
		}

		if (!found) {
			SVG_WARN(false, "Ignoring element %.*s", tag.getLength(), tag.getPtr());
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
					SVG_WARN(false, "Unknown baseProfile \"%.*s\"", value.getLength(), value.getPtr());
				}
			} else if (!bx::strCmp(name, "width", 5)) {
				img->m_Width = (float)atof(value.getPtr());
			} else if (!bx::strCmp(name, "height", 6)) {
				img->m_Height = (float)atof(value.getPtr());
			} else if (!bx::strCmp(name, "xmlns", 5)) {
				// Ignore. This is here in order to shut up the trace message below.
			} else {
				// Unknown attribute. Ignore it (parser has already moved forward)
				SVG_WARN(false, "Ignoring svg attribute: %.*s=\"%.*s\"", name.getLength(), name.getPtr(), value.getLength(), value.getPtr());
			}
		}
	}

	if (err) {
		return false;
	}

	return parseShapes(parser, &img->m_ShapeList, &s_DefaultAttrs, "</svg>", 6);
}

Image* imageCreate()
{
	Image* img = (Image*)BX_ALLOC(s_Allocator, sizeof(Image));
	bx::memSet(img, 0, sizeof(Image));

	return img;
}

Image* imageLoad(const char* xmlStr)
{
	if (!xmlStr || *xmlStr == 0) {
		return nullptr;
	}

	Image* img = imageCreate();

	ParserState parser;
	parser.m_XMLString = xmlStr;
	parser.m_Ptr = xmlStr;

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
			} else {
				SVG_WARN(false, "Ignoring unknown root tag %.*s", tag.getLength(), tag.getPtr());
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

void destroyShapeList(ShapeList* shapeList)
{
	const uint32_t n = shapeList->m_NumShapes;
	for (uint32_t i = 0; i < n; ++i) {
		Shape* shape = &shapeList->m_Shapes[i];
		switch (shape->m_Type) {
		case ShapeType::Group:
			destroyShapeList(&shape->m_ShapeList);
			break;
		case ShapeType::Path:
			pathFree(&shape->m_Path);
			break;
		case ShapeType::Polygon:
		case ShapeType::Polyline:
			pointListFree(&shape->m_PointList);
			break;
		case ShapeType::Text:
			BX_FREE(s_Allocator, shape->m_Text.m_String);
			break;
		}
	}

	shapeListFree(shapeList);
}

void imageDestroy(Image* img)
{
	destroyShapeList(&img->m_ShapeList);
	BX_FREE(s_Allocator, img);
}

void initLib(bx::AllocatorI* allocator, const ShapeAttributes* defaultAttrs)
{
	s_Allocator = allocator;
	if (defaultAttrs) {
		bx::memCopy(&s_DefaultAttrs, defaultAttrs, sizeof(ShapeAttributes));
	} else {
		bx::memSet(&s_DefaultAttrs, 0, sizeof(ShapeAttributes));
		s_DefaultAttrs.m_StrokeWidth = 1.0f;
		s_DefaultAttrs.m_StrokeMiterLimit = 4.0f;
		s_DefaultAttrs.m_StrokeOpacity = 1.0f;
		s_DefaultAttrs.m_StrokePaint.m_Type = PaintType::None;
		s_DefaultAttrs.m_StrokePaint.m_ColorABGR = 0x00000000; // Transparent
		s_DefaultAttrs.m_StrokeLineCap = LineCap::Butt;
		s_DefaultAttrs.m_StrokeLineJoin = LineJoin::Miter;
		s_DefaultAttrs.m_FillOpacity = 1.0f;
		s_DefaultAttrs.m_FillPaint.m_Type = PaintType::None;
		s_DefaultAttrs.m_FillPaint.m_ColorABGR = 0x00000000;
		transformIdentity(&s_DefaultAttrs.m_Transform[0]);
		shapeAttrsSetFontFamily(&s_DefaultAttrs, "sans-serif");
	}
}

bool shapeCopy(Shape* dst, const Shape* src, bool copyAttrs)
{
	const ShapeType::Enum type = src->m_Type;

	dst->m_Type = type;
	if (copyAttrs) {
		bx::memCopy(&dst->m_Attrs, &src->m_Attrs, sizeof(ShapeAttributes));
	}

	switch (type) {
	case ShapeType::Group:
	{
		const ShapeList* srcShapeList = &src->m_ShapeList;
		const uint32_t numShapes = srcShapeList->m_NumShapes;

		ShapeList* dstShapeList = &dst->m_ShapeList;
		shapeListReserve(dstShapeList, numShapes);

		for (uint32_t i = 0; i < numShapes; ++i) {
			const Shape* srcShape = &srcShapeList->m_Shapes[i];
			Shape* dstShape = shapeListAllocShape(dstShapeList, srcShape->m_Type, nullptr);
			shapeCopy(dstShape, srcShape);
		}
	}
		break;
	case ShapeType::Rect:
		bx::memCopy(&dst->m_Rect, &src->m_Rect, sizeof(Rect));
		break;
	case ShapeType::Circle:
		bx::memCopy(&dst->m_Circle, &src->m_Circle, sizeof(Circle));
		break;
	case ShapeType::Ellipse:
		bx::memCopy(&dst->m_Ellipse, &src->m_Ellipse, sizeof(Ellipse));
		break;
	case ShapeType::Line:
		bx::memCopy(&dst->m_Line, &src->m_Line, sizeof(Line));
		break;
	case ShapeType::Polyline:
	case ShapeType::Polygon:
		bx::memCopy(
			pointListAllocPoints(&dst->m_PointList, src->m_PointList.m_NumPoints), 
			src->m_PointList.m_Coords, 
			sizeof(float) * 2 * src->m_PointList.m_NumPoints);
		break;
	case ShapeType::Path:
	{
		const Path* srcPath = &src->m_Path;
		const uint32_t numCommands = srcPath->m_NumCommands;

		Path* dstPath = &dst->m_Path;
		PathCmd* dstCommands = pathAllocCommands(dstPath, numCommands);
		bx::memCopy(dstCommands, srcPath->m_Commands, sizeof(PathCmd) * numCommands);
	}
		break;
	case ShapeType::Text:
	{
		const Text* srcText = &src->m_Text;
		Text* dstText = &dst->m_Text;

		dstText->x = srcText->x;
		dstText->y = srcText->y;
		dstText->m_Anchor = srcText->m_Anchor;
		
		const uint32_t len = bx::strLen(srcText->m_String);
		dstText->m_String = (char*)BX_ALLOC(s_Allocator, sizeof(char) * (len + 1));
		bx::memCopy(dstText->m_String, srcText->m_String, len);
		dstText->m_String[len] = '\0';
	}
		break;
	}

	return true;
}
} // namespace svg
