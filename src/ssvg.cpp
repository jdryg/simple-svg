#include <ssvg/ssvg.h>
#include <bx/bx.h>
#include <bx/allocator.h>
#include <bx/string.h>
#include <bx/math.h>
#include <float.h> // FLT_MAX

namespace ssvg
{
struct ShapeAttributeFreeListNode
{
	ShapeAttributeFreeListNode* m_Next;
	ShapeAttributeFreeListNode* m_Prev;
	ShapeAttributes* m_Attrs;
	uint32_t m_NumAttrs;
	uint32_t m_FirstFreeID;
	uint32_t m_NumFree;
};

bx::AllocatorI* s_Allocator = nullptr;
static ShapeAttributeFreeListNode* s_ShapeAttrFreeListHead = nullptr;

static ShapeAttributes* shapeAttrsAlloc();
static void shapeAttrsFree(ShapeAttributes* attrs);

void transformIdentity(float* transform)
{
	bx::memSet(transform, 0, sizeof(float) * 6);
	transform[0] = 1.0f;
	transform[3] = 1.0f;
}

void transformTranslation(float* transform, float x, float y)
{
	transform[0] = 1.0f;
	transform[1] = 0.0f;
	transform[2] = 0.0f;
	transform[3] = 1.0f;
	transform[4] = x;
	transform[5] = y;
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

void transformTranslate(float* transform, float x, float y)
{
	float tmp[6];
	transformTranslation(&tmp[0], x, y);
	transformMultiply(transform, tmp);
}

void transformPoint(const float* transform, const float* localPos, float* globalPos)
{
	const float x = localPos[0];
	const float y = localPos[1];
	globalPos[0] = transform[0] * x + transform[2] * y + transform[4];
	globalPos[1] = transform[1] * x + transform[3] * y + transform[5];
}

void transformBoundingRect(const float* transform, const float* localRect, float* globalRect)
{
	float transformedRect[4];
	transformPoint(transform, &localRect[0], &transformedRect[0]);
	transformPoint(transform, &localRect[2], &transformedRect[2]);

	globalRect[0] = bx::min<float>(transformedRect[0], transformedRect[2]);
	globalRect[1] = bx::min<float>(transformedRect[1], transformedRect[3]);
	globalRect[2] = bx::max<float>(transformedRect[0], transformedRect[2]);
	globalRect[3] = bx::max<float>(transformedRect[1], transformedRect[3]);
}

Shape* shapeListAllocShape(ShapeList* shapeList, ShapeType::Enum type, const ShapeAttributes* parentAttrs)
{
	SSVG_CHECK(shapeList->m_NumShapes <= shapeList->m_Capacity, "Trying to expand a read-only shape list?");

	if (shapeList->m_NumShapes + 1 > shapeList->m_Capacity) {
		const uint32_t oldCapacity = shapeList->m_Capacity;

		// TODO: Since shapes are fairly large objects, check if allocating a constant amount each time
		// somehow helps.
		shapeList->m_Capacity = oldCapacity ? (oldCapacity * 3) / 2 : 4;
		shapeList->m_Shapes = (Shape*)BX_REALLOC(s_Allocator, shapeList->m_Shapes, sizeof(Shape) * shapeList->m_Capacity);
		bx::memSet(&shapeList->m_Shapes[oldCapacity], 0, sizeof(Shape) * (shapeList->m_Capacity - oldCapacity));
	}

	Shape* shape = &shapeList->m_Shapes[shapeList->m_NumShapes++];
	shape->m_Type = type;
	shape->m_Attrs = shapeAttrsAlloc();
	bx::memSet(shape->m_Attrs, 0, sizeof(ShapeAttributes));
	shape->m_Attrs->m_Parent = parentAttrs;
	shape->m_Attrs->m_Flags = AttribFlags::InheritAll;
	shape->m_Attrs->m_Opacity = 1.0f;
	shape->m_Attrs->m_ID[0] = '\0';
#if SSVG_CONFIG_CLASS_MAX_LEN
	shape->m_Attrs->m_Class[0] = '\0';
#endif
	transformIdentity(&shape->m_Attrs->m_Transform[0]);

	return shape;
}

void shapeListShrinkToFit(ShapeList* shapeList)
{
	SSVG_CHECK(shapeList->m_NumShapes <= shapeList->m_Capacity, "Trying to shrink a read-only shape list?");

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
	SSVG_CHECK(shapeList->m_NumShapes <= shapeList->m_Capacity, "Trying to free a read-only shape list?");

	const uint32_t n = shapeList->m_NumShapes;
	for (uint32_t i = 0; i < n; ++i) {
		Shape* shape = &shapeList->m_Shapes[i];
		shapeFree(shape);
	}

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

uint32_t shapeListMoveShapeToBack(ShapeList* shapeList, uint32_t shapeID)
{
	SSVG_CHECK(shapeID < shapeList->m_NumShapes, "Invalid shape ID");
	if (shapeID == 0 || shapeList->m_NumShapes <= 1) {
		return shapeID;
	}

	Shape tmp;
	bx::memCopy(&tmp, &shapeList->m_Shapes[shapeID - 1], sizeof(Shape));
	bx::memCopy(&shapeList->m_Shapes[shapeID - 1], &shapeList->m_Shapes[shapeID], sizeof(Shape));
	bx::memCopy(&shapeList->m_Shapes[shapeID], &tmp, sizeof(Shape));

	return shapeID - 1;
}

uint32_t shapeListMoveShapeToFront(ShapeList* shapeList, uint32_t shapeID)
{
	SSVG_CHECK(shapeID < shapeList->m_NumShapes, "Invalid shape ID");
	if (shapeID == shapeList->m_NumShapes - 1) {
		return shapeID;
	}

	Shape tmp;
	bx::memCopy(&tmp, &shapeList->m_Shapes[shapeID + 1], sizeof(Shape));
	bx::memCopy(&shapeList->m_Shapes[shapeID + 1], &shapeList->m_Shapes[shapeID], sizeof(Shape));
	bx::memCopy(&shapeList->m_Shapes[shapeID], &tmp, sizeof(Shape));

	return shapeID + 1;
}

void shapeListDeleteShape(ShapeList* shapeList, uint32_t shapeID)
{
	SSVG_CHECK(shapeID < shapeList->m_NumShapes, "Invalid shape ID");

	shapeFree(&shapeList->m_Shapes[shapeID]);

	const uint32_t numShapesToMove = shapeList->m_NumShapes - 1 - shapeID;
	if (numShapesToMove != 0) {
		bx::memMove(&shapeList->m_Shapes[shapeID], &shapeList->m_Shapes[shapeID + 1], sizeof(Shape) * numShapesToMove);
	}

	shapeList->m_NumShapes--;
}

void shapeListCalcBounds(ShapeList* shapeList, float* bounds)
{
	const uint32_t numShapes = shapeList->m_NumShapes;
	if (numShapes == 0) {
		bounds[0] = bounds[1] = bounds[2] = bounds[3] = 0.0f;
		return;
	}

	bounds[0] = FLT_MAX;
	bounds[1] = FLT_MAX;
	bounds[2] = -FLT_MAX;
	bounds[3] = -FLT_MAX;
	for (uint32_t i = 0; i < numShapes; ++i) {
		Shape* shape = &shapeList->m_Shapes[i];
		shapeUpdateBounds(shape);

		// Since this is a group, the child's bounding rect should be transformed using its
		// transformation matrix before calculating the group's local bounding rect.
		float childTransformedRect[4];
		transformBoundingRect(&shape->m_Attrs->m_Transform[0], &shape->m_BoundingRect[0], &childTransformedRect[0]);

		bounds[0] = bx::min<float>(bounds[0], childTransformedRect[0]);
		bounds[1] = bx::min<float>(bounds[1], childTransformedRect[1]);
		bounds[2] = bx::max<float>(bounds[2], childTransformedRect[2]);
		bounds[3] = bx::max<float>(bounds[3], childTransformedRect[3]);
	}
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

PathCmd* pathInsertCommands(Path* path, uint32_t at, uint32_t n)
{
	if (at == path->m_NumCommands) {
		// Insert at the end == alloc
		return pathAllocCommands(path, n);
	}

	const uint32_t numOldCommands = path->m_NumCommands;
	pathAllocCommands(path, n);
	bx::memMove(&path->m_Commands[at + n], &path->m_Commands[at], sizeof(PathCmd) * (numOldCommands - at));
	return &path->m_Commands[at];
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

inline uint32_t solveQuad(float a, float b, float c, float* t)
{
	if (bx::abs(a) < 1e-5f) {
		if (bx::abs(b) > 1e-5f) {
			t[0] = -c / b;
			return 1;
		}
	} else {
		const float desc = b * b - 4.0f * a * c;
		if (bx::abs(desc) > 1e-5f) {
			const float desc_sqrt = bx::sqrt(desc);
			t[0] = (-b + desc_sqrt) / (2.0f * a);
			t[1] = (-b - desc_sqrt) / (2.0f * a);

			return 2;
		}
	}

	return 0;
}

inline void evalCubicBezierAt(float t, const float* p0, const float* p1, const float* p2, const float* p3, float* p)
{
	const float t2 = t * t;
	const float t3 = t2 * t;
	const float one_t = 1.0f - t;
	const float one_t2 = one_t * one_t;
	const float one_t3 = one_t2 * one_t;

	const float a = one_t3;
	const float b = 3.0f * t * one_t2;
	const float c = 3.0f * t2 * one_t;
	const float d = t3;

	p[0] = a * p0[0] + b * p1[0] + c * p2[0] + d * p3[0];
	p[1] = a * p0[1] + b * p1[1] + c * p2[1] + d * p3[1];
}

inline void evalQuadraticBezierAt(float t, const float* p0, const float* p1, const float* p2, float* p)
{
	const float t2 = t * t;
	const float one_t = 1.0f - t;
	const float one_t2 = one_t * one_t;

	const float a = one_t2;
	const float b = 2.0f * one_t * t;
	const float c = t2;

	p[0] = a * p0[0] + b * p1[0] + c * p2[0];
	p[1] = a * p0[1] + b * p1[1] + c * p2[1];
}

void pathCalcBounds(const Path* path, float* bounds)
{
	const uint32_t numCommands = path->m_NumCommands;
	if (!numCommands) {
		// No commands -> invalid bounding rect
		bounds[0] = bounds[1] = FLT_MAX;
		bounds[2] = bounds[3] = -FLT_MAX;
		return;
	}

	const PathCmd* cmd = path->m_Commands;
	SSVG_CHECK(cmd->m_Type == PathCmdType::MoveTo, "First path command must be MoveTo");
	bounds[0] = bounds[2] = cmd->m_Data[0];
	bounds[1] = bounds[3] = cmd->m_Data[1];

	float last[2] = { cmd->m_Data[0], cmd->m_Data[1] };
	for (uint32_t iCmd = 1; iCmd < numCommands; ++iCmd) {
		cmd = &path->m_Commands[iCmd];

		switch (cmd->m_Type) {
		case PathCmdType::MoveTo:
		case PathCmdType::LineTo:
			bounds[0] = bx::min<float>(bounds[0], cmd->m_Data[0]);
			bounds[1] = bx::min<float>(bounds[1], cmd->m_Data[1]);
			bounds[2] = bx::max<float>(bounds[2], cmd->m_Data[0]);
			bounds[3] = bx::max<float>(bounds[3], cmd->m_Data[1]);

			last[0] = cmd->m_Data[0];
			last[1] = cmd->m_Data[1];
			break;
		case PathCmdType::CubicTo:
		{
			// Bezier end point
			bounds[0] = bx::min<float>(bounds[0], cmd->m_Data[4]);
			bounds[1] = bx::min<float>(bounds[1], cmd->m_Data[5]);
			bounds[2] = bx::max<float>(bounds[2], cmd->m_Data[4]);
			bounds[3] = bx::max<float>(bounds[3], cmd->m_Data[5]);

			// Extremities
			for (uint32_t dim = 0; dim < 2; ++dim) {
				const float c0 = last[dim];
				const float c1 = cmd->m_Data[dim + 0];
				const float c2 = cmd->m_Data[dim + 2];
				const float c3 = cmd->m_Data[dim + 4];

				const float a = 3.0f * (-c0 + 3.0f * (c1 - c2) + c3);
				const float b = 6.0f * (c0 - 2.0f * c1 + c2);
				const float c = 3.0f * (c1 - c0);

				float root[2] = { -1.0f, -1.0f }; // Max 2 roots
				uint32_t numRoots = solveQuad(a, b, c, &root[0]);

				for (uint32_t iRoot = 0; iRoot < numRoots; ++iRoot) {
					const float t = root[iRoot];
					if (t > 1e-5f && t < (1.0f - 1e-5f)) {
						float pos[2];
						evalCubicBezierAt(t, &last[0], &cmd->m_Data[0], &cmd->m_Data[2], &cmd->m_Data[4], &pos[0]);

						bounds[0] = bx::min<float>(bounds[0], pos[0]);
						bounds[1] = bx::min<float>(bounds[1], pos[1]);
						bounds[2] = bx::max<float>(bounds[2], pos[0]);
						bounds[3] = bx::max<float>(bounds[3], pos[1]);
					}
				}
			}

			last[0] = cmd->m_Data[4];
			last[1] = cmd->m_Data[5];
		}
		break;
		case PathCmdType::QuadraticTo:
			// Bezier end point
			bounds[0] = bx::min<float>(bounds[0], cmd->m_Data[2]);
			bounds[1] = bx::min<float>(bounds[1], cmd->m_Data[3]);
			bounds[2] = bx::max<float>(bounds[2], cmd->m_Data[2]);
			bounds[3] = bx::max<float>(bounds[3], cmd->m_Data[3]);

			// Extremities
			for (uint32_t dim = 0; dim < 2; ++dim) {
				const float c0 = last[dim];
				const float c1 = cmd->m_Data[dim + 0];
				const float c2 = cmd->m_Data[dim + 2];

				// dBezier(2,t)/dt = 2 * (a * t + b)
				const float a = (c2 - c1);
				const float b = (c1 - c0);

				if (bx::abs(a) > 1e-5f) {
					const float t = -b / a;

					if (t > 1e-5f && t < (1.0f - 1e-5f)) {
						float pos[2];
						evalQuadraticBezierAt(t, &last[0], &cmd->m_Data[0], &cmd->m_Data[2], &pos[0]);

						bounds[0] = bx::min<float>(bounds[0], pos[0]);
						bounds[1] = bx::min<float>(bounds[1], pos[1]);
						bounds[2] = bx::max<float>(bounds[2], pos[0]);
						bounds[3] = bx::max<float>(bounds[3], pos[1]);
					}
				}
			}

			last[0] = cmd->m_Data[2];
			last[1] = cmd->m_Data[3];
			break;
		case PathCmdType::ArcTo:
			// TODO: Find the true bounds of the arc.

			// End point
			bounds[0] = bx::min<float>(bounds[0], cmd->m_Data[5]);
			bounds[1] = bx::min<float>(bounds[1], cmd->m_Data[6]);
			bounds[2] = bx::max<float>(bounds[2], cmd->m_Data[5]);
			bounds[3] = bx::max<float>(bounds[3], cmd->m_Data[6]);

			last[0] = cmd->m_Data[5];
			last[1] = cmd->m_Data[6];
			break;
		case PathCmdType::ClosePath:
			// Noop
			break;
		default:
			SSVG_CHECK(false, "Unknown path command");
			break;
		}
	}
}

float* pointListAllocPoints(PointList* ptList, uint32_t n)
{
	SSVG_CHECK(n != 0, "Requested invalid number of points");

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

void pointListCalcBounds(const PointList* ptList, float* bounds)
{
	const uint32_t numPoints = ptList->m_NumPoints;
	if (!numPoints) {
		// No points -> invalid bounding rect
		bounds[0] = bounds[1] = FLT_MAX;
		bounds[2] = bounds[3] = -FLT_MAX;
		return;
	}

	bounds[0] = bounds[2] = ptList->m_Coords[0];
	bounds[1] = bounds[3] = ptList->m_Coords[1];

	for (uint32_t i = 1; i < numPoints; ++i) {
		const float x = ptList->m_Coords[i * 2 + 0];
		const float y = ptList->m_Coords[i * 2 + 1];

		bounds[0] = bx::min<float>(bounds[0], x);
		bounds[1] = bx::min<float>(bounds[1], y);
		bounds[2] = bx::max<float>(bounds[2], x);
		bounds[3] = bx::max<float>(bounds[3], y);
	}
}

void shapeAttrsSetID(ShapeAttributes* attrs, const bx::StringView& value)
{
	uint32_t maxLen = bx::min<uint32_t>(SSVG_CONFIG_ID_MAX_LEN - 1, value.getLength());
	SSVG_WARN((int32_t)maxLen >= value.getLength(), "id \"%.*s\" truncated to %d characters", value.getLength(), value.getPtr(), maxLen);
	bx::memCopy(&attrs->m_ID[0], value.getPtr(), maxLen);
	attrs->m_ID[maxLen] = '\0';
}

void shapeAttrsSetFontFamily(ShapeAttributes* attrs, const bx::StringView& value)
{
	uint32_t maxLen = bx::min<uint32_t>(SSVG_CONFIG_FONT_FAMILY_MAX_LEN - 1, value.getLength());
	SSVG_WARN((int32_t)maxLen >= value.getLength(), "font-family \"%.*s\" truncated to %d characters", value.getLength(), value.getPtr(), maxLen);
	bx::memCopy(&attrs->m_FontFamily[0], value.getPtr(), maxLen);
	attrs->m_FontFamily[maxLen] = '\0';
}

void shapeAttrsSetClass(ShapeAttributes* attrs, const bx::StringView& value)
{
#if SSVG_CONFIG_CLASS_MAX_LEN
	uint32_t maxLen = bx::min<uint32_t>(SSVG_CONFIG_CLASS_MAX_LEN - 1, value.getLength());
	SSVG_WARN((int32_t)maxLen >= value.getLength(), "class \"%.*s\" truncated to %d characters", value.getLength(), value.getPtr(), maxLen);
	bx::memCopy(&attrs->m_Class[0], value.getPtr(), maxLen);
	attrs->m_Class[maxLen] = '\0';
#else
	BX_UNUSED(attrs, value);
#endif
}

Image* imageCreate(const ShapeAttributes* baseAttrs)
{
	Image* img = (Image*)BX_ALLOC(s_Allocator, sizeof(Image));
	bx::memSet(img, 0, sizeof(Image));
	bx::memCopy(&img->m_BaseAttrs, baseAttrs, sizeof(ShapeAttributes));

	return img;
}

void imageDestroy(Image* img)
{
	shapeListFree(&img->m_ShapeList);
	BX_FREE(s_Allocator, img);
}

void initLib(bx::AllocatorI* allocator)
{
	s_Allocator = allocator;
	s_ShapeAttrFreeListHead = nullptr;
}

void shutdownLib()
{
	ShapeAttributeFreeListNode* node = s_ShapeAttrFreeListHead;
	while (node) {
		ShapeAttributeFreeListNode* next = node->m_Next;

		BX_FREE(s_Allocator, node->m_Attrs);
		BX_FREE(s_Allocator, node);

		node = next;
	}
	s_ShapeAttrFreeListHead = nullptr;
}

bool shapeCopy(Shape* dst, const Shape* src, bool copyAttrs)
{
	const ShapeType::Enum type = src->m_Type;

	dst->m_Type = type;
	bx::memCopy(&dst->m_BoundingRect[0], &src->m_BoundingRect[0], sizeof(float) * 4);
	if (copyAttrs) {
		bx::memCopy(dst->m_Attrs, src->m_Attrs, sizeof(ShapeAttributes));
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
	default:
		SSVG_CHECK(false, "Unknown shape type");
		return false;
	}

	return true;
}

void shapeFree(Shape* shape)
{
	switch (shape->m_Type) {
	case ShapeType::Group:
		shapeListFree(&shape->m_ShapeList);
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
		shape->m_Text.m_String = nullptr;
		break;
	default:
		break;
	}

	shapeAttrsFree(shape->m_Attrs);
}

void shapeUpdateBounds(Shape* shape)
{
	float bounds[4] = { FLT_MAX, FLT_MAX, -FLT_MAX, -FLT_MAX };

	const ShapeType::Enum type = shape->m_Type;
	switch (type) {
	case ShapeType::Group:
		shapeListCalcBounds(&shape->m_ShapeList, &bounds[0]);
		break;
	case ShapeType::Rect:
		bounds[0] = shape->m_Rect.x;
		bounds[1] = shape->m_Rect.y;
		bounds[2] = shape->m_Rect.x + shape->m_Rect.width;
		bounds[3] = shape->m_Rect.y + shape->m_Rect.height;
		break;
	case ShapeType::Circle:
		bounds[0] = shape->m_Circle.cx - shape->m_Circle.r;
		bounds[1] = shape->m_Circle.cy - shape->m_Circle.r;
		bounds[2] = shape->m_Circle.cx + shape->m_Circle.r;
		bounds[3] = shape->m_Circle.cy + shape->m_Circle.r;
		break;
	case ShapeType::Ellipse:
		bounds[0] = shape->m_Ellipse.cx - shape->m_Ellipse.rx;
		bounds[1] = shape->m_Ellipse.cy - shape->m_Ellipse.ry;
		bounds[2] = shape->m_Ellipse.cx + shape->m_Ellipse.rx;
		bounds[3] = shape->m_Ellipse.cy + shape->m_Ellipse.ry;
		break;
	case ShapeType::Line:
		bounds[0] = bx::min<float>(shape->m_Line.x1, shape->m_Line.x2);
		bounds[1] = bx::min<float>(shape->m_Line.y1, shape->m_Line.y2);
		bounds[2] = bx::max<float>(shape->m_Line.x1, shape->m_Line.x2);
		bounds[3] = bx::max<float>(shape->m_Line.y1, shape->m_Line.y2);
		break;
	case ShapeType::Polyline:
	case ShapeType::Polygon:
		pointListCalcBounds(&shape->m_PointList, &bounds[0]);
		break;
	case ShapeType::Path:
		pathCalcBounds(&shape->m_Path, &bounds[0]);
		break;
	case ShapeType::Text:
		// TODO: This is complicated!
		bounds[0] = bounds[1] = bounds[2] = bounds[3] = 0.0f;
		break;
	default:
		break;
	}

	bx::memCopy(&shape->m_BoundingRect[0], &bounds[0], sizeof(float) * 4);
}

static ShapeAttributes* shapeAttrsAllocFromNode(ShapeAttributeFreeListNode* node)
{
	SSVG_CHECK(node->m_FirstFreeID != UINT32_MAX, "No free slot in free list node. This function shouldn't have been called");

	ShapeAttributes* attrs = &node->m_Attrs[node->m_FirstFreeID];
	const uint32_t nextFreeID = *(uint32_t*)attrs;

	node->m_FirstFreeID = nextFreeID;
	node->m_NumFree--;

	return attrs;
}

static ShapeAttributes* shapeAttrsAlloc()
{
	static const uint32_t kNumShapeAttributesPerBatch = 1024;

	ShapeAttributeFreeListNode* node = s_ShapeAttrFreeListHead;
	while (node) {
		if (node->m_FirstFreeID != UINT32_MAX) {
			return shapeAttrsAllocFromNode(node);
		}

		node = node->m_Next;
	}

	node = (ShapeAttributeFreeListNode*)BX_ALLOC(s_Allocator, sizeof(ShapeAttributeFreeListNode));
	SSVG_CHECK(node != nullptr, "Failed to allocate shape attributes");

	node->m_Attrs = (ShapeAttributes*)BX_ALLOC(s_Allocator, sizeof(ShapeAttributes) * kNumShapeAttributesPerBatch);
	node->m_NumAttrs = kNumShapeAttributesPerBatch;
	node->m_Next = s_ShapeAttrFreeListHead;
	node->m_Prev = nullptr;
	node->m_NumFree = kNumShapeAttributesPerBatch;

	node->m_FirstFreeID = 0;
	for (uint32_t i = 0; i < kNumShapeAttributesPerBatch - 1; ++i) {
		*(uint32_t*)&node->m_Attrs[i] = i + 1;
	}
	*(uint32_t*)&node->m_Attrs[kNumShapeAttributesPerBatch - 1] = UINT32_MAX;

	if (s_ShapeAttrFreeListHead != nullptr) {
		s_ShapeAttrFreeListHead->m_Prev = node;
	}
	s_ShapeAttrFreeListHead = node;
		
	return shapeAttrsAllocFromNode(node);
}

static void shapeAttrsFree(ShapeAttributes* attrs)
{
	// Find the free list node attrs belongs to
	ShapeAttributeFreeListNode* node = s_ShapeAttrFreeListHead;
	while (node) {
		if (attrs >= node->m_Attrs && attrs < node->m_Attrs + node->m_NumAttrs) {
			break;
		}

		node = node->m_Next;
	}

	SSVG_CHECK(node != nullptr, "Shape attributes not allocated via the free list (double deallocation?)");

	const uint32_t id = (uint32_t)(attrs - node->m_Attrs);
	SSVG_CHECK(id < node->m_NumAttrs, "Index out of bounds");

	*(uint32_t*)attrs = node->m_FirstFreeID;
	node->m_FirstFreeID = id;

	node->m_NumFree++;
	if (node->m_NumFree == node->m_NumAttrs) {
		BX_FREE(s_Allocator, node->m_Attrs);

		ShapeAttributeFreeListNode* prev = node->m_Prev;
		ShapeAttributeFreeListNode* next = node->m_Next;
		if (prev) {
			prev->m_Next = next;
		}
		if (next) {
			next->m_Prev = prev;
		}

		if (s_ShapeAttrFreeListHead == node) {
			s_ShapeAttrFreeListHead = next;
		}

		BX_FREE(s_Allocator, node);
	}
}
} // namespace svg
