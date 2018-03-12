#include <ssvg/ssvg.h>
#include <bx/bx.h>
#include <bx/string.h>

namespace ssvg
{
uint32_t shapeListAddShape(ShapeList* shapeList, const Shape* shape)
{
	SVG_CHECK(shape != nullptr, "Use shapeListAllocShape() instead");

	Shape* newShape = shapeListAllocShape(shapeList, shape->m_Type, nullptr);
	if (!newShape) {
		return ~0u;
	}

	shapeCopy(newShape, shape, true);
	shapeUpdateBounds(newShape);

	return shapeList->m_NumShapes - 1;
}

uint32_t shapeListAddGroup(ShapeList* shapeList, const ShapeAttributes* parentAttrs, const Shape* children, uint32_t numChildren)
{
	Shape* group = shapeListAllocShape(shapeList, ShapeType::Group, parentAttrs);
	if (!group) {
		return ~0u;
	}

	if (children && numChildren) {
		// Create a temp group on the stack and use shapeCopy()
		Shape tmpGroup;
		bx::memSet(&tmpGroup, 0, sizeof(Shape));
		tmpGroup.m_Type = ShapeType::Group;
		if (parentAttrs) {
			bx::memCopy(&tmpGroup.m_Attrs, parentAttrs, sizeof(ShapeAttributes));
		}
		tmpGroup.m_ShapeList.m_Shapes = (Shape*)children; // NOTE: Casting a const to not-const should be ok in this case since the tmpGroup is passed as const to shapeCopy().
		tmpGroup.m_ShapeList.m_NumShapes = numChildren;
		tmpGroup.m_ShapeList.m_Capacity = 0; // capacity < count == list is read-only (should assert in debug mode if you try to alloc/shrink the list).

		shapeCopy(group, &tmpGroup);
	}

	shapeUpdateBounds(group);

	return shapeList->m_NumShapes - 1;
}

uint32_t shapeListAddRect(ShapeList* shapeList, const ShapeAttributes* parentAttrs, float x, float y, float w, float h, float rx, float ry)
{
	Shape* rect = shapeListAllocShape(shapeList, ShapeType::Rect, parentAttrs);
	if (!rect) {
		return ~0u;
	}

	rect->m_Rect.x = x;
	rect->m_Rect.y = y;
	rect->m_Rect.width = w;
	rect->m_Rect.height = h;
	rect->m_Rect.rx = rx;
	rect->m_Rect.ry = ry;

	shapeUpdateBounds(rect);

	return shapeList->m_NumShapes - 1;
}

uint32_t shapeListAddCircle(ShapeList* shapeList, const ShapeAttributes* parentAttrs, float x, float y, float r)
{
	Shape* circle = shapeListAllocShape(shapeList, ShapeType::Circle, parentAttrs);
	if (!circle) {
		return ~0u;
	}

	circle->m_Circle.cx = x;
	circle->m_Circle.cy = y;
	circle->m_Circle.r = r;

	shapeUpdateBounds(circle);

	return shapeList->m_NumShapes - 1;
}

uint32_t shapeListAddEllipse(ShapeList* shapeList, const ShapeAttributes* parentAttrs, float x, float y, float rx, float ry)
{
	Shape* ellipse = shapeListAllocShape(shapeList, ShapeType::Ellipse, parentAttrs);
	if (!ellipse) {
		return ~0u;
	}

	ellipse->m_Ellipse.cx = x;
	ellipse->m_Ellipse.cy = y;
	ellipse->m_Ellipse.rx = rx;
	ellipse->m_Ellipse.ry = ry;

	shapeUpdateBounds(ellipse);

	return shapeList->m_NumShapes - 1;
}

uint32_t shapeListAddLine(ShapeList* shapeList, const ShapeAttributes* parentAttrs, float x1, float y1, float x2, float y2)
{
	Shape* line = shapeListAllocShape(shapeList, ShapeType::Line, parentAttrs);
	if (!line) {
		return ~0u;
	}

	line->m_Line.x1 = x1;
	line->m_Line.y1 = y1;
	line->m_Line.x2 = x2;
	line->m_Line.y2 = y2;

	shapeUpdateBounds(line);

	return shapeList->m_NumShapes - 1;
}

uint32_t shapeListAddPolyline(ShapeList* shapeList, const ShapeAttributes* parentAttrs, const float* coords, uint32_t numPoints)
{
	Shape* polyline = shapeListAllocShape(shapeList, ShapeType::Polyline, parentAttrs);
	if (!polyline) {
		return ~0u;
	}

	if (coords && numPoints) {
		float* dstCoords = pointListAllocPoints(&polyline->m_PointList, numPoints);
		bx::memCopy(dstCoords, coords, sizeof(float) * 2 * numPoints);
	}

	shapeUpdateBounds(polyline);

	return shapeList->m_NumShapes - 1;
}

uint32_t shapeListAddPolygon(ShapeList* shapeList, const ShapeAttributes* parentAttrs, const float* coords, uint32_t numPoints)
{
	Shape* polygon = shapeListAllocShape(shapeList, ShapeType::Polygon, parentAttrs);
	if (!polygon) {
		return ~0u;
	}

	if (coords && numPoints) {
		float* dstCoords = pointListAllocPoints(&polygon->m_PointList, numPoints);
		bx::memCopy(dstCoords, coords, sizeof(float) * 2 * numPoints);
	}

	shapeUpdateBounds(polygon);

	return shapeList->m_NumShapes - 1;
}

uint32_t shapeListAddPath(ShapeList* shapeList, const ShapeAttributes* parentAttrs, const PathCmd* commands, uint32_t numCommands)
{
	Shape* path = shapeListAllocShape(shapeList, ShapeType::Path, parentAttrs);
	if (!path) {
		return ~0u;
	}

	if (commands && numCommands) {
		Shape tmpPath;
		bx::memSet(&tmpPath, 0, sizeof(Shape));
		tmpPath.m_Type = ShapeType::Path;
		if (parentAttrs) {
			bx::memCopy(&tmpPath.m_Attrs, parentAttrs, sizeof(ShapeAttributes));
		}
		tmpPath.m_Path.m_Commands = (PathCmd*)commands;
		tmpPath.m_Path.m_NumCommands = numCommands;
		tmpPath.m_Path.m_Capacity = 0;

		shapeCopy(path, &tmpPath);
	}

	shapeUpdateBounds(path);

	return shapeList->m_NumShapes - 1;
}

uint32_t shapeListAddText(ShapeList* shapeList, const ShapeAttributes* parentAttrs, float x, float y, TextAnchor::Enum anchor, const char* str)
{
	Shape* text = shapeListAllocShape(shapeList, ShapeType::Text, parentAttrs);
	if (!text) {
		return ~0u;
	}

	if (str) {
		Shape tmpText;
		bx::memSet(&tmpText, 0, sizeof(Shape));
		tmpText.m_Type = ShapeType::Text;
		if (parentAttrs) {
			bx::memCopy(&tmpText.m_Attrs, parentAttrs, sizeof(ShapeAttributes));
		}
		tmpText.m_Text.x = x;
		tmpText.m_Text.y = y;
		tmpText.m_Text.m_Anchor = anchor;
		tmpText.m_Text.m_String = (char*)str;

		shapeCopy(text, &tmpText);
	} else {
		text->m_Text.x = x;
		text->m_Text.y = y;
		text->m_Text.m_Anchor = anchor;
		text->m_Text.m_String = nullptr;
	}

	shapeUpdateBounds(text);

	return shapeList->m_NumShapes - 1;
}

uint32_t pathMoveTo(Path* path, float x, float y)
{
	PathCmd* cmd = pathAllocCommand(path, PathCmdType::MoveTo);
	cmd->m_Data[0] = x;
	cmd->m_Data[1] = y;

	return path->m_NumCommands - 1;
}

uint32_t pathLineTo(Path* path, float x, float y)
{
	PathCmd* cmd = pathAllocCommand(path, PathCmdType::LineTo);
	cmd->m_Data[0] = x;
	cmd->m_Data[1] = y;

	return path->m_NumCommands - 1;
}

uint32_t pathCubicTo(Path* path, float x1, float y1, float x2, float y2, float x, float y)
{
	PathCmd* cmd = pathAllocCommand(path, PathCmdType::CubicTo);
	cmd->m_Data[0] = x1;
	cmd->m_Data[1] = y1;
	cmd->m_Data[2] = x2;
	cmd->m_Data[3] = y2;
	cmd->m_Data[4] = x;
	cmd->m_Data[5] = y;

	return path->m_NumCommands - 1;
}

uint32_t pathQuadraticTo(Path* path, float x1, float y1, float x, float y)
{
	PathCmd* cmd = pathAllocCommand(path, PathCmdType::QuadraticTo);
	cmd->m_Data[0] = x1;
	cmd->m_Data[1] = y1;
	cmd->m_Data[2] = x;
	cmd->m_Data[3] = y;

	return path->m_NumCommands - 1;
}

uint32_t pathArcTo(Path* path, float rx, float ry, float xAxisRotation, int largeArcFlag, int sweepFlag, float x, float y)
{
	PathCmd* cmd = pathAllocCommand(path, PathCmdType::ArcTo);
	cmd->m_Data[0] = rx;
	cmd->m_Data[1] = ry;
	cmd->m_Data[2] = xAxisRotation;
	cmd->m_Data[3] = (float)largeArcFlag;
	cmd->m_Data[4] = (float)sweepFlag;
	cmd->m_Data[5] = x;
	cmd->m_Data[6] = y;

	return path->m_NumCommands - 1;
}

uint32_t pathClose(Path* path)
{
	PathCmd* cmd = pathAllocCommand(path, PathCmdType::ClosePath);

	BX_UNUSED(cmd);

	return path->m_NumCommands - 1;
}

inline void pathCmdGetEndPoint(const PathCmd* cmd, float* p)
{
	switch (cmd->m_Type) {
	case PathCmdType::MoveTo:
	case PathCmdType::LineTo:
		p[0] = cmd->m_Data[0];
		p[1] = cmd->m_Data[1];
		break;
	case PathCmdType::CubicTo:
		p[0] = cmd->m_Data[4];
		p[1] = cmd->m_Data[5];
		break;
	case PathCmdType::QuadraticTo:
		p[0] = cmd->m_Data[2];
		p[1] = cmd->m_Data[3];
		break;
	case PathCmdType::ArcTo:
		p[0] = cmd->m_Data[5];
		p[1] = cmd->m_Data[6];
		break;
	case PathCmdType::ClosePath:
		// TODO: The end point of a close path command is the first command's end point
		// which requires the whole path.
		SVG_CHECK(false, "Cannot get ClosePath command's endpoint");
		p[0] = p[1] = 0.0f;
		break;
	}
}

void pathConvertCommand(Path* path, uint32_t cmdID, PathCmdType::Enum newType)
{
	SVG_CHECK(cmdID < path->m_NumCommands, "Invalid command ID");

	if (cmdID == 0) {
		SVG_CHECK(newType == PathCmdType::MoveTo, "Cannot convert 1st command to other than MoveTo");
		return;
	}

	PathCmd* cmd = &path->m_Commands[cmdID];
	const PathCmdType::Enum oldType = cmd->m_Type;
	if (oldType == newType) {
		// No conversion required.
		return;
	}

	PathCmd* prevCmd = &path->m_Commands[cmdID - 1];

	switch (oldType) {
	case PathCmdType::MoveTo:
		if (newType == PathCmdType::LineTo) {
			cmd->m_Type = PathCmdType::LineTo;
		} else {
			SVG_WARN(false, "Path command conversion not implemented yet.");
		}
		break;
	case PathCmdType::LineTo:
		if (newType == PathCmdType::CubicTo) {
			float last[2];
			pathCmdGetEndPoint(prevCmd, &last[0]);

			const float pos[2] = { cmd->m_Data[0], cmd->m_Data[1] };
			const float delta[2] = { pos[0] - last[0], pos[1] - last[1] };

			cmd->m_Type = PathCmdType::CubicTo;
			cmd->m_Data[0] = last[0] + delta[0] * 0.5f;
			cmd->m_Data[1] = last[1] + delta[1] * 0.5f;
			cmd->m_Data[2] = last[0] + delta[0] * 0.5f;
			cmd->m_Data[3] = last[1] + delta[1] * 0.5f;
			cmd->m_Data[4] = pos[0];
			cmd->m_Data[5] = pos[1];
		} else if (newType == PathCmdType::QuadraticTo) {
			float last[2];
			pathCmdGetEndPoint(prevCmd, &last[0]);

			const float pos[2] = { cmd->m_Data[0], cmd->m_Data[1] };
			const float delta[2] = { pos[0] - last[0], pos[1] - last[1] };

			cmd->m_Type = PathCmdType::QuadraticTo;
			cmd->m_Data[0] = last[0] + delta[0] * 0.5f;
			cmd->m_Data[1] = last[1] + delta[1] * 0.5f;
			cmd->m_Data[2] = pos[0];
			cmd->m_Data[3] = pos[1];
		} else {
			SVG_WARN(false, "Path command conversion not implemented yet.");
		}
		break;
	case PathCmdType::CubicTo:
		if (newType == PathCmdType::LineTo) {
			cmd->m_Type = PathCmdType::LineTo;
			cmd->m_Data[0] = cmd->m_Data[4];
			cmd->m_Data[1] = cmd->m_Data[5];
		} else {
			SVG_WARN(false, "Path command conversion not implemented yet.");
		}
		break;
	case PathCmdType::QuadraticTo:
		if (newType == PathCmdType::LineTo) {
			cmd->m_Type = PathCmdType::LineTo;
			cmd->m_Data[0] = cmd->m_Data[2];
			cmd->m_Data[1] = cmd->m_Data[3];
		} else {
			SVG_WARN(false, "Path command conversion not implemented yet.");
		}
		break;
	case PathCmdType::ArcTo:
		SVG_WARN(false, "Path command conversion not implemented yet.");
		break;
	case PathCmdType::ClosePath:
		SVG_WARN(false, "Path command conversion not implemented yet.");
		break;
	default:
		SVG_CHECK(false, "Unknown command type");
	}
}
}
