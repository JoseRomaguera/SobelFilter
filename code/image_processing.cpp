#include "inc.h"

u32 image_format_get_pixel_stride(ImageFormat format)
{
	// Assume that all the channels are 1 byte long
	return image_format_get_number_of_channels(format);
}

u32 image_format_get_number_of_channels(ImageFormat format)
{
	if (format == ImageFormat_I8) return 1;
	if (format == ImageFormat_RGB8) return 3;
	if (format == ImageFormat_RGBA8) return 4;
	assert(0);
	return 1;
}

u32 image_calculate_size(Image image) {
	return image.width * image.height * image_format_get_pixel_stride(image.format);
}

Image image_alloc(u32 width, u32 height, ImageFormat format)
{
	u32 size = width * height * image_format_get_pixel_stride(format);

	Image img = {};
	img.width = width;
	img.height = height;
	img._data = (u8*)memory_allocate(size);
	img.format = format;

	return img;
}

void image_free(Image image)
{
	if (image_is_invalid(image)) return;
	memory_free(image._data);
}

// Common image operations with same dimensions
struct ImageOp_Task {
	Image dst, src0, src1;
	u32 width;
	u32 height;
	u32 write_count;
	u32 mode; // 0 -> copy; 1 -> mult; 2 -> blend; 3 -> threshold
	union {
		f32 mult;
		f32 blend_factor;
		f32 threshold;
	};
};

internal_fn void image_op_task(u32 index, void* _data)
{
	ImageOp_Task* data = (ImageOp_Task*)_data;

	u32 total_pixel_count = data->width * data->height;
	u32 pixel_offset = index * data->write_count;
	u32 end_pixel = MIN(pixel_offset + data->write_count, total_pixel_count);

	// Image Copy
	if (data->mode == 0)
	{
		Image src = data->src0;
		Image dst = data->dst;

		u32 src_pixel_stride = image_format_get_pixel_stride(src.format);
		u32 dst_pixel_stride = image_format_get_pixel_stride(dst.format);

		if (dst.format == src.format)
		{
			u8* dst_ptr = (u8*)dst._data + pixel_offset * dst_pixel_stride;
			u8* src_ptr = (u8*)src._data + pixel_offset * src_pixel_stride;
			u32 copy_size = (end_pixel - pixel_offset) * src_pixel_stride;

			memory_copy(dst_ptr, src_ptr, copy_size);
			return;
		}

		if (dst.format == ImageFormat_I8 && src.format == ImageFormat_RGBA8)
		{
			Array<u8> s = image_get_data<u8>(src);
			Array<u8> d = image_get_data<u8>(dst);

			for (u32 i = pixel_offset; i < end_pixel; ++i)
			{
				u32 src_offset = i * src_pixel_stride;
				u32 dst_offset = i * dst_pixel_stride;

				f32 r = s[src_offset + 0] * (1.f / 255.f) * 0.299f;
				f32 g = s[src_offset + 1] * (1.f / 255.f) * 0.587f;
				f32 b = s[src_offset + 2] * (1.f / 255.f) * 0.114f;
				f32 a = s[src_offset + 3] * (1.f / 255.f);

				f32 v = f32_clamp01((r + g + b) * a);

				d[dst_offset] = (u8)(v * 255.f);
			}

			return;
		}

		assert(0);
		printf("Invalid image copy formats\n");
		return;
	}
	// Image Mult
	else if (data->mode == 1)
	{
		Image dst = data->dst;

		if (dst.format != ImageFormat_I8) {
			assert(0);
			return;
		}

		Array<u8> d = image_get_data<u8>(dst);
		for (u32 i = pixel_offset; i < end_pixel; ++i) {
			d[i] = (u8)f32_clamp(0.f, 255.f, d[i] * data->mult);
		}

		return;
	}
	// Image Blend
	else if (data->mode == 2)
	{
		Image src0 = data->src0;
		Image src1 = data->src1;
		Image dst = data->dst;

		if (src0.format == ImageFormat_I8 && src1.format == ImageFormat_I8)
		{
			Array<u8> s0 = image_get_data<u8>(src0);
			Array<u8> s1 = image_get_data<u8>(src1);
			Array<u8> d = image_get_data<u8>(dst);

			for (u32 i = pixel_offset; i < end_pixel; ++i) {
				f32 v0 = s0[i] * (1.f / 255.f);
				f32 v1 = s1[i] * (1.f / 255.f);

				f32 v = (v0 * (1.f - data->blend_factor)) + (v1 * data->blend_factor);
				d[i] = (u8)(v * 255.f);
			}

			return;
		}

		assert(0);
		return;
	}
	// Image Threshold
	else if (data->mode == 3)
	{
		Array<u8> s = image_get_data<u8>(data->src0);
		Array<u8> d = image_get_data<u8>(data->dst);

		u8 threshold_u8 = (u8)(f32_clamp01(data->threshold) * 255.f);

		for (u32 i = pixel_offset; i < end_pixel; ++i) {
			u8 value = s[i];
			d[i] = (value > threshold_u8) * 255;
		}
	}
}

Image image_copy(Image src, ImageFormat format)
{
	PROFILE_SCOPE("Image Copy");

	if (image_is_invalid(src)) return IMAGE_INVALID;

	Image dst = image_alloc(src.width, src.height, format);

	u32 pixel_count = src.width * src.height;
	u32 task_count = u64_divide_high(pixel_count, app.os.pixels_per_thread);

	TaskContext ctx = {};

	ImageOp_Task data = {};
	data.mode = 0;
	data.width = src.width;
	data.height = src.height;
	data.dst = dst;
	data.src0 = src;
	data.src1 = IMAGE_INVALID;
	data.write_count = app.os.pixels_per_thread;

	task_dispatch(image_op_task, { &data, sizeof(data) }, task_count, &ctx);
	task_wait(&ctx);

	return dst;
}

void image_mult(Image dst, f32 mult)
{
	PROFILE_SCOPE("Image Mult");

	u32 pixel_count = dst.width * dst.height;
	u32 task_count = u64_divide_high(pixel_count, app.os.pixels_per_thread);

	TaskContext ctx = {};

	ImageOp_Task data = {};
	data.mode = 1;
	data.mult = mult;
	data.width = dst.width;
	data.height = dst.height;
	data.dst = dst;
	data.src0 = IMAGE_INVALID;
	data.src1 = IMAGE_INVALID;
	data.write_count = app.os.pixels_per_thread;

	task_dispatch(image_op_task, { &data, sizeof(data) }, task_count, &ctx);
	task_wait(&ctx);
}

Image image_apply_sobel_convolution(Image src)
{
	PROFILE_SCOPE("Sobel Convolution");

	if (src.format != ImageFormat_I8) {
		return IMAGE_INVALID;
	}

	const u32 normalize_factor = 1;

	Image kernel = image_alloc(3, 3, ImageFormat_I8);

	Array<i8> k = image_get_data<i8>(kernel);
	k[0 + 0 * kernel.width] = -1;
	k[1 + 0 * kernel.width] = 0;
	k[2 + 0 * kernel.width] = 1;

	k[0 + 1 * kernel.width] = -2;
	k[1 + 1 * kernel.width] = 0;
	k[2 + 1 * kernel.width] = +2;

	k[0 + 2 * kernel.width] = -1;
	k[1 + 2 * kernel.width] = 0;
	k[2 + 2 * kernel.width] = 1;

	Image x_axis = image_apply_1pass_kernel3x3(src, kernel, normalize_factor, false);
	app_save_intermediate(x_axis, "x_axis_sobel");
	DEFER(image_free(x_axis));

	k = image_get_data<i8>(kernel);
	k[0 + 0 * kernel.width] = -1;
	k[1 + 0 * kernel.width] = -2;
	k[2 + 0 * kernel.width] = -1;

	k[0 + 1 * kernel.width] = 0;
	k[1 + 1 * kernel.width] = 0;
	k[2 + 1 * kernel.width] = 0;

	k[0 + 2 * kernel.width] = 1;
	k[1 + 2 * kernel.width] = 2;
	k[2 + 2 * kernel.width] = 1;

	Image y_axis = image_apply_1pass_kernel3x3(src, kernel, normalize_factor, false);
	app_save_intermediate(y_axis, "y_axis_sobel");
	DEFER(image_free(y_axis));

	Image result = image_blend(x_axis, y_axis, 0.5f);
	app_save_intermediate(result, "raw_sobel_blend");
	image_mult(result, 1.41f);
	return result;
}

Image image_apply_threshold(Image src, f32 threshold)
{
	PROFILE_SCOPE("Threshold");

	if (src.format != ImageFormat_I8) {
		return IMAGE_INVALID;
	}

	Image dst = image_alloc(src.width, src.height, ImageFormat_I8);

	u32 pixel_count = dst.width * dst.height;
	u32 task_count = u64_divide_high(pixel_count, app.os.pixels_per_thread);

	TaskContext ctx = {};

	ImageOp_Task data = {};
	data.mode = 3;
	data.threshold = threshold;
	data.width = src.width;
	data.height = src.height;
	data.dst = dst;
	data.src0 = src;
	data.src1 = IMAGE_INVALID;
	data.write_count = app.os.pixels_per_thread;

	task_dispatch(image_op_task, { &data, sizeof(data) }, task_count, &ctx);
	task_wait(&ctx);

	return dst;
}

Image image_apply_gaussian_blur(Image src, BlurDistance distance)
{
	PROFILE_SCOPE("Gaussian Blur");

	if (src.format != ImageFormat_I8) {
		return IMAGE_INVALID;
	}

	if (distance == BlurDistance_3) {
		Image kernel = image_alloc(3, 3, ImageFormat_I8);

		Array<i8> k = image_get_data<i8>(kernel);
		k[0 + 0 * kernel.width] = 1;
		k[1 + 0 * kernel.width] = 2;
		k[2 + 0 * kernel.width] = 1;

		k[0 + 1 * kernel.width] = 2;
		k[1 + 1 * kernel.width] = 4;
		k[2 + 1 * kernel.width] = 2;

		k[0 + 2 * kernel.width] = 1;
		k[1 + 2 * kernel.width] = 2;
		k[2 + 2 * kernel.width] = 1;

		return image_apply_1pass_kernel3x3(src, kernel, 16, true);
	}

	if (distance == BlurDistance_5) {
		Image kernel = image_alloc(5, 1, ImageFormat_I8);

		Array<i8> k = image_get_data<i8>(kernel);
		k[0] = 1;
		k[1] = 4;
		k[2] = 6;
		k[3] = 4;
		k[4] = 1;

		return image_apply_2pass_kernel5x5(src, kernel, 16);
	}

	assert(0);
	return IMAGE_INVALID;
}

Image image_blend(Image src0, Image src1, f32 factor)
{
	PROFILE_SCOPE("Blend");

	if (src0.width != src1.width || src0.height != src1.height) {
		assert(0);
		return IMAGE_INVALID;
	}

	if (src0.format != src1.format) {
		assert(0);
		return IMAGE_INVALID;
	}

	Image dst = image_alloc(src0.width, src0.height, src0.format);

	u32 pixel_count = dst.width * dst.height;
	u32 task_count = u64_divide_high(pixel_count, app.os.pixels_per_thread);

	TaskContext ctx = {};

	ImageOp_Task data = {};
	data.mode = 2;
	data.blend_factor = factor;
	data.width = dst.width;
	data.height = dst.height;
	data.dst = dst;
	data.src0 = src0;
	data.src1 = src1;
	data.write_count = app.os.pixels_per_thread;

	task_dispatch(image_op_task, { &data, sizeof(data) }, task_count, &ctx);
	task_wait(&ctx);

	return dst;
}

struct Kernel3x3Indices {
	i32 lt;
	i32 ct;
	i32 rt;
	i32 lc;
	i32 cc;
	i32 rc;
	i32 lb;
	i32 cb;
	i32 rb;
};

inline_fn i32 sample_1pass_kernel3x3(Array<u8> s, i32 base, Kernel3x3Indices off, Kernel3x3Indices k, u32 normalize_factor)
{
	i32 lt = (i32)s[base + off.lt] * k.lt;
	i32 ct = (i32)s[base + off.ct] * k.ct;
	i32 rt = (i32)s[base + off.rt] * k.rt;
	i32 lc = (i32)s[base + off.lc] * k.lc;
	i32 cc = (i32)s[base + off.cc] * k.cc;
	i32 rc = (i32)s[base + off.rc] * k.rc;
	i32 lb = (i32)s[base + off.lb] * k.lb;
	i32 cb = (i32)s[base + off.cb] * k.cb;
	i32 rb = (i32)s[base + off.rb] * k.rb;

	i32 res = lt + ct + rt + lc + cc + rc + lb + cb + rb;
	res /= (i32)normalize_factor;
	return MIN(ABS(res), 255);
}

struct Kernel5Indices {
	i32 v[5];
};

inline_fn i32 sample_2pass_kernel5x5(Array<u8> s, i32 base, Kernel5Indices off, Kernel5Indices k, u32 normalize_factor)
{
	i32 vl1 = (i32)s[base + off.v[0]] * k.v[0];
	i32 vl0 = (i32)s[base + off.v[1]] * k.v[1];
	i32 vc = (i32)s[base + off.v[2]] * k.v[2];
	i32 vr0 = (i32)s[base + off.v[3]] * k.v[3];
	i32 vr1 = (i32)s[base + off.v[4]] * k.v[4];

	i32 res = vl1 + vl0 + vc + vr0 + vr1;
	res /= (i32)normalize_factor;
	return MIN(ABS(res), 255);
}

struct ImageApplyKernel_Task {
	Image dst, src, kernel;
	u32 write_count;
	u32 mode; // 0 -> 3x3; 1 -> h5; 2 -> v5
	u32 normalize_factor;
};

internal_fn void image_apply_kernel_task(u32 index, void* _data)
{
	ImageApplyKernel_Task* data = (ImageApplyKernel_Task*)_data;

	Image src = data->src;
	Image dst = data->dst;

	u32 border_size = (data->mode == 0) ? 1 : 2;

	u32 pixel_offset = index * data->write_count;
	pixel_offset += border_size + border_size * src.width;

	u32 total_pixel_count = (src.width - (border_size * 2)) * (src.height - (border_size * 2));
	u32 end_pixel = MIN(pixel_offset + data->write_count, total_pixel_count);

	Array<u8> s = image_get_data<u8>(src);
	Array<u8> d = image_get_data<u8>(dst);

	if (data->mode == 0)
	{
		Kernel3x3Indices k;
		{
			u32 w = data->kernel.width;
			Array<i8> buffer = image_get_data<i8>(data->kernel);
			k.lt = buffer[0 + 0 * w];
			k.ct = buffer[1 + 0 * w];
			k.rt = buffer[2 + 0 * w];
			k.lc = buffer[0 + 1 * w];
			k.cc = buffer[1 + 1 * w];
			k.rc = buffer[2 + 1 * w];
			k.lb = buffer[0 + 2 * w];
			k.cb = buffer[1 + 2 * w];
			k.rb = buffer[2 + 2 * w];
		}

		Kernel3x3Indices off;
		off.lt = -1 - 1 * src.width;
		off.ct = +0 - 1 * src.width;
		off.rt = +1 - 1 * src.width;
		off.lc = -1 + 0 * src.width;
		off.cc = +0 + 0 * src.width;
		off.rc = +1 + 0 * src.width;
		off.lb = -1 + 1 * src.width;
		off.cb = +0 + 1 * src.width;
		off.rb = +1 + 1 * src.width;

		for (u32 base = pixel_offset; base < end_pixel; ++base) {
			d[base] = sample_1pass_kernel3x3(s, base, off, k, data->normalize_factor);
		}
	}
	else if (data->mode == 1 || data->mode == 2)
	{
		Kernel5Indices k;
		{
			Array<i8> buffer = image_get_data<i8>(data->kernel);
			k.v[0] = buffer[0];
			k.v[1] = buffer[1];
			k.v[2] = buffer[2];
			k.v[3] = buffer[3];
			k.v[4] = buffer[4];
		}

		if (data->mode == 1) {
			Kernel5Indices def_off;
			def_off.v[0] = -2;
			def_off.v[1] = -1;
			def_off.v[2] = 0;
			def_off.v[3] = 1;
			def_off.v[4] = 2;

			for (u32 base = pixel_offset; base < end_pixel; ++base) {
				d[base] = sample_2pass_kernel5x5(s, base, def_off, k, data->normalize_factor);
			}
		}
		else if (data->mode == 2) {
			Kernel5Indices def_off;
			def_off.v[0] = 0 + -2 * src.width;
			def_off.v[1] = 0 + -1 * src.width;
			def_off.v[2] = 0 + 0 * src.width;
			def_off.v[3] = 0 + 1 * src.width;
			def_off.v[4] = 0 + 2 * src.width;

			for (u32 base = pixel_offset; base < end_pixel; ++base) {
				d[base] = sample_2pass_kernel5x5(s, base, def_off, k, data->normalize_factor);
			}
		}
	}
}

Image image_apply_1pass_kernel3x3(Image src, Image kernel, u32 normalize_factor, b32 include_border)
{
	PROFILE_SCOPE("1pass kernel3x3");

	if (src.format != ImageFormat_I8) {
		return IMAGE_INVALID;
	}

	if (kernel.format != ImageFormat_I8 || kernel.width != 3 || kernel.height != 3) {
		return IMAGE_INVALID;
	}

	Image dst;
	
	if (include_border) {
		dst = image_copy(src, src.format);
	}
	else {
		dst = image_alloc(src.width, src.height, src.format);
		memory_zero(dst._data, image_calculate_size(dst));
	}

	u32 pixel_count = (src.width - 2) * (src.height - 2);
	u32 task_count = u64_divide_high(pixel_count, app.os.pixels_per_thread);

	TaskContext ctx = {};

	ImageApplyKernel_Task data = {};
	data.mode = 0;
	data.dst = dst;
	data.src = src;
	data.kernel = kernel;
	data.write_count = app.os.pixels_per_thread;
	data.normalize_factor = normalize_factor;

	task_dispatch(image_apply_kernel_task, { &data, sizeof(data) }, task_count, &ctx);
	task_wait(&ctx);

	return dst;
}

Image image_apply_2pass_kernel5x5(Image src, Image kernel, u32 normalize_factor)
{
	PROFILE_SCOPE("2pass kernel5x5");

	if (src.format != ImageFormat_I8) {
		return IMAGE_INVALID;
	}

	if (kernel.format != ImageFormat_I8 || kernel.width != 5 || kernel.height != 1) {
		return IMAGE_INVALID;
	}

	Image inter = image_copy(src, src.format);
	DEFER(image_free(inter));

	Image dst = image_copy(src, src.format);

	u32 pixel_count = (src.width - 4) * (src.height - 4);
	u32 task_count = u64_divide_high(pixel_count, app.os.pixels_per_thread);

	ImageApplyKernel_Task data = {};
	data.kernel = kernel;
	data.write_count = app.os.pixels_per_thread;
	data.normalize_factor = normalize_factor;

	// Horizontal
	{
		data.dst = inter;
		data.src = src;
		data.mode = 1;
		TaskContext ctx = {};
		task_dispatch(image_apply_kernel_task, { &data, sizeof(data) }, task_count, &ctx);
		task_wait(&ctx);
	}

	app_save_intermediate(inter, "inter_blur");

	// Vertical
	{
		data.dst = dst;
		data.src = inter;
		data.mode = 2;
		TaskContext ctx = {};
		task_dispatch(image_apply_kernel_task, { &data, sizeof(data) }, task_count, &ctx);
		task_wait(&ctx);
	}

	return dst;
}

#define STBI_ASSERT(x) assert(x)
#define STBI_MALLOC(size) memory_allocate(size)
#define STBI_FREE(ptr) memory_free(ptr)
#define STBI_REALLOC_SIZED(old_ptr, old_size, new_size) memory_reallocate(old_ptr, new_size)
#define STBI_REALLOC(old_ptr, new_size) memory_reallocate(old_ptr, new_size)
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION

#include "external/stbi_lib.h"
#include "external/stb_image_write.h"

Image load_image(String path)
{
    PROFILE_SCOPE("Load Image");

    String path0 = string_copy(app.temp_arena, path);

    int w = 0, h = 0, c = 0;
    void* data = stbi_load(path0.data, &w, &h, &c, 4);

    if (data == NULL) return IMAGE_INVALID;

    Image image = {};
    image.format = ImageFormat_RGBA8;
    image._data = (u8*)data;
    image.width = w;
    image.height = h;

    return image;
}

b32 save_image(String path, Image image)
{
    PROFILE_SCOPE("Save Image");

    if (image_is_invalid(image)) return false;

    u32 number_of_channels = image_format_get_number_of_channels(image.format);
    u32 pixel_stride = image_format_get_pixel_stride(image.format);
    u32 row_stride_in_bytes = image.width * pixel_stride;

    String path0 = string_copy(app.temp_arena, path);

    if (!stbi_write_png(path0.data, image.width, image.height, number_of_channels, image._data, row_stride_in_bytes)) return false;
    return true;
}