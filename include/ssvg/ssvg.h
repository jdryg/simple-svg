#ifndef SVG_SVG_H
#define SVG_SVG_H

#include <stdint.h>

#ifndef SSVG_CONFIG_DEBUG
#	define SSVG_CONFIG_DEBUG 0
#endif

#ifndef SSVG_CONFIG_ID_MAX_LEN
#	define SSVG_CONFIG_ID_MAX_LEN  16
#endif

#ifndef SSVG_CONFIG_FONT_FAMILY_MAX_LEN
#	define SSVG_CONFIG_FONT_FAMILY_MAX_LEN  16
#endif

#ifndef SSVG_CONFIG_CLASS_MAX_LEN
#	define SSVG_CONFIG_CLASS_MAX_LEN 0
#endif

#ifndef SSVG_CONFIG_MINIFY_PATHS
#	define SSVG_CONFIG_MINIFY_PATHS 1
#endif

#if SSVG_CONFIG_DEBUG
#include <bx/debug.h>

#define SSVG_TRACE(_format, ...) \
	do { \
		bx::debugPrintf(BX_FILE_LINE_LITERAL "ssvg " _format "\n", ##__VA_ARGS__); \
	} while(0)

#define SSVG_WARN(_condition, _format, ...) \
	do { \
		if (!(_condition) ) { \
			SSVG_TRACE(BX_FILE_LINE_LITERAL _format, ##__VA_ARGS__); \
		} \
	} while(0)

#define SSVG_CHECK(_condition, _format, ...) \
	do { \
		if (!(_condition) ) { \
			SSVG_TRACE(BX_FILE_LINE_LITERAL _format, ##__VA_ARGS__); \
			bx::debugBreak(); \
		} \
	} while(0)
#else
#define SSVG_TRACE(_format, ...)
#define SSVG_WARN(_condition, _format, ...)
#define SSVG_CHECK(_condition, _format, ...)
#endif

namespace bx
{
struct AllocatorI;
struct WriterI;
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
		None = 0,
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

struct FillRule
{
	enum Enum : uint32_t
	{
		NonZero = 0,
		EvenOdd = 1
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

	union
	{
		uint32_t m_ColorABGR;
	};
};

struct AttribFlags
{
	enum Enum : uint32_t
	{
		StrokePaintInherit      = 1 << 0,
		StrokeMiterLimitInherit = 1 << 1,
		StrokeOpacityInherit    = 1 << 2,
		StrokeWidthInherit      = 1 << 3,
		StrokeLineJoinInherit   = 1 << 4,
		StrokeLineCapInherit    = 1 << 5,
		FillPaintInherit        = 1 << 6,
		FillOpacityInherit      = 1 << 7,
		FillRuleInherit         = 1 << 8,
		FontSizeInherit         = 1 << 9,
		FontFamilyInherit       = 1 << 10,

		InheritAll = StrokePaintInherit | StrokeMiterLimitInherit | StrokeOpacityInherit | StrokeWidthInherit | StrokeLineJoinInherit | StrokeLineCapInherit 
		           | FillPaintInherit | FillOpacityInherit | FillRuleInherit | FontSizeInherit | FontFamilyInherit
	};
};

struct ShapeAttributes
{
	const ShapeAttributes* m_Parent;
	Paint m_StrokePaint;
	Paint m_FillPaint;
	float m_Transform[6];
	float m_StrokeMiterLimit;
	float m_StrokeOpacity;
	float m_StrokeWidth;
	float m_FillOpacity;
	float m_FontSize;
	float m_Opacity;
	uint32_t m_Flags;
	LineJoin::Enum m_StrokeLineJoin;
	LineCap::Enum m_StrokeLineCap;
	FillRule::Enum m_FillRule;
	char m_ID[SSVG_CONFIG_ID_MAX_LEN];
	char m_FontFamily[SSVG_CONFIG_FONT_FAMILY_MAX_LEN];
#if SSVG_CONFIG_CLASS_MAX_LEN
	char m_Class[SSVG_CONFIG_CLASS_MAX_LEN];
#endif
};

struct Shape
{
	ShapeType::Enum m_Type;
	ShapeAttributes* m_Attrs;
	float m_BoundingRect[4]; // NOTE: Transformation independent axis-aligned bounding rect {minx, miny, maxx, maxy}

	union
	{
		ShapeList m_ShapeList; // NOTE: Used for ShapeType::Group
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
	ShapeAttributes m_BaseAttrs;
	float m_Width;
	float m_Height;
	float m_ViewBox[4];
	float m_BoundingRect[4];
	BaseProfile::Enum m_BaseProfile;
	uint16_t m_VerMajor;
	uint16_t m_VerMinor;
};

struct ImageLoadFlags
{
	enum Enum : uint32_t
	{
		ConvertPolygonsToPaths = 1 << 0,
		ConvertPolylinesToPaths = 1 << 1,
		ConvertQuadToCubicBezier = 1 << 2,
		ConvertArcToCubicBezier = 1 << 3,
		CalcShapeBounds = 1 << 4,
		CalcPathConvexity = 1 << 5,
	};
};

void initLib(bx::AllocatorI* allocator);
void shutdownLib();

Image* imageLoad(const char* xmlStr, uint32_t flags, const ShapeAttributes* baseAttrs);
bool imageSave(const Image* img, bx::WriterI* writer);
Image* imageCreate(const ShapeAttributes* baseAttrs);
void imageDestroy(Image* img);

Shape* shapeListAllocShape(ShapeList* shapeList, ShapeType::Enum type, const ShapeAttributes* parentAttrs);
void shapeListShrinkToFit(ShapeList* shapeList);
void shapeListFree(ShapeList* shapeList);
uint32_t shapeListAddShape(ShapeList* shapeList, const Shape* shape);
uint32_t shapeListAddGroup(ShapeList* shapeList, const ShapeAttributes* parentAttrs, const Shape* children, uint32_t numChildren);
uint32_t shapeListAddRect(ShapeList* shapeList, const ShapeAttributes* parentAttrs, float x, float y, float w, float h, float rx, float ry);
uint32_t shapeListAddCircle(ShapeList* shapeList, const ShapeAttributes* parentAttrs, float x, float y, float r);
uint32_t shapeListAddEllipse(ShapeList* shapeList, const ShapeAttributes* parentAttrs, float x, float y, float rx, float ry);
uint32_t shapeListAddLine(ShapeList* shapeList, const ShapeAttributes* parentAttrs, float x1, float y1, float x2, float y2);
uint32_t shapeListAddPolyline(ShapeList* shapeList, const ShapeAttributes* parentAttrs, const float* coords, uint32_t numPoints);
uint32_t shapeListAddPolygon(ShapeList* shapeList, const ShapeAttributes* parentAttrs, const float* coords, uint32_t numPoints);
uint32_t shapeListAddPath(ShapeList* shapeList, const ShapeAttributes* parentAttrs, const PathCmd* pathCommands, uint32_t commands);
uint32_t shapeListAddText(ShapeList* shapeList, const ShapeAttributes* parentAttrs, float x, float y, TextAnchor::Enum anchor, const char* text);
uint32_t shapeListMoveShapeToBack(ShapeList* shapeList, uint32_t shapeID);
uint32_t shapeListMoveShapeToFront(ShapeList* shapeList, uint32_t shapeID);
void shapeListDeleteShape(ShapeList* shapeList, uint32_t shapeID);
void shapeListCalcBounds(ShapeList* shapeList, float* bounds);

PathCmd* pathAllocCommand(Path* path, PathCmdType::Enum type);
PathCmd* pathAllocCommands(Path* path, uint32_t n);
PathCmd* pathInsertCommands(Path* path, uint32_t at, uint32_t n);
void pathShrinkToFit(Path* path);
void pathFree(Path* path);
bool pathFromString(Path* path, const bx::StringView& str, uint32_t flags);
bool pathToString(const Path* path, bx::WriterI* writer);
uint32_t pathMoveTo(Path* path, float x, float y);
uint32_t pathLineTo(Path* path, float x, float y);
uint32_t pathCubicTo(Path* path, float x1, float y1, float x2, float y2, float x, float y);
uint32_t pathQuadraticTo(Path* path, float x1, float y1, float x, float y);
uint32_t pathArcTo(Path* path, float rx, float ry, float xAxisRotation, int largeArcFlag, int sweepFlag, float x, float y);
uint32_t pathClose(Path* path);
void pathCalcBounds(const Path* path, float* bounds);
void pathConvertCommand(Path* path, uint32_t cmdID, PathCmdType::Enum newType);

float* pointListAllocPoints(PointList* ptList, uint32_t n);
void pointListShrinkToFit(PointList* ptList);
void pointListFree(PointList* ptList);
bool pointListFromString(PointList* ptList, const bx::StringView& str);
bool pointListToString(const PointList* ptList, bx::WriterI* writer);
void pointListCalcBounds(const PointList* ptList, float* bounds);

void shapeAttrsSetID(ShapeAttributes* attrs, const bx::StringView& id);
void shapeAttrsSetFontFamily(ShapeAttributes* attrs, const bx::StringView& fontFamily);
void shapeAttrsSetClass(ShapeAttributes* attrs, const bx::StringView& c);

void shapeFree(Shape* shape);
bool shapeCopy(Shape* dst, const Shape* src, bool copyAttrs = true);
void shapeUpdateBounds(Shape* shape);

void transformIdentity(float* transform);
void transformTranslation(float* transform, float x, float y);
void transformMultiply(float* a, const float* b);
void transformTranslate(float* transform, float x, float y);
void transformPoint(const float* transform, const float* localPos, float* globalPos);
void transformBoundingRect(const float* transform, const float* localRect, float* globalRect);
}

#endif
