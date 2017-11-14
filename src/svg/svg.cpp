#include "svg.h"
#include <bx/bx.h>
#include <bx/allocator.h>
#include <bx/string.h>

namespace ssvg
{
bx::AllocatorI* s_Allocator = nullptr;
ShapeAttributes s_DefaultAttrs;

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
	
	// Copy parent attributes
	if (parentAttrs) {
		bx::memCopy(&shape->m_Attrs, parentAttrs, sizeof(ShapeAttributes));
	} else {
		bx::memCopy(&shape->m_Attrs, &s_DefaultAttrs, sizeof(ShapeAttributes));
	}
	
	// Reset id and transform
	shape->m_Attrs.m_ID[0] = '\0';
	transformIdentity(&shape->m_Attrs.m_Transform[0]);

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

Image* imageCreate()
{
	Image* img = (Image*)BX_ALLOC(s_Allocator, sizeof(Image));
	bx::memSet(img, 0, sizeof(Image));

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
