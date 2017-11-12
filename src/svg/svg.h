#ifndef SVG_SVG_H
#define SVG_SVG_H

#include <stdint.h>

#ifndef SVG_CONFIG_DEBUG
#	define SVG_CONFIG_DEBUG 0
#endif

// NOTE: Those 2 are used as the sizes of m_ID and m_FontFamily in ShapeAttributes structs.
// Setting both to 16 chars makes ShapeAttributes 100 bytes long, which in turn makes the Shape struct 128 bytes long.
// (This is only guaranteed to be true at the time of writing this comment)
#ifndef SSVG_CONFIG_ID_MAX_LEN
#	define SSVG_CONFIG_ID_MAX_LEN  16
#endif

#ifndef SSVG_CONFIG_FONT_FAMILY_MAX_LEN
#	define SSVG_CONFIG_FONT_FAMILY_MAX_LEN  16
#endif

#if SVG_CONFIG_DEBUG
#define SVG_TRACE(_format, ...) \
	do { \
		bx::debugPrintf(BX_FILE_LINE_LITERAL "SimpleSVG " _format "\n", ##__VA_ARGS__); \
	} while(0)

#define SVG_WARN(_condition, _format, ...) \
	do { \
		if (!(_condition) ) { \
			SVG_TRACE(BX_FILE_LINE_LITERAL _format, ##__VA_ARGS__); \
		} \
	} while(0)

#define SVG_CHECK(_condition, _format, ...) \
	do { \
		if (!(_condition) ) { \
			SVG_TRACE(BX_FILE_LINE_LITERAL _format, ##__VA_ARGS__); \
			bx::debugBreak(); \
		} \
	} while(0)
#else
#define SVG_TRACE(_format, ...)
#define SVG_WARN(_condition, _format, ...)
#define SVG_CHECK(_condition, _format, ...)
#endif

namespace bx
{
struct AllocatorI;
class StringView;
}

namespace ssvg
{
struct Shape;

struct BaseProfile
{
	enum Enum : uint32_t
	{
		None = 0,
		Full,
		Basic,
		Tiny
	};
};

struct ShapeType
{
	enum Enum : uint32_t
	{
		Group = 0,
		Rect,
		Circle,
		Ellipse,
		Line,
		Polyline,
		Polygon,
		Path,
		Text,

		NumTypes
	};
};

struct PathCmdType
{
	enum Enum : uint32_t
	{
		MoveTo,      // Data: [0] = x, [1] = y
		LineTo,      // Data: [0] = x, [1] = y
		CubicTo,     // Data: [0] = x1, [1] = y1, [2] = x2, [3] = y2, [4] = x, [5] = y
		QuadraticTo, // Data: [0] = x1, [1] = y1, [2] = x, [3] = y
		ArcTo,       // Data: [0] = rx, [1] = ry, [2] = x-axis-rotation, [3] = large-arc-flag, [4] = sweep-flag, [5] = x, [6] = y
		ClosePath,   // No data
	};
};

struct TextAnchor
{
	enum Enum : uint32_t
	{
		Start,
		Middle,
		End
	};
};

struct PaintType
{
	enum Enum : uint32_t
	{
		None,
		Transparent, // Does it make sense? 
		Color
	};
};

struct LineJoin
{
	enum Enum : uint32_t
	{
		Miter,
		Round,
		Bevel
	};
};

struct LineCap
{
	enum Enum : uint32_t
	{
		Butt,
		Round,
		Square
	};
};

struct ShapeList
{
	Shape* m_Shapes;
	uint32_t m_NumShapes;
	uint32_t m_Capacity;
};

struct Rect
{
	float x;
	float y;
	float width;
	float height;
	float rx;
	float ry;
};

struct Circle
{
	float cx;
	float cy;
	float r;
};

struct Ellipse
{
	float cx;
	float cy;
	float rx;
	float ry;
};

struct Line
{
	float x1;
	float y1;
	float x2;
	float y2;
};

struct PointList
{
	float* m_Coords;
	uint32_t m_NumPoints;
	uint32_t m_Capacity;
};

struct PathCmd
{
	PathCmdType::Enum m_Type;
	float m_Data[7]; // NOTE: The amount and meaning of each value depends on m_Type (see PathCmdType::Enum)
};

struct Path
{
	PathCmd* m_Commands;
	uint32_t m_NumCommands;
	uint32_t m_Capacity;
};

// TODO: alignment-baseline
struct Text
{
	char* m_String;
	float x;
	float y;
	TextAnchor::Enum m_Anchor;
};

// TODO: Gradients
// TODO: Images (???)
struct Paint
{
	PaintType::Enum m_Type;

	union {
		uint32_t m_ColorABGR;
	};
};

struct ShapeAttributes
{
	Paint m_StrokePaint;
	Paint m_FillPaint;
	float m_Transform[6];
	float m_StrokeMiterLimit;
	float m_StrokeOpacity;
	float m_StrokeWidth;
	float m_FillOpacity;
	float m_FontSize;
	LineJoin::Enum m_StrokeLineJoin;
	LineCap::Enum m_StrokeLineCap;
	char m_ID[SSVG_CONFIG_ID_MAX_LEN];
	char m_FontFamily[SSVG_CONFIG_FONT_FAMILY_MAX_LEN];
};

struct Shape
{
	ShapeType::Enum m_Type;
	ShapeAttributes m_Attrs;

	union {
		ShapeList m_ShapeList; // Note: Used for ShapeType::Group
		PointList m_PointList; // NOTE: Used for ShapeType::Polyline and ShapeType::Polygon
		Rect m_Rect;
		Circle m_Circle;
		Ellipse m_Ellipse;
		Line m_Line;
		Path m_Path;
		Text m_Text;
	};
};

struct Image
{
	ShapeList m_ShapeList;
	float m_Width;
	float m_Height;
	BaseProfile::Enum m_BaseProfile;
	uint16_t m_VerMajor;
	uint16_t m_VerMinor;
};

void initLib(bx::AllocatorI* allocator);

Image* loadImage(const char* xmlStr);
void destroyImage(Image* img);

Shape* shapeListAllocShape(ShapeList* shapeList, ShapeType::Enum type, const ShapeAttributes* parentAttrs);
void shapeListShrinkToFit(ShapeList* shapeList);
void shapeListFree(ShapeList* shapeList);

PathCmd* pathAllocCmd(Path* path, PathCmdType::Enum type);
void pathShrinkToFit(Path* path);
void pathFree(Path* path);
bool pathFromString(Path* path, const bx::StringView& str);

float* pointListAllocPoints(PointList* ptList, uint32_t n);
void pointListShrinkToFit(PointList* ptList);
void pointListFree(PointList* ptList);
bool pointListFromString(PointList* ptList, const bx::StringView& str);
}

#endif
