// Static model allocations own their GPU buffers. Dynamic geometry stays streamed.
#ifdef __EMSCRIPTEN__
#define WEBGL_COMPAT_IMPLEMENTATION
#include "game.h"

#define STATIC_ARRAY_BUCKETS 256

typedef struct
{
	GLuint name;
	size_t bytes;
	bool valid;
} StaticArrayTarget;

typedef struct StaticArray
{
	const void *pointer;
	size_t bytes;
	StaticArrayTarget targets[2];
	struct StaticArray *next;
} StaticArray;

static StaticArray *staticArrays[STATIC_ARRAY_BUCKETS];

static StaticArray **FindStaticArray(const void *pointer)
{
	size_t bucket = ((uintptr_t)pointer >> 4) & (STATIC_ARRAY_BUCKETS - 1);
	StaticArray **link = &staticArrays[bucket];
	while (*link && (*link)->pointer != pointer)
		link = &(*link)->next;
	return link;
}

static bool DeleteStaticArrayBuffers(const StaticArray *array)
{
	bool deleted = false;
	for (int i = 0; i < 2; i++)
	{
		if (array->targets[i].name)
		{
			glDeleteBuffers(1, &array->targets[i].name);
			deleted = true;
		}
	}
	return deleted;
}

void WebGL_RegisterStaticArray(const void *pointer, size_t bytes)
{
	if (!bytes)
		return;
	GAME_ASSERT(pointer);
	StaticArray **link = FindStaticArray(pointer);
	GAME_ASSERT(!*link);
	StaticArray *array = calloc(1, sizeof(*array));
	GAME_ASSERT(array);
	array->pointer = pointer;
	array->bytes = bytes;
	*link = array;
}

void WebGL_UnregisterStaticArray(const void *pointer)
{
	StaticArray **link = FindStaticArray(pointer);
	StaticArray *array = *link;
	if (!array)
		return;
	*link = array->next;
	if (DeleteStaticArrayBuffers(array))
		WebGL_InvalidateBufferState();
	free(array);
}

void WebGL_InvalidateStaticArrays(void)
{
	for (int i = 0; i < STATIC_ARRAY_BUCKETS; i++)
		for (StaticArray *array = staticArrays[i]; array; array = array->next)
			array->targets[0].valid = array->targets[1].valid = false;
}

void WebGL_ClearStaticArrays(void)
{
	bool deleted = false;
	for (int i = 0; i < STATIC_ARRAY_BUCKETS; i++)
	{
		StaticArray *array = staticArrays[i];
		staticArrays[i] = NULL;
		while (array)
		{
			StaticArray *next = array->next;
			deleted = DeleteStaticArrayBuffers(array) || deleted;
			free(array);
			array = next;
		}
	}
	if (deleted)
		WebGL_InvalidateBufferState();
}

GLuint WebGL_GetStaticArrayBuffer(GLenum target, const void *pointer, size_t bytes)
{
	StaticArray *array = *FindStaticArray(pointer);
	if (!array)
		return 0;
	GAME_ASSERT(bytes <= array->bytes);
	GAME_ASSERT(target == GL_ARRAY_BUFFER || target == GL_ELEMENT_ARRAY_BUFFER);
	StaticArrayTarget *slot = &array->targets[target == GL_ELEMENT_ARRAY_BUFFER];
	if (!slot->valid || slot->bytes < bytes)
	{
		if (!slot->name)
			glGenBuffers(1, &slot->name);
		glBindBuffer(target, slot->name);
		glBufferData(target, bytes, pointer, GL_STATIC_DRAW);
		slot->bytes = bytes;
		slot->valid = true;
		WebGL_InvalidateBufferState();
	}
	return slot->name;
}

#endif
