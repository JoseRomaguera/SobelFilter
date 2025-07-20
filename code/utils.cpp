#include "inc.h"

u32 cstring_size(const char* str) {
    u32 size = 0u;
    while (*str++) ++size;
    return size;
}

String string_copy_from_data(void* memory, u64 memory_size, String src)
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
	u64 data_size = src.size + 1;
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

String string_format_time(f64 seconds)
{
    if (seconds >= 10.0) {
        return string_format(app.temp_arena, "%.2f sec", seconds);
    }

    f64 millis = seconds * 1000.0;

    if (millis >= 10.0) {
        return string_format(app.temp_arena, "%.2f ms", millis);
    }

    f64 micro = millis * 1000.0;

    if (micro >= 10.0) {
        return string_format(app.temp_arena, "%.2f us", micro);
    }

    f64 nano = micro * 1000.0;
    return string_format(app.temp_arena, "%.2f ns", nano);
}

f64 timer_now()
{
    u64 freq = app.os.timer_frequency;
    u64 start = app.os.timer_start_counter;

    u64 ellapsed = os_get_time_counter() - start;
    return (f64)ellapsed / (f64)freq;
}