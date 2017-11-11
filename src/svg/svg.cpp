#include "svg.h"
#include <bx/string.h>
#include <bx/math.h>
#include <float.h> // FLT_MAX

namespace ssvg
{
static bx::AllocatorI* s_Allocator = nullptr;

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

inline void transformIdentity(float* transform)
{
	bx::memSet(transform, 0, sizeof(float) * 6);
	transform[0] = 1.0f;
	transform[3] = 1.0f;
}

static Shape* shapeListAllocShape(ShapeList* shapeList, ShapeType::Enum type, const ShapeAttributes* parentAttrs)
{
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
	bx::memCopy(&shape->m_Attrs, parentAttrs, sizeof(ShapeAttributes));
	shape->m_Attrs.m_ID[0] = '\0';
	transformIdentity(&shape->m_Attrs.m_Transform[0]);

	return shape;
}

static void shapeListShrinkToFit(ShapeList* shapeList)
{
	if (!shapeList->m_NumShapes && shapeList->m_Capacity) {
		BX_FREE(s_Allocator, shapeList->m_Shapes);
		shapeList->m_Shapes = nullptr;
		shapeList->m_Capacity = 0;
	} else if (shapeList->m_NumShapes != shapeList->m_Capacity) {
		shapeList->m_Shapes = (Shape*)BX_REALLOC(s_Allocator, shapeList->m_Shapes, sizeof(Shape) * shapeList->m_NumShapes);
		shapeList->m_Capacity = shapeList->m_NumShapes;
	}
}

static void shapeListFree(ShapeList* shapeList)
{
	BX_FREE(s_Allocator, shapeList->m_Shapes);
	shapeList->m_Shapes = nullptr;
	shapeList->m_Capacity = 0;
	shapeList->m_NumShapes = 0;
}

static PathCmd* pathAllocCmd(Path* path, PathCmdType::Enum type)
{
	if (path->m_NumCommands + 1 > path->m_Capacity) {
		const uint32_t oldCapacity = path->m_Capacity;

		path->m_Capacity = oldCapacity ? (oldCapacity * 3) / 2 : 4;
		path->m_Commands = (PathCmd*)BX_REALLOC(s_Allocator, path->m_Commands, sizeof(PathCmd) * path->m_Capacity);
		bx::memSet(&path->m_Commands[oldCapacity], 0, sizeof(PathCmd) * (path->m_Capacity - oldCapacity));
	}

	PathCmd* cmd = &path->m_Commands[path->m_NumCommands++];
	cmd->m_Type = type;

	return cmd;
}

static void pathShrinkToFit(Path* path)
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

static void pathFree(Path* path)
{
	BX_FREE(s_Allocator, path->m_Commands);
	path->m_Commands = nullptr;
	path->m_NumCommands = 0;
	path->m_Capacity = 0;
}

static float* pointListAllocPoint(PointList* ptList)
{
	if (ptList->m_NumPoints + 1 > ptList->m_Capacity) {
		const uint32_t oldCapacity = ptList->m_Capacity;

		ptList->m_Capacity = oldCapacity ? (oldCapacity * 3) / 2 : 8;
		ptList->m_Coords = (float*)BX_REALLOC(s_Allocator, ptList->m_Coords, sizeof(float) * 2 * ptList->m_Capacity);
	}

	float* coords = &ptList->m_Coords[ptList->m_NumPoints << 1];
	++ptList->m_NumPoints;

	return coords;
}

static void pointListShrinkToFit(PointList* ptList)
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

static void pointListFree(PointList* ptList)
{
	BX_FREE(s_Allocator, ptList->m_Coords);
	ptList->m_Coords = 0;
	ptList->m_NumPoints = 0;
	ptList->m_Capacity = 0;
}

inline bool parserDone(ParserState* parser)
{
	return *parser->m_Ptr == 0;
}

inline void parserSkipWhitespace(ParserState* parser)
{
	const char* ch = parser->m_Ptr;
	while (bx::isSpace(*ch)) {
		++ch;
	}
	parser->m_Ptr = ch;
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

	return true;
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

static bool parserGetAttribute(ParserState* parser, bx::StringView* name, bx::StringView* value)
{
	parserSkipWhitespace(parser);

	if (!bx::isAlpha(*parser->m_Ptr)) {
		return false;
	}
	
	const char* namePtr = parser->m_Ptr;
	
	// Skip the identifier
	while (bx::isAlpha(*parser->m_Ptr) || *parser->m_Ptr == '-') {
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
		paint->m_Color = 0xFF000000;
		if (*ptr == '#') {
			// Hex color
			if (str.getLength() == 7) {
				const uint8_t r = (charToNibble(ptr[1]) << 4) | charToNibble(ptr[2]);
				const uint8_t g = (charToNibble(ptr[3]) << 4) | charToNibble(ptr[4]);
				const uint8_t b = (charToNibble(ptr[5]) << 4) | charToNibble(ptr[6]);
				paint->m_Color |= ((uint32_t)r) | ((uint32_t)g << 8) | ((uint32_t)b << 16);
			} else if (str.getLength() == 4) {
				const uint8_t r = charToNibble(ptr[1]);
				const uint8_t g = charToNibble(ptr[2]);
				const uint8_t b = charToNibble(ptr[3]);
				const uint32_t rgb = ((uint32_t)r) | ((uint32_t)g << 8) | ((uint32_t)b << 16);
				paint->m_Color |= rgb | (rgb << 4);
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
	const char* ptr = str;

	// Skip leading whitespaces and commas
	while (ptr != end) {
		const char ch = *ptr;
		if (!bx::isSpace(ch) && ch != ',') {
			break;
		}

		++ptr;
	}

	SVG_CHECK(ptr != end && !bx::isAlpha(*ptr), "Parse error");

	*coord = (float)atof(ptr);

	// Skip optional sign
	if (*ptr == '-' || *ptr == '+') {
		++ptr;
	}

	// Skip number
	bool dotFound = false;
	BX_UNUSED(dotFound); // Used only in debug mode.
	while (ptr != end) {
		const char ch = *ptr;
		if (!bx::isNumeric(ch)) {
			if (ch == '.') {
				SVG_CHECK(!dotFound, "Parse error: Multiple decimal places in coord!");
				dotFound = true;
			} else {
				break;
			}
		}

		++ptr;
	}

	// Skip trailing whitespaces and commas
	while (ptr != end) {
		const char ch = *ptr;
		if (!bx::isSpace(ch) && ch != ',') {
			break;
		}

		++ptr;
	}

	return ptr;
}

static bool parseTransform(ParserState* parser, const bx::StringView& str, float* transform)
{
	BX_UNUSED(parser);
	if (!bx::strCmp(str, "matrix", 6)) {
		const char* ptr = str.getPtr() + 6;
		const char* end = str.getTerm();

		// Search for the opening parenthesis
		while (ptr != end) {
			char ch = *ptr++;
			SVG_CHECK(bx::isSpace(ch) || ch == '(', "Invalid matrix format");
			if (ch == '(') {
				break;
			}
		}

		ptr = parseCoord(ptr, end, &transform[0]);
		ptr = parseCoord(ptr, end, &transform[1]);
		ptr = parseCoord(ptr, end, &transform[2]);
		ptr = parseCoord(ptr, end, &transform[3]);
		ptr = parseCoord(ptr, end, &transform[4]);
		ptr = parseCoord(ptr, end, &transform[5]);

		// At this point we should be at the closing parenthesis
		SVG_CHECK(*ptr == ')', "Invalid matrix format");
	} else {
		SVG_WARN(false, "Unhandled transform value: %.*s", str.getLength(), str.getPtr());
	}

	return true;
}

static bool parsePath(const bx::StringView& str, Path* path)
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
			PathCmd* cmd = pathAllocCmd(path, PathCmdType::MoveTo);
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
			PathCmd* cmd = pathAllocCmd(path, PathCmdType::LineTo);
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
			PathCmd* cmd = pathAllocCmd(path, PathCmdType::LineTo);
			ptr = parseCoord(ptr, end, &cmd->m_Data[0]);
			cmd->m_Data[1] = lastY;

			if (ch == lch) {
				cmd->m_Data[0] += lastX;
			}

			lastX = cmd->m_Data[0];
			lastY = cmd->m_Data[1];
		} else if (lch == 'v') {
			// Vertical LineTo abs
			PathCmd* cmd = pathAllocCmd(path, PathCmdType::LineTo);
			ptr = parseCoord(ptr, end, &cmd->m_Data[1]);
			cmd->m_Data[0] = lastX;

			if (ch == lch) {
				cmd->m_Data[1] += lastY;
			}

			lastX = cmd->m_Data[0];
			lastY = cmd->m_Data[1];
		} else if (lch == 'z') {
			// ClosePath
			PathCmd* cmd = pathAllocCmd(path, PathCmdType::ClosePath);
			BX_UNUSED(cmd);
			lastX = firstX;
			lastY = firstY;
			// No data
		} else if (lch == 'c') {
			// CubicTo abs
			PathCmd* cmd = pathAllocCmd(path, PathCmdType::CubicTo);
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
			PathCmd* cmd = pathAllocCmd(path, PathCmdType::CubicTo);
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
			PathCmd* cmd = pathAllocCmd(path, PathCmdType::QuadraticTo);
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
			PathCmd* cmd = pathAllocCmd(path, PathCmdType::QuadraticTo);
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
			PathCmd* cmd = pathAllocCmd(path, PathCmdType::ArcTo);
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

static bool parsePointList(const bx::StringView& str, PointList* ptList)
{
	const char* ptr = str.getPtr();
	const char* end = str.getTerm();
	while (ptr != end) {
		float* pt = pointListAllocPoint(ptList);
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
			uint32_t maxLen = bx::min<uint32_t>(SSVG_CONFIG_FONT_FAMILY_MAX_LEN - 1, value.getLength());
			SVG_WARN((int32_t)maxLen >= value.getLength(), "font-family \"%.*s\" truncated to %d characters", value.getLength(), value.getPtr(), maxLen);
			bx::memCopy(&attrs->m_FontFamily[0], value.getPtr(), maxLen);
			attrs->m_FontFamily[maxLen] = '\0';
			return ParseAttr::OK;
		} else if (!bx::strCmp(partialName, "-size", 5)) {
			return parseLength(parser, value, &attrs->m_FontSize) ? ParseAttr::OK : ParseAttr::Fail;
		}
	} else if (!bx::strCmp(name, "transform", 9)) {
		return parseTransform(parser, value, &attrs->m_Transform[0]) ? ParseAttr::OK : ParseAttr::Fail;
	} else if (!bx::strCmp(name, "id", 2)) {
		uint32_t maxLen = bx::min<uint32_t>(SSVG_CONFIG_ID_MAX_LEN - 1, value.getLength());
		SVG_WARN((int32_t)maxLen >= value.getLength(), "id \"%.*s\" truncated to %d characters", value.getLength(), value.getPtr(), maxLen);
		bx::memCopy(&attrs->m_ID[0], value.getPtr(), maxLen);
		attrs->m_ID[maxLen] = '\0';
		return ParseAttr::OK;
	}

	return ParseAttr::Unknown;
}

static bool parseShape_Group(ParserState* parser, Shape* group)
{
	bool err = false;
	while (!parserDone(parser)) {
		SVG_CHECK(!(parser->m_Ptr[0] == '/' && parser->m_Ptr[1] == '>'), "Empty group element");
		if (parserExpectingChar(parser, '>')) {
			break;
		}

		bx::StringView name, value;
		if (!parserGetAttribute(parser, &name, &value)) {
			err = true;
			break;
		}

		// Check if this a generic attribute (i.e. styling)
		ParseAttr::Result res = parseGenericShapeAttribute(parser, name, value, &group->m_Attrs);
		if (res == ParseAttr::Fail) {
			err = true;
			break;
		} else if (res == ParseAttr::Unknown) {
			// No specific attributes for groups. Ignore it.
			SVG_WARN(false, "Ignoring g attribute: %.*s=\"%.*s\"", name.getLength(), name.getPtr(), value.getLength(), value.getPtr());
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
					err = !parsePath(value, &path->m_Path);
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
					err = !parsePointList(value, &shape->m_PointList);
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
	while (!parserDone(parser)) {
		if (parserExpectingChar(parser, '>')) {
			break;
		}

		bx::StringView name, value;
		if (!parserGetAttribute(parser, &name, &value)) {
			err = true;
			break;
		}

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

	if (err) {
		return false;
	}

	ShapeAttributes defaultAttrs;
	bx::memSet(&defaultAttrs, 0, sizeof(ShapeAttributes));
	defaultAttrs.m_StrokeWidth = 1.0f;
	defaultAttrs.m_StrokeMiterLimit = 4.0f;
	defaultAttrs.m_StrokeOpacity = 1.0f;
	defaultAttrs.m_StrokePaint.m_Type = PaintType::None;
	defaultAttrs.m_StrokePaint.m_Color = 0x00000000; // Transparent
	defaultAttrs.m_StrokeLineCap = LineCap::Butt;
	defaultAttrs.m_StrokeLineJoin = LineJoin::Miter;
	defaultAttrs.m_FillOpacity = 1.0f;
	defaultAttrs.m_FillPaint.m_Type = PaintType::None;
	defaultAttrs.m_FillPaint.m_Color = 0x00000000;
	transformIdentity(&defaultAttrs.m_Transform[0]);
	bx::snprintf(defaultAttrs.m_FontFamily, SSVG_CONFIG_FONT_FAMILY_MAX_LEN, "%s", "sans-serif");

	return parseShapes(parser, &img->m_ShapeList, &defaultAttrs, "</svg>", 6);
}

Image* imageAlloc()
{
	Image* img = (Image*)BX_ALLOC(s_Allocator, sizeof(Image));
	bx::memSet(img, 0, sizeof(Image));

	return img;
}

Image* loadImage(const char* xmlStr)
{
	if (!xmlStr || *xmlStr == 0) {
		return nullptr;
	}

	Image* img = imageAlloc();

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
			} else if (!bx::strCmp(tag, "svg", 3)) {
				err = !parseTag_svg(&parser, img);
			} else {
				SVG_WARN(false, "Ignoring unknown root tag %.*s", tag.getLength(), tag.getPtr());
				parserSkipTag(&parser);
			}
		}
	}

	if (err) {
		destroyImage(img);
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

void destroyImage(Image* img)
{
	destroyShapeList(&img->m_ShapeList);
	BX_FREE(s_Allocator, img);
}

void initLib(bx::AllocatorI* allocator)
{
	s_Allocator = allocator;
}
} // namespace svg
