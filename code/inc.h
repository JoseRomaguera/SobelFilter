#pragma once

// Used for stb_image usage of "sprintf"
#define _CRT_SECURE_NO_WARNINGS

#include <memory.h>
#include <cassert>
#include <cstdlib>
#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t  i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

typedef u16    f16;
typedef float  f32;
typedef double f64;

typedef u8  b8;
typedef u16 b16;
typedef u32 b32;
typedef u64 b64;

typedef u32 wchar;

static_assert(sizeof(u8) == 1);
static_assert(sizeof(u16) == 2);
static_assert(sizeof(u32) == 4);
static_assert(sizeof(u64) == 8);

static_assert(sizeof(i8) == 1);
static_assert(sizeof(i16) == 2);
static_assert(sizeof(i32) == 4);
static_assert(sizeof(i64) == 8);

static_assert(sizeof(f16) == 2);
static_assert(sizeof(f32) == 4);
static_assert(sizeof(f64) == 8);

static_assert(sizeof(size_t) == sizeof(void*));

#define ABS(x) (((x) < 0) ? (-(x)) : (x))
#define MIN(a, b) ((a < b) ? a : b)
#define MAX(a, b) ((a > b) ? a : b)

#define KB(bytes) (((u64)(bytes)) << 10)
#define MB(bytes) (((u64)(bytes)) << 20)
#define GB(bytes) (((u64)(bytes)) << 30)
#define TB(bytes) (((u64)(bytes)) << 40)

#define _JOIN(x, y) x##y
#define JOIN(x, y) _JOIN(x, y)

template <typename F>
struct _defer {
	F f;
	_defer(F f) : f(f) {}
	~_defer() { f(); }
};

template <typename F>
_defer<F> _defer_func(F f) {
	return _defer<F>(f);
}

#define _DEFER(x) JOIN(x, __COUNTER__)
#define DEFER(code) auto _DEFER(_defer_) = _defer_func([&](){code;})

#define inline_fn inline
#define internal_fn static

#define global_var extern

// UTILS

struct Arena;

template<typename T>
struct Array {
	T* data;
	u32 count;

	inline T& operator[](u32 index) {
		assert(index < count);
		return data[index];
	}

	inline const T& operator[](u32 index) const {
		assert(index < count);
		return data[index];
	}
};

template<typename T>
inline_fn Array<T> array_make(T* data, u32 count)
{
	Array<T> array;
	array.data = data;
	array.count = count;
	return array;
}

inline_fn f32 f32_clamp(f32 min, f32 max, f32 n) { return MAX(MIN(n, max), min); }
inline_fn f32 f32_clamp01(f32 n) { return f32_clamp(0.f, 1.f, n); }
inline_fn u64 u64_divide_high(u64 n, u64 div) {
	return (n / div) + (n % div != 0ULL);
}


inline_fn u32 cstring_size(const char* str) {
	u32 size = 0u;
	while (*str++) ++size;
	return size;
}

struct String {
	char* data;
	u64 size;

	inline char& operator[](u32 index) {
		assert(data != NULL);
		assert(index < size);
		return data[index];
	}

	String() = default;

	String(const char* cstr) {
		size = cstring_size(cstr);
		data = (char*)cstr;
	}
};

String string_copy_from_data(void* memory, u32 memory_size, String src);
String string_copy(Arena* arena, String src);
String string_format(Arena* arena, String text, ...);

// OS LAYER

#define memory_copy(dst, src, size) memcpy(dst, src, size)
#define memory_zero(dst, size) memset(dst, 0, size)

#define memory_allocate(size) malloc(size)
#define memory_reallocate(ptr, size) realloc(ptr, size)
#define memory_free(ptr) free(ptr)

void os_initialize();
void os_shutdown();

struct Arena {
	u8* data;
	u64 size;
	u32 reserved_pages;
	u32 committed_pages;
};

Arena* arena_alloc();
void  arena_free(Arena* arena);

void* arena_push(Arena* arena, u64 size);
void* arena_push_align(Arena* arena, u64 user_size, u64 alignment);
void arena_pop_to(Arena* arena, u64 size);

b32 os_remove_folder(String path);
b32 os_create_folder(String path);

// APP

enum ImageFormat {
	ImageFormat_Invalid,
	ImageFormat_I8,
	ImageFormat_RGB8,
	ImageFormat_RGBA8,
};

struct Image {
	void* _data;
	ImageFormat format;
	u32 width;
	u32 height;
};

enum BlurDistance {
	BlurDistance_3,
	BlurDistance_5,
};

#define IMAGE_INVALID (Image{})

struct AppGlobals {
	struct {
		b32 save_intermediates;
		u32 blur_iterations;
		BlurDistance blur_distance;
		f32 threshold;
	} sett;

	struct {
		u32 page_size;
		u32 cache_line_size;
	} os;

	u32 intermediate_image_saves_counter;
	String intermediate_path;
	Arena* temp_arena;
};

global_var AppGlobals app;

void app_save_intermediate(Image image, const char* name);

u32 image_format_get_pixel_stride(ImageFormat format);
u32 image_format_get_number_of_channels(ImageFormat format);
u32 image_calculate_size(Image image);

inline_fn b32 image_is_invalid(Image img) { return img.format == ImageFormat_Invalid; }

template<typename T>
inline_fn Array<T> image_get_data(Image img) { return array_make<T>((T*)img._data, image_calculate_size(img) / sizeof(T)); }

Image image_alloc(u32 width, u32 height, ImageFormat format);
void image_free(Image image);
Image image_copy(Image src, ImageFormat format);
void image_mult(Image dst, f32 mult);

Image image_apply_sobel_convolution(Image src);
Image image_apply_threshold(Image src, f32 threshold);
Image image_apply_gaussian_blur(Image src, BlurDistance distance);

Image image_blend(Image src0, Image src1, f32 factor);
Image image_apply_1pass_kernel3x3(Image src, Image kernel, u32 normalize_factor, b32 include_border);
Image image_apply_2pass_kernel5x5(Image src, Image kernel, u32 normalize_factor);

Image load_image(String path);
b32 save_image(String path, Image image);
