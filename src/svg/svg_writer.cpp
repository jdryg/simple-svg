#include "svg.h"
#include <bx/readerwriter.h>
#include <bx/string.h>

namespace ssvg
{
extern ShapeAttributes s_DefaultAttrs;

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
		All = Shape | Font,
		Text = Unique | Fill | Font | ConditionalPaints
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

static const char* textAnchorToString(TextAnchor::Enum anchor)
{
	switch (anchor) {
	case TextAnchor::Start:
		return "start";
	case TextAnchor::Middle:
		return "middle";
	case TextAnchor::End:
		return "end";
	}

	SVG_WARN(false, "Unknown text-anchor value");

	return "start";
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
#if SSVG_CONFIG_MINIFY_PATHS
		bx::writePrintf(writer, "%g,%g ", coords[0], coords[1]);
#else
		bx::writePrintf(writer, "%g, %g ", coords[0], coords[1]);
#endif
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
#if SSVG_CONFIG_MINIFY_PATHS
			bx::writePrintf(writer, "M%g %g", data[0], data[1]);
#else
			bx::writePrintf(writer, "M %g %g ", data[0], data[1]);
#endif
			break;
		case PathCmdType::LineTo:
#if SSVG_CONFIG_MINIFY_PATHS
			bx::writePrintf(writer, "L%g %g", data[0], data[1]);
#else
			bx::writePrintf(writer, "L %g %g ", data[0], data[1]);
#endif
			break;
		case PathCmdType::CubicTo:
#if SSVG_CONFIG_MINIFY_PATHS
			bx::writePrintf(writer, "C%g %g,%g %g,%g %g", data[0], data[1], data[2], data[3], data[4], data[5]);
#else
			bx::writePrintf(writer, "C %g %g, %g %g, %g %g ", data[0], data[1], data[2], data[3], data[4], data[5]);
#endif
			break;
		case PathCmdType::QuadraticTo:
#if SSVG_CONFIG_MINIFY_PATHS
			bx::writePrintf(writer, "Q%g %g,%g %g", data[0], data[1], data[2], data[3]);
#else
			bx::writePrintf(writer, "Q %g %g, %g %g ", data[0], data[1], data[2], data[3]);
#endif
			break;
		case PathCmdType::ArcTo:
#if SSVG_CONFIG_MINIFY_PATHS
			bx::writePrintf(writer, "A%g %g %g %d %d %g %g", data[0], data[1], data[2], (int)data[3], (int)data[4], data[5], data[6]);
#else
			bx::writePrintf(writer, "A %g %g %g %d %d %g %g ", data[0], data[1], data[2], (int)data[3], (int)data[4], data[5], data[6]);
#endif
			break;
		case PathCmdType::ClosePath:
			bx::writePrintf(writer, "Z");
			break;
		default:
			SVG_WARN(false, "Unknown path command");
			break;
		}
	}

	return true;
}

static bool writeShapeAttributes(bx::WriterI* writer, const ShapeAttributes* attrs, const ShapeAttributes* parentAttrs, uint32_t flags)
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
		const PaintType::Enum parentStrokeType = parentAttrs->m_StrokePaint.m_Type;

		if (strokeType == PaintType::None && parentStrokeType != PaintType::None) {
			bx::writePrintf(writer, "stroke=\"none\" ");
		} else if (strokeType == PaintType::Transparent && parentStrokeType != PaintType::Transparent) {
			bx::writePrintf(writer, "stroke=\"transparent\" ");
		} else if (strokeType == PaintType::Color) {
			const uint32_t abgr = attrs->m_StrokePaint.m_ColorABGR;
			if (parentStrokeType != PaintType::Color || (parentStrokeType == PaintType::Color && parentAttrs->m_StrokePaint.m_ColorABGR != abgr)) {
				char hexColor[32];
				colorToHexString(hexColor, 32, abgr);
				bx::writePrintf(writer, "stroke=\"%s\" ", hexColor);
			}
		}

		const bool saveExtra = !conditionalPaints || (conditionalPaints && strokeType != PaintType::None && strokeType != PaintType::Transparent);
		if (saveExtra) {
			const float miterLimit = attrs->m_StrokeMiterLimit;
			if (miterLimit >= 1.0f && parentAttrs->m_StrokeMiterLimit != miterLimit) {
				bx::writePrintf(writer, "stroke-miterlimit=\"%g\" ", miterLimit);
			}

			const float width = attrs->m_StrokeWidth;
			if (width >= 0.0f && parentAttrs->m_StrokeWidth != width) {
				bx::writePrintf(writer, "stroke-width=\"%g\" ", width);
			}

			const float opacity = attrs->m_StrokeOpacity;
			if (opacity >= 0.0f && opacity <= 1.0f && parentAttrs->m_StrokeOpacity != opacity) {
				bx::writePrintf(writer, "stroke-opacity=\"%g\" ", opacity);
			}

			const LineJoin::Enum lineJoin = attrs->m_StrokeLineJoin;
			if (lineJoin != parentAttrs->m_StrokeLineJoin) {
				bx::writePrintf(writer, "stroke-linejoin=\"%s\" ", lineJoinToString(lineJoin));
			}

			const LineCap::Enum lineCap = attrs->m_StrokeLineCap;
			if (lineCap != parentAttrs->m_StrokeLineCap) {
				bx::writePrintf(writer, "stroke-linecap=\"%s\" ", lineCapToString(lineCap));
			}
		}
	}

	if ((flags & SaveAttr::Fill) != 0) {
		const PaintType::Enum fillType = attrs->m_FillPaint.m_Type;
		const PaintType::Enum parentFillType = parentAttrs->m_FillPaint.m_Type;

		if (fillType == PaintType::None && parentFillType != PaintType::None) {
			bx::writePrintf(writer, "fill=\"none\" ");
		} else if (fillType == PaintType::Transparent && parentFillType != PaintType::Transparent) {
			bx::writePrintf(writer, "fill=\"transparent\" ");
		} else if (fillType == PaintType::Color) {
			const uint32_t abgr = attrs->m_FillPaint.m_ColorABGR;
			if (parentFillType != PaintType::Color || (parentFillType == PaintType::Color && parentAttrs->m_FillPaint.m_ColorABGR != abgr)) {
				char hexColor[32];
				colorToHexString(hexColor, 32, abgr);
				bx::writePrintf(writer, "fill=\"%s\" ", hexColor);
			}
		}

		const bool saveExtra = !conditionalPaints || (conditionalPaints && fillType != PaintType::None && fillType != PaintType::Transparent);
		if (saveExtra) {
			const float opacity = attrs->m_FillOpacity;
			if (opacity >= 0.0f && opacity <= 1.0f && opacity != parentAttrs->m_FillOpacity) {
				bx::writePrintf(writer, "fill-opacity=\"%g\" ", opacity);
			}
		}
	}

	if ((flags & SaveAttr::Font) != 0) {
		const char* fontFamily = attrs->m_FontFamily;
		if (fontFamily[0] != '\0' && bx::strCmp(fontFamily, parentAttrs->m_FontFamily)) {
			bx::writePrintf(writer, "font-family=\"%s\" ", fontFamily);
		}

		const float fontSize = attrs->m_FontSize;
		if (fontSize > 0.0f && fontSize != parentAttrs->m_FontSize) {
			bx::writePrintf(writer, "font-size=\"%g\" ", fontSize);
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

bool writeShapeList(bx::WriterI* writer, const ShapeList* shapeList, const ShapeAttributes* parentAttrs, uint32_t indentation)
{
	const uint32_t numShapes = shapeList->m_NumShapes;
	for (uint32_t iShape = 0; iShape < numShapes; ++iShape) {
		const Shape* shape = &shapeList->m_Shapes[iShape];

		const ShapeType::Enum shapeType = shape->m_Type;
		switch (shapeType) {
		case ShapeType::Group:
			bx::writePrintf(writer, "%*s<g ", indentation, "");
			if (!writeShapeAttributes(writer, &shape->m_Attrs, parentAttrs, SaveAttr::All)) {
				return false;
			}
			bx::writePrintf(writer, ">\n");
			
			if (!writeShapeList(writer, &shape->m_ShapeList, &shape->m_Attrs, indentation + 2)) {
				return false;
			}

			bx::writePrintf(writer, "%*s</g>\n", indentation, "");
			break;
		case ShapeType::Rect:
			bx::writePrintf(writer, "%*s<rect ", indentation, "");
			if (!writeShapeAttributes(writer, &shape->m_Attrs, parentAttrs, SaveAttr::Shape | SaveAttr::ConditionalPaints)) {
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
			if (!writeShapeAttributes(writer, &shape->m_Attrs, parentAttrs, SaveAttr::Shape | SaveAttr::ConditionalPaints)) {
				return false;
			}
			bx::writePrintf(writer, "cx=\"%g\" cy=\"%g\" r=\"%g\" />\n"
				, shape->m_Circle.cx
				, shape->m_Circle.cy
				, shape->m_Circle.r);
			break;
		case ShapeType::Ellipse:
			bx::writePrintf(writer, "%*s<ellipse ", indentation, "");
			if (!writeShapeAttributes(writer, &shape->m_Attrs, parentAttrs, SaveAttr::Shape | SaveAttr::ConditionalPaints)) {
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
			if (!writeShapeAttributes(writer, &shape->m_Attrs, parentAttrs, SaveAttr::Shape | SaveAttr::ConditionalPaints)) {
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
			if (!writeShapeAttributes(writer, &shape->m_Attrs, parentAttrs, SaveAttr::Shape | SaveAttr::ConditionalPaints)) {
				return false;
			}
			if (!writePointList(writer, &shape->m_PointList)) {
				return false;
			}
			bx::writePrintf(writer, "/>\n");
			break;
		case ShapeType::Polygon:
			bx::writePrintf(writer, "%*s<polygon ", indentation, "");
			if (!writeShapeAttributes(writer, &shape->m_Attrs, parentAttrs, SaveAttr::Shape | SaveAttr::ConditionalPaints)) {
				return false;
			}
			if (!writePointList(writer, &shape->m_PointList)) {
				return false;
			}
			bx::writePrintf(writer, "/>\n");
			break;
		case ShapeType::Path:
			bx::writePrintf(writer, "%*s<path ", indentation, "");
			if (!writeShapeAttributes(writer, &shape->m_Attrs, parentAttrs, SaveAttr::Shape | SaveAttr::ConditionalPaints)) {
				return false;
			}
			if (!writePath(writer, &shape->m_Path)) {
				return false;
			}
			bx::writePrintf(writer, "/>\n");
			break;
		case ShapeType::Text:
			bx::writePrintf(writer, "%*s<text ", indentation, "");
			if (!writeShapeAttributes(writer, &shape->m_Attrs, parentAttrs, SaveAttr::Text)) {
				return false;
			}
			bx::writePrintf(writer, "x=\"%g\" y=\"%g\" text-anchor=\"%s\">%s</text>\n"
				, shape->m_Text.x
				, shape->m_Text.y
				, textAnchorToString(shape->m_Text.m_Anchor)
				, shape->m_Text.m_String);
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
	if (img->m_ViewBox[2] > 0.0f && img->m_ViewBox[3] > 0.0f) {
		bx::writePrintf(writer, "viewBox=\"%g %g %g %g\" ", img->m_ViewBox[0], img->m_ViewBox[1], img->m_ViewBox[2], img->m_ViewBox[3]);
	}
	bx::writePrintf(writer, "xmlns=\"http://www.w3.org/2000/svg\">\n");

	if (!writeShapeList(writer, &img->m_ShapeList, &s_DefaultAttrs, 1)) {
		return false;
	}

	bx::writePrintf(writer, "</svg>\n");

	return true;
}
}
