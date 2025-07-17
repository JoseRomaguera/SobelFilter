#include "inc.h"

String string_copy_from_data(void* memory, u32 memory_size, String src)
{
    u64 size = MIN(memory_size, src.size);
    memory_copy(memory, src.data, size);
    if (memory_size > size) {
        u8* c = (u8*)memory;
        c[size] = '\0';
    }

    String res;
    res.data = (char*)memory;
    res.size = size;
    return res;
}

String string_copy(Arena* arena, String src)
{
	u32 data_size = src.size + 1;
	void* data = arena_push(arena, data_size);
	return string_copy_from_data(data, data_size, src);
}

String string_format(Arena* arena, String text, ...)
{
	String text0 = string_copy(app.temp_arena, text);

	va_list args;
	va_start(args, text);
	u32 length = vsnprintf(NULL, 0, text0.data, args) + 1;
	char* buffer = (char*)arena_push(arena, length);
	vsnprintf(buffer, length, text0.data, args);
	va_end(args);

	return buffer;
}