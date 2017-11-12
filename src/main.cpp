#include <stdint.h>
#include <stdio.h>
#include <bx/allocator.h>
#include <bx/file.h>
#include <bx/timer.h>
#include "svg/svg.h"

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

bool testBuilder()
{
	ssvg::Image* img = ssvg::imageCreate();

	ssvg::ShapeList* imgShapeList = &img->m_ShapeList;

	// Add shapes to the image shape list
	{
		uint32_t rectID = ssvg::shapeListAddRect(imgShapeList, nullptr, 100.0f, 100.0f, 200.0f, 200.0f, 0.0f, 0.0f);
		uint32_t circleID = ssvg::shapeListAddCircle(imgShapeList, nullptr, 200.0f, 200.0f, 80.0f);
	}

	// Add shapes to a group
	{
		uint32_t groupID = ssvg::shapeListAddGroup(imgShapeList, nullptr, nullptr, 0);

		float groupTransform[6] = { 1.0f, 0.0f, 0.0f, 1.0f, 400.0f, 0.0f };
		bx::memCopy(&imgShapeList->m_Shapes[groupID].m_Attrs.m_Transform[0], &groupTransform[0], sizeof(float) * 6);

		ssvg::ShapeList* groupShapeList = &imgShapeList->m_Shapes[groupID].m_ShapeList;
		uint32_t rectID = ssvg::shapeListAddRect(groupShapeList, nullptr, 100.0f, 100.0f, 200.0f, 200.0f, 0.0f, 0.0f);
		uint32_t circleID = ssvg::shapeListAddCircle(groupShapeList, nullptr, 200.0f, 200.0f, 80.0f);
	}

	// Add shapes to a group (alt version)
	{
		// Create a temporary shape list
		ssvg::ShapeList tempShapeList;
		uint32_t rectID = ssvg::shapeListAddRect(&tempShapeList, nullptr, 100.0f, 100.0f, 200.0f, 200.0f, 0.0f, 0.0f);
		uint32_t circleID = ssvg::shapeListAddCircle(&tempShapeList, nullptr, 200.0f, 200.0f, 80.0f);

		// Add a new group using the shapes from the temp shape list
		uint32_t groupID = ssvg::shapeListAddGroup(imgShapeList, nullptr, tempShapeList.m_Shapes, tempShapeList.m_NumShapes);

		// Free the temp shape list
		ssvg::shapeListFree(&tempShapeList);

		// Transform the group
		float groupTransform[6] = { 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 400.0f };
		bx::memCopy(&imgShapeList->m_Shapes[groupID].m_Attrs.m_Transform[0], &groupTransform[0], sizeof(float) * 6);
	}

	ssvg::imageDestroy(img);

	return true;
}

int main()
{
	ssvg::initLib(&g_Allocator);

	testParser("./Ghostscript_Tiger.svg");
	
	testBuilder();

	return 0;
}
