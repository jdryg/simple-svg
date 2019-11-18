#include <ssvg/ssvg.h>
#include <bx/bx.h>
#include <bx/string.h>
#include <bx/math.h>

BX_PRAGMA_DIAGNOSTIC_IGNORED_GCC("-Wmaybe-uninitialized")

namespace ssvg
{
static void convertArcToBezier(Path* path, uint32_t cmdID, const float* arcToArgs, const float* lastPt);

uint32_t shapeListAddShape(ShapeList* shapeList, const Shape* shape)
{
	SSVG_CHECK(shape != nullptr, "Use shapeListAllocShape() instead");

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
		tmpGroup.m_ShapeList.m_Shapes = (Shape*)children; // NOTE: Casting a const to not-const should be ok in this case since the tmpGroup is passed as const to shapeCopy().
		tmpGroup.m_ShapeList.m_NumShapes = numChildren;
		tmpGroup.m_ShapeList.m_Capacity = 0; // capacity < count == list is read-only (should assert in debug mode if you try to alloc/shrink the list).

		shapeCopy(group, &tmpGroup, false);
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
		tmpPath.m_Path.m_Commands = (PathCmd*)commands;
		tmpPath.m_Path.m_NumCommands = numCommands;
		tmpPath.m_Path.m_Capacity = 0;

		shapeCopy(path, &tmpPath, false);
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
		tmpText.m_Text.x = x;
		tmpText.m_Text.y = y;
		tmpText.m_Text.m_Anchor = anchor;
		tmpText.m_Text.m_String = (char*)str;

		shapeCopy(text, &tmpText, false);
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
		SSVG_CHECK(false, "Cannot get ClosePath command's endpoint");
		p[0] = p[1] = 0.0f;
		break;
	}
}

void pathConvertCommand(Path* path, uint32_t cmdID, PathCmdType::Enum newType)
{
	SSVG_CHECK(cmdID < path->m_NumCommands, "Invalid command ID");

	if (cmdID == 0) {
		SSVG_CHECK(newType == PathCmdType::MoveTo, "Cannot convert 1st command to other than MoveTo");
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
			SSVG_WARN(false, "Path command conversion not implemented yet.");
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
			SSVG_WARN(false, "Path command conversion not implemented yet.");
		}
		break;
	case PathCmdType::CubicTo:
		if (newType == PathCmdType::LineTo) {
			cmd->m_Type = PathCmdType::LineTo;
			cmd->m_Data[0] = cmd->m_Data[4];
			cmd->m_Data[1] = cmd->m_Data[5];
		} else {
			SSVG_WARN(false, "Path command conversion not implemented yet.");
		}
		break;
	case PathCmdType::QuadraticTo:
		if (newType == PathCmdType::LineTo) {
			cmd->m_Type = PathCmdType::LineTo;
			cmd->m_Data[0] = cmd->m_Data[2];
			cmd->m_Data[1] = cmd->m_Data[3];
		} else if (newType == PathCmdType::CubicTo) {
			float last[2];
			pathCmdGetEndPoint(prevCmd, &last[0]);

			const float cx = cmd->m_Data[0];
			const float cy = cmd->m_Data[1];
			const float x = cmd->m_Data[2];
			const float y = cmd->m_Data[3];

			const float c1x = last[0] + (2.0f / 3.0f) * (cx - last[0]);
			const float c1y = last[1] + (2.0f / 3.0f) * (cy - last[1]);
			const float c2x = x + (2.0f / 3.0f) * (cx - x);
			const float c2y = y + (2.0f / 3.0f) * (cy - y);

			cmd->m_Type = PathCmdType::CubicTo;
			cmd->m_Data[0] = c1x;
			cmd->m_Data[1] = c1y;
			cmd->m_Data[2] = c2x;
			cmd->m_Data[3] = c2y;
			cmd->m_Data[4] = x;
			cmd->m_Data[5] = y;
		} else {
			SSVG_WARN(false, "Path command conversion not implemented yet.");
		}
		break;
	case PathCmdType::ArcTo:
		if (newType == PathCmdType::LineTo) {
			cmd->m_Type = PathCmdType::LineTo;
			cmd->m_Data[0] = cmd->m_Data[5];
			cmd->m_Data[1] = cmd->m_Data[6];
		} else if (newType == PathCmdType::CubicTo) {
			float last[2];
			pathCmdGetEndPoint(prevCmd, &last[0]);

			convertArcToBezier(path, cmdID, &cmd->m_Data[0], last);
		} else {
			SSVG_WARN(false, "Path command conversion not implemented yet.");
		}
		break;
	case PathCmdType::ClosePath:
		SSVG_WARN(false, "Path command conversion not implemented yet.");
		break;
	default:
		SSVG_CHECK(false, "Unknown command type");
	}
}

static float nsvg__vecang(float ux, float uy, float vx, float vy)
{
	const float umag = bx::sqrt(ux * ux + uy * uy);
	const float vmag = bx::sqrt(vx * vx + vy * vy);
	const float u_dot_v = ux * vx + uy * vy;
	const float r = bx::clamp<float>(u_dot_v / (umag * vmag), -1.0f, 1.0f);
	return bx::sign(ux * vy - uy * vx) * bx::acos(r);
}

// nsvg__pathArcTo(NSVGparser* p, float* cpx, float* cpy, float* args, int rel)
static void convertArcToBezier(Path* path, uint32_t cmdID, const float* arcToArgs, const float* lastPt)
{
	// Ported from canvg (https://code.google.com/p/canvg/)
	float rx = bx::abs(arcToArgs[0]);                    // x radius
	float ry = bx::abs(arcToArgs[1]);                    // y radius
	const float rotx = bx::toRad(arcToArgs[2]);          // x rotation angle
	const int fa = bx::abs(arcToArgs[3]) > 1e-6 ? 1 : 0; // Large arc
	const int fs = bx::abs(arcToArgs[4]) > 1e-6 ? 1 : 0; // Sweep direction
	const float x1 = lastPt[0];                          // start point x
	const float y1 = lastPt[1];                          // start point y
	const float x2 = arcToArgs[5];                       // end point x
	const float y2 = arcToArgs[6];                       // end point y

	float dx = x1 - x2;
	float dy = y1 - y2;
	float d = bx::sqrt(dx * dx + dy * dy);
	if (d < 1e-6f || rx < 1e-6f || ry < 1e-6f) {
		// The arc degenerates to a line
		PathCmd* cmd = &path->m_Commands[cmdID];
		cmd->m_Type = PathCmdType::LineTo;
		cmd->m_Data[0] = x2;
		cmd->m_Data[1] = y2;
		return;
	}

	const float sinrx = bx::sin(rotx);
	const float cosrx = bx::cos(rotx);

	// Convert to center point parameterization.
	// http://www.w3.org/TR/SVG11/implnote.html#ArcImplementationNotes
	// 1) Compute x1', y1'
	const float x1p = cosrx * dx / 2.0f + sinrx * dy / 2.0f;
	const float y1p = -sinrx * dx / 2.0f + cosrx * dy / 2.0f;
	d = (x1p * x1p) / (rx * rx) + (y1p * y1p) / (ry * ry);
	if (d > 1) {
		d = bx::sqrt(d);
		rx *= d;
		ry *= d;
	}

	// 2) Compute cx', cy'
	float s = 0.0f;
	float sa = (rx * rx) * (ry * ry) - (rx * rx) * (y1p * y1p) - (ry * ry) * (x1p * x1p);
	const float sb = (rx * rx) * (y1p * y1p) + (ry * ry) * (x1p * x1p);
	if (sa < 0.0f) {
		sa = 0.0f;
	}
	if (sb > 0.0f) {
		s = bx::sqrt(sa / sb);
	}
	if (fa == fs) {
		s = -s;
	}
	const float cxp = s * rx * y1p / ry;
	const float cyp = s * -ry * x1p / rx;

	// 3) Compute cx,cy from cx',cy'
	const float cx = (x1 + x2) / 2.0f + cosrx * cxp - sinrx * cyp;
	const float cy = (y1 + y2) / 2.0f + sinrx * cxp + cosrx * cyp;

	// 4) Calculate theta1, and delta theta.
	const float ux = (x1p - cxp) / rx;
	const float uy = (y1p - cyp) / ry;
	const float vx = (-x1p - cxp) / rx;
	const float vy = (-y1p - cyp) / ry;
	const float a1 = nsvg__vecang(1.0f, 0.0f, ux, uy); // Initial angle

	float da = nsvg__vecang(ux, uy, vx, vy);     // Delta angle
	if (fs == 0 && da > 0) {
		da -= bx::kPi2;
	} else if (fs == 1 && da < 0) {
		da += bx::kPi2;
	}

	// Approximate the arc using cubic spline segments.
	float t[6];
	t[0] = cosrx; 
	t[1] = sinrx;
	t[2] = -sinrx; 
	t[3] = cosrx;
	t[4] = cx; 
	t[5] = cy;

	// Split arc into max 90 degree segments.
	// The loop assumes an iteration per end point (including start and end), this +1.
	const int ndivs = (int)(bx::abs(da) / bx::kPiHalf + 1.0f);
	const float hda = (da / (float)ndivs) / 2.0f;
	float kappa = bx::abs(4.0f / 3.0f * (1.0f - bx::cos(hda)) / bx::sin(hda));
	if (da < 0.0f) {
		kappa = -kappa;
	}

	float px = 0.0f;
	float py = 0.0f;
	float ptanx = 0.0f;
	float ptany = 0.0f;

	PathCmd* nextCmd = nullptr;
	if (ndivs > 1) {
		PathCmd* newCommands = pathInsertCommands(path, cmdID + 1, ndivs - 1);
		nextCmd = newCommands - 1; // Replace existing command.
	} else {
		nextCmd = &path->m_Commands[cmdID];
	}

	for (int i = 0; i <= ndivs; i++) {
		const float a = a1 + da * ((float)i / (float)ndivs);
		dx = bx::cos(a);
		dy = bx::sin(a);

		const float dxrx = dx * rx;
		const float dyry = dy * ry;
		const float x = dxrx * t[0] + dyry * t[2] + t[4];
		const float y = dxrx * t[1] + dyry * t[3] + t[5];

		const float dyrxkappa = dy * rx * kappa;
		const float dxrykappa = dx * ry * kappa;
		const float tanx = dxrykappa * t[2] - dyrxkappa * t[0];
		const float tany = dxrykappa * t[3] - dyrxkappa * t[1];

		if (i > 0) {
			nextCmd->m_Type = PathCmdType::CubicTo;
			nextCmd->m_Data[0] = px + ptanx;
			nextCmd->m_Data[1] = py + ptany;
			nextCmd->m_Data[2] = x - tanx;
			nextCmd->m_Data[3] = y - tany;
			nextCmd->m_Data[4] = x;
			nextCmd->m_Data[5] = y;
			++nextCmd;
		}

		px = x;
		py = y;
		ptanx = tanx;
		ptany = tany;
	}
}
}
