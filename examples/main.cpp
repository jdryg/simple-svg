#include <stdint.h>
#include <stdio.h>
#include <bx/allocator.h>
#include <bx/file.h>
#include <bx/timer.h>
#include <ssvg/ssvg.h>

bx::DefaultAllocator g_Allocator;

uint8_t* loadFile(const bx::FilePath& filePath)
{
	bx::Error err;
	bx::FileReader reader;
	if (!reader.open(filePath, &err)) {
		return nullptr;
	}

	int32_t fileSize = (int32_t)reader.seek(0, bx::Whence::End);
	reader.seek(0, bx::Whence::Begin);

	uint8_t* buffer = (uint8_t*)BX_ALLOC(&g_Allocator, fileSize + 1);
	reader.read(buffer, fileSize, &err);
	buffer[fileSize] = 0;

	reader.close();

	return buffer;
}

bool testParser(const char* filename)
{
	printf("Loading \"%s\"...\n", filename);

	uint8_t* svgFileBuffer = loadFile(bx::FilePath(filename));
	if (!svgFileBuffer) {
		printf("(x) Failed to load svg file.\n");
		return false;
	}

	ssvg::Image* img = nullptr;

	int64_t startTime = bx::getHPCounter();
	{
		img = ssvg::imageLoad((char*)svgFileBuffer);
	}
	int64_t deltaTime = bx::getHPCounter() - startTime;

	if (!img) {
		printf("(x) Failed to parse svg file.\n");
		return false;
	}

	double t = (double)deltaTime / (double)bx::getHPFrequency();
	printf("- Time: %g msec\n", t * 1000.0f);
	printf("- Root element contains %d shapes\n", img->m_ShapeList.m_NumShapes);

	ssvg::imageDestroy(img);

	BX_FREE(&g_Allocator, svgFileBuffer);

	return true;
}

bool testBuilder(const char* filename)
{
	ssvg::ShapeAttributes defaultAttrs;
	bx::memSet(&defaultAttrs, 0, sizeof(ssvg::ShapeAttributes));
	defaultAttrs.m_StrokeWidth = 1.0f;
	defaultAttrs.m_StrokeMiterLimit = 4.0f;
	defaultAttrs.m_StrokeOpacity = 1.0f;
	defaultAttrs.m_StrokePaint.m_Type = ssvg::PaintType::Color;
	defaultAttrs.m_StrokePaint.m_ColorABGR = 0xFF000000; // Black
	defaultAttrs.m_StrokeLineCap = ssvg::LineCap::Butt;
	defaultAttrs.m_StrokeLineJoin = ssvg::LineJoin::Miter;
	defaultAttrs.m_FillOpacity = 1.0f;
	defaultAttrs.m_FillPaint.m_Type = ssvg::PaintType::None;
	defaultAttrs.m_FillPaint.m_ColorABGR = 0x00000000;
	ssvg::transformIdentity(&defaultAttrs.m_Transform[0]);

	ssvg::ShapeAttributes textAttrs;
	bx::memCopy(&textAttrs, &defaultAttrs, sizeof(ssvg::ShapeAttributes));
	ssvg::shapeAttrsSetFontFamily(&textAttrs, "sans-serif");
	textAttrs.m_FontSize = 20.0f;
	textAttrs.m_FillPaint.m_Type = ssvg::PaintType::Color;
	textAttrs.m_FillPaint.m_ColorABGR = 0xFF000000;
	textAttrs.m_StrokePaint.m_Type = ssvg::PaintType::None;

	ssvg::Image* img = ssvg::imageCreate();

	ssvg::ShapeList* imgShapeList = &img->m_ShapeList;

	// Add shapes to the image shape list
	{
		uint32_t rectID = ssvg::shapeListAddRect(imgShapeList, &defaultAttrs, 100.0f, 100.0f, 200.0f, 200.0f, 0.0f, 0.0f);
		uint32_t circleID = ssvg::shapeListAddCircle(imgShapeList, &defaultAttrs, 200.0f, 200.0f, 80.0f);

		// Path
		uint32_t pathID = ssvg::shapeListAddPath(imgShapeList, &defaultAttrs, nullptr, 0);
		ssvg::Path* path = &imgShapeList->m_Shapes[pathID].m_Path;
		ssvg::pathMoveTo(path, 0.0f, 0.0f);
		ssvg::pathLineTo(path, 10.0f, 10.0f);
		ssvg::pathCubicTo(path, 10.0f, 20.0f, 30.0f, 40.0f, 50.0f, 50.0f);
		ssvg::pathClose(path);

		// Text
		ssvg::shapeListAddText(imgShapeList, &textAttrs, 200.0f, 50.0f, ssvg::TextAnchor::Start, "This is a test string");
	}

	// Add shapes to a group
	{
		uint32_t groupID = ssvg::shapeListAddGroup(imgShapeList, &defaultAttrs, nullptr, 0);

		float groupTransform[6] = { 1.0f, 0.0f, 0.0f, 1.0f, 400.0f, 0.0f };
		bx::memCopy(&imgShapeList->m_Shapes[groupID].m_Attrs.m_Transform[0], &groupTransform[0], sizeof(float) * 6);

		ssvg::ShapeList* groupShapeList = &imgShapeList->m_Shapes[groupID].m_ShapeList;
		uint32_t rectID = ssvg::shapeListAddRect(groupShapeList, &defaultAttrs, 100.0f, 100.0f, 200.0f, 200.0f, 0.0f, 0.0f);
		uint32_t circleID = ssvg::shapeListAddCircle(groupShapeList, &defaultAttrs, 200.0f, 200.0f, 80.0f);
	}

	// Add shapes to a group (alt version)
	{
		// Create a temporary shape list
		ssvg::ShapeList tempShapeList;
		bx::memSet(&tempShapeList, 0, sizeof(ssvg::ShapeList));
		uint32_t rectID = ssvg::shapeListAddRect(&tempShapeList, &defaultAttrs, 100.0f, 100.0f, 200.0f, 200.0f, 0.0f, 0.0f);
		uint32_t circleID = ssvg::shapeListAddCircle(&tempShapeList, &defaultAttrs, 200.0f, 200.0f, 80.0f);

		// Add a new group using the shapes from the temp shape list
		uint32_t groupID = ssvg::shapeListAddGroup(imgShapeList, &defaultAttrs, tempShapeList.m_Shapes, tempShapeList.m_NumShapes);

		// Free the temp shape list
		ssvg::shapeListFree(&tempShapeList);

		// Transform the group
		float groupTransform[6] = { 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 400.0f };
		bx::memCopy(&imgShapeList->m_Shapes[groupID].m_Attrs.m_Transform[0], &groupTransform[0], sizeof(float) * 6);
	}

	bx::Error err;
	bx::FileWriter fileWriter;
	if (!fileWriter.open(bx::FilePath(filename), false, &err)) {
		printf("Failed to open file for writing.\n");
		return false;
	}

	ssvg::imageSave(img, &fileWriter);

	fileWriter.close();

	ssvg::imageDestroy(img);

	return true;
}

bool testRoundTrip(const char* input, const char* output)
{
	printf("Converting \"%s\" to \"%s\"...\n", input, output);

	uint8_t* svgFileBuffer = loadFile(bx::FilePath(input));
	if (!svgFileBuffer) {
		printf("(x) Failed to load svg file.\n");
		return false;
	}

	ssvg::Image* img = ssvg::imageLoad((char*)svgFileBuffer);
	if (!img) {
		printf("(x) Failed to parse svg file.\n");
		return false;
	}

	bx::Error err;
	bx::FileWriter fileWriter;
	if (!fileWriter.open(bx::FilePath(output), false, &err)) {
		printf("Failed to open file for writing.\n");
		return false;
	}

	ssvg::imageSave(img, &fileWriter);

	fileWriter.close();

	ssvg::imageDestroy(img);

	BX_FREE(&g_Allocator, svgFileBuffer);

	return true;
}

int main()
{
	ssvg::ShapeAttributes defaultAttrs;
	bx::memSet(&defaultAttrs, 0, sizeof(ssvg::ShapeAttributes));
	defaultAttrs.m_StrokeWidth = 1.0f;
	defaultAttrs.m_StrokeMiterLimit = 4.0f;
	defaultAttrs.m_StrokeOpacity = 1.0f;
	defaultAttrs.m_StrokePaint.m_Type = ssvg::PaintType::None;
	defaultAttrs.m_StrokePaint.m_ColorABGR = 0x00000000;
	defaultAttrs.m_StrokeLineCap = ssvg::LineCap::Butt;
	defaultAttrs.m_StrokeLineJoin = ssvg::LineJoin::Miter;
	defaultAttrs.m_FillOpacity = 1.0f;
	defaultAttrs.m_FillPaint.m_Type = ssvg::PaintType::None;
	defaultAttrs.m_FillPaint.m_ColorABGR = 0x00000000;
	ssvg::transformIdentity(&defaultAttrs.m_Transform[0]);
	shapeAttrsSetFontFamily(&defaultAttrs, "sans-serif");

	ssvg::initLib(&g_Allocator, &defaultAttrs);

	testParser("./Ghostscript_Tiger.svg");
	testBuilder("./output.svg");
	testRoundTrip("./Ghostscript_Tiger.svg", "./tiger.svg");

	testParser("./tiger.svg");

	return 0;
}
