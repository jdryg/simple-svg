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

int main()
{
	ssvg::initLib(&g_Allocator);

	uint8_t* svgFileBuffer = loadFile(bx::FilePath("./Ghostscript_Tiger.svg"));
	if (!svgFileBuffer) {
		printf("(x) Failed to load svg file.\n");
		return -1;
	}

	ssvg::Image* img = nullptr;

	int64_t startTime = bx::getHPCounter();
	{
		img = ssvg::loadImage((char*)svgFileBuffer);
	}
	int64_t deltaTime = bx::getHPCounter() - startTime;

	double t = (double)deltaTime / (double)bx::getHPFrequency();
	printf("Time: %g msec\n", t * 1000.0f);

	if (img) {
		printf("Parsed %d shapes\n", img->m_ShapeList.m_NumShapes);
	}

	ssvg::destroyImage(img);

	BX_FREE(&g_Allocator, svgFileBuffer);

	return 0;
}
