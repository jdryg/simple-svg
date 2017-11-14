#include "svg.h"
#include <bx/readerwriter.h>
#include <bx/string.h>

namespace ssvg
{
struct SaveAttr
{
	enum Enum : uint32_t
	{
		ID = 0x00000001,
		Transform = 0x00000002,
		Stroke = 0x00000004,
		Fill = 0x00000008,
		Font = 0x00000010,

		ConditionalPaints = 0x80000000, // If set, and PaintType == None || Transparent don't save stroke-width, stroke-opacity, etc.

		// Common combinations
		Unique = Transform | ID,
		Shape = Unique | Stroke | Fill,
		All = Shape | Font
	};
};

static const char* baseProfileToString(BaseProfile::Enum bp)
{
	switch (bp) {
	case BaseProfile::Basic:
		return "basic";
	case BaseProfile::Full:
		return "full";
	case BaseProfile::Tiny:
		return "tiny";
	}

	SVG_WARN(false, "Unknown baseProfile value");

	return "full";
}

static const char* lineJoinToString(LineJoin::Enum join)
{
	switch (join) {
	case LineJoin::Miter:
		return "miter";
	case LineJoin::Bevel:
		return "bevel";
	case LineJoin::Round:
		return "round";
	}

	SVG_WARN(false, "Unknown stroke-linejoin value");

	return "miter";
}

static const char* lineCapToString(LineCap::Enum cap)
{
	switch (cap) {
	case LineCap::Butt:
		return "butt";
	case LineCap::Square:
		return "square";
	case LineCap::Round:
		return "Round";
	}

	SVG_WARN(false, "Unknown stroke-linecap value");

	return "butt";
}

static bool transformIsIdentity(const float* transform)
{
	return transform[0] == 1.0f
		&& transform[1] == 0.0f
		&& transform[2] == 0.0f
		&& transform[3] == 1.0f
		&& transform[4] == 0.0f
		&& transform[5] == 0.0f;
}

static void colorToHexString(char* str, uint32_t len, uint32_t abgr)
{
	const uint32_t r = abgr & 0x000000FF;
	const uint32_t g = (abgr >> 8) & 0x000000FF;
	const uint32_t b = (abgr >> 16) & 0x000000FF;

	bx::snprintf(str, len, "#%02X%02X%02X", r, g, b);
}

bool pointListToString(const PointList* pointList, bx::WriterI* writer)
{
	const uint32_t numPoints = pointList->m_NumPoints;
	for (uint32_t i = 0; i < numPoints; ++i) {
		const float* coords = &pointList->m_Coords[i * 2];
		bx::writePrintf(writer, "%g,%g ", coords[0], coords[1]);
	}

	return true;
}

bool pathToString(const Path* path, bx::WriterI* writer)
{
	const uint32_t numCommands = path->m_NumCommands;
	for (uint32_t iCmd = 0; iCmd < numCommands; ++iCmd) {
		const PathCmd* cmd = &path->m_Commands[iCmd];

		const PathCmdType::Enum type = cmd->m_Type;
		const float* data = &cmd->m_Data[0];
		switch (type) {
		case PathCmdType::MoveTo:
			bx::writePrintf(writer, "M %g %g ", data[0], data[1]);
			break;
		case PathCmdType::LineTo:
			bx::writePrintf(writer, "L %g %g ", data[0], data[1]);
			break;
		case PathCmdType::CubicTo:
			bx::writePrintf(writer, "C %g %g, %g %g, %g %g ", data[0], data[1], data[2], data[3], data[4], data[5]);
			break;
		case PathCmdType::QuadraticTo:
			bx::writePrintf(writer, "Q %g %g, %g %g ", data[0], data[1], data[2], data[3]);
			break;
		case PathCmdType::ArcTo:
			bx::writePrintf(writer, "A %g %g %g %d %d %g %g ", data[0], data[1], data[2], (int)data[3], (int)data[4], data[5], data[6]);
			break;
		case PathCmdType::ClosePath:
			bx::writePrintf(writer, "Z ");
			break;
		default:
			SVG_WARN(false, "Unknown path command");
			break;
		}
	}

	return true;
}

static bool writeShapeAttributes(bx::WriterI* writer, const ShapeAttributes* attrs, uint32_t flags)
{
	const bool conditionalPaints = (flags & SaveAttr::ConditionalPaints) != 0;

	if ((flags & SaveAttr::ID) != 0 && attrs->m_ID[0] != '\0') {
		bx::writePrintf(writer, "id=\"%s\" ", attrs->m_ID);
	}

	if ((flags & SaveAttr::Transform) != 0 && !transformIsIdentity(&attrs->m_Transform[0])) {
		bx::writePrintf(writer, "transform=\"matrix(%g,%g,%g,%g,%g,%g)\" "
			, attrs->m_Transform[0]
			, attrs->m_Transform[1]
			, attrs->m_Transform[2]
			, attrs->m_Transform[3]
			, attrs->m_Transform[4]
			, attrs->m_Transform[5]);
	}

	if ((flags & SaveAttr::Stroke) != 0) {
		const PaintType::Enum strokeType = attrs->m_StrokePaint.m_Type;

		if (strokeType == PaintType::None) {
			bx::writePrintf(writer, "stroke=\"none\" ");
		} else if (strokeType == PaintType::Transparent) {
			bx::writePrintf(writer, "stroke=\"transparent\" ");
		} else if (strokeType == PaintType::Color) {
			char hexColor[32];
			colorToHexString(hexColor, 32, attrs->m_StrokePaint.m_ColorABGR);
			bx::writePrintf(writer, "stroke=\"%s\" ", hexColor);
		}

		const bool saveExtra = !conditionalPaints || (conditionalPaints && strokeType != PaintType::None && strokeType != PaintType::Transparent);
		if (saveExtra) {
			if (attrs->m_StrokeMiterLimit >= 1.0f && attrs->m_StrokeMiterLimit != 4.0f) {
				bx::writePrintf(writer, "stroke-miterlimit=\"%g\" ", attrs->m_StrokeMiterLimit);
			}

			// TODO: Check if there are any invalid values for those.
			bx::writePrintf(writer, "stroke-width=\"%g\" ", attrs->m_StrokeWidth);
			bx::writePrintf(writer, "stroke-opacity=\"%g\" ", attrs->m_StrokeOpacity);
			bx::writePrintf(writer, "stroke-linejoin=\"%s\" ", lineJoinToString(attrs->m_StrokeLineJoin));
			bx::writePrintf(writer, "stroke-linecap=\"%s\" ", lineCapToString(attrs->m_StrokeLineCap));
		}
	}

	if ((flags & SaveAttr::Fill) != 0) {
		const PaintType::Enum fillType = attrs->m_FillPaint.m_Type;

		if (fillType == PaintType::None) {
			bx::writePrintf(writer, "fill=\"none\" ");
		} else if (fillType == PaintType::Transparent) {
			bx::writePrintf(writer, "fill=\"transparent\" ");
		} else if (fillType == PaintType::Color) {
			char hexColor[32];
			colorToHexString(hexColor, 32, attrs->m_FillPaint.m_ColorABGR);
			bx::writePrintf(writer, "fill=\"%s\" ", hexColor);
		}

		const bool saveExtra = !conditionalPaints || (conditionalPaints && fillType != PaintType::None && fillType != PaintType::Transparent);
		if (saveExtra) {
			bx::writePrintf(writer, "fill-opacity=\"%g\" "
				, attrs->m_FillOpacity);
		}
	}

	if ((flags & SaveAttr::Font) != 0) {
		if (attrs->m_FontFamily[0] != '\0') {
			bx::writePrintf(writer, "font-family=\"%s\" ", attrs->m_FontFamily);
		}

		if (attrs->m_FontSize != 0.0f) {
			bx::writePrintf(writer, "font-size=\"%g\" ", attrs->m_FontSize);
		}
	}

	return true;
}

bool writePointList(bx::WriterI* writer, const PointList* pointList)
{
	bx::writePrintf(writer, "points=\"");
	if (!pointListToString(pointList, writer)) {
		return false;
	}
	bx::writePrintf(writer, "\" ");

	return true;
}

bool writePath(bx::WriterI* writer, const Path* path)
{
	bx::writePrintf(writer, "d=\"");
	if (!pathToString(path, writer)) {
		return false;
	}
	bx::writePrintf(writer, "\" ");

	return true;
}

bool writeShapeList(bx::WriterI* writer, const ShapeList* shapeList, uint32_t indentation)
{
	const uint32_t numShapes = shapeList->m_NumShapes;
	for (uint32_t iShape = 0; iShape < numShapes; ++iShape) {
		const Shape* shape = &shapeList->m_Shapes[iShape];

		const ShapeType::Enum shapeType = shape->m_Type;
		switch (shapeType) {
		case ShapeType::Group:
			bx::writePrintf(writer, "%*s<g ", indentation, "");
			if (!writeShapeAttributes(writer, &shape->m_Attrs, SaveAttr::All)) {
				return false;
			}
			bx::writePrintf(writer, ">\n");
			
			if (!writeShapeList(writer, &shape->m_ShapeList, indentation + 2)) {
				return false;
			}

			bx::writePrintf(writer, "%*s</g>\n", indentation, "");
			break;
		case ShapeType::Rect:
			bx::writePrintf(writer, "%*s<rect ", indentation, "");
			if (!writeShapeAttributes(writer, &shape->m_Attrs, SaveAttr::Shape | SaveAttr::ConditionalPaints)) {
				return false;
			}
			bx::writePrintf(writer, "x=\"%g\" y=\"%g\" width=\"%g\" height=\"%g\" "
				, shape->m_Rect.x
				, shape->m_Rect.y
				, shape->m_Rect.width
				, shape->m_Rect.height);

			if (shape->m_Rect.rx != 0.0f) {
				bx::writePrintf(writer, "rx=\"%g\" ", shape->m_Rect.rx);
			}
			if (shape->m_Rect.ry != 0.0f) {
				bx::writePrintf(writer, "ry=\"%g\" ", shape->m_Rect.ry);
			}
			bx::writePrintf(writer, "/>\n");
			break;
		case ShapeType::Circle:
			bx::writePrintf(writer, "%*s<circle ", indentation, "");
			if (!writeShapeAttributes(writer, &shape->m_Attrs, SaveAttr::Shape | SaveAttr::ConditionalPaints)) {
				return false;
			}
			bx::writePrintf(writer, "cx=\"%g\" cy=\"%g\" r=\"%g\" />\n"
				, shape->m_Circle.cx
				, shape->m_Circle.cy
				, shape->m_Circle.r);
			break;
		case ShapeType::Ellipse:
			bx::writePrintf(writer, "%*s<ellipse ", indentation, "");
			if (!writeShapeAttributes(writer, &shape->m_Attrs, SaveAttr::Shape | SaveAttr::ConditionalPaints)) {
				return false;
			}
			bx::writePrintf(writer, "cx=\"%g\" cy=\"%g\" rx=\"%g\" ry=\"%g\" />\n"
				, shape->m_Ellipse.cx
				, shape->m_Ellipse.cy
				, shape->m_Ellipse.rx
				, shape->m_Ellipse.ry);
			break;
		case ShapeType::Line:
			bx::writePrintf(writer, "%*s<line ", indentation, "");
			if (!writeShapeAttributes(writer, &shape->m_Attrs, SaveAttr::Shape | SaveAttr::ConditionalPaints)) {
				return false;
			}
			bx::writePrintf(writer, "x1=\"%g\" y1=\"%g\" x2=\"%g\" y2=\"%g\" />\n"
				, shape->m_Line.x1
				, shape->m_Line.y1
				, shape->m_Line.x2
				, shape->m_Line.y2);
			break;
		case ShapeType::Polyline:
			bx::writePrintf(writer, "%*s<polyline ", indentation, "");
			if (!writeShapeAttributes(writer, &shape->m_Attrs, SaveAttr::Shape | SaveAttr::ConditionalPaints)) {
				return false;
			}
			if (!writePointList(writer, &shape->m_PointList)) {
				return false;
			}
			bx::writePrintf(writer, "/>\n");
			break;
		case ShapeType::Polygon:
			bx::writePrintf(writer, "%*s<polygon ", indentation, "");
			if (!writeShapeAttributes(writer, &shape->m_Attrs, SaveAttr::Shape | SaveAttr::ConditionalPaints)) {
				return false;
			}
			if (!writePointList(writer, &shape->m_PointList)) {
				return false;
			}
			bx::writePrintf(writer, "/>\n");
			break;
		case ShapeType::Path:
			bx::writePrintf(writer, "%*s<path ", indentation, "");
			if (!writeShapeAttributes(writer, &shape->m_Attrs, SaveAttr::Shape | SaveAttr::ConditionalPaints)) {
				return false;
			}
			if (!writePath(writer, &shape->m_Path)) {
				return false;
			}
			bx::writePrintf(writer, "/>\n");
			break;
		case ShapeType::Text:
			break;
		default:
			SVG_WARN(false, "Unknown shape type");
		}
	}

	return true;
}

bool imageSave(const Image* img, bx::WriterI* writer)
{
	bx::writePrintf(writer, "<svg ");
	if (img->m_Width != 0.0f) {
		bx::writePrintf(writer, "width=\"%g\" ", img->m_Width);
	}
	if (img->m_Height != 0.0f) {
		bx::writePrintf(writer, "height=\"%g\" ", img->m_Height);
	}
	if (img->m_VerMajor != 0) {
		bx::writePrintf(writer, "version=\"%u.%u\" ", img->m_VerMajor, img->m_VerMinor);
	}
	if (img->m_BaseProfile != BaseProfile::None) {
		bx::writePrintf(writer, "baseProfile=\"%s\" ", baseProfileToString(img->m_BaseProfile));
	}
	bx::writePrintf(writer, "xmlns=\"http://www.w3.org/2000/svg\">\n");

	if (!writeShapeList(writer, &img->m_ShapeList, 1)) {
		return false;
	}

	bx::writePrintf(writer, "</svg>\n");

	return true;
}
}
