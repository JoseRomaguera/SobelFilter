#include "inc.h"

AppGlobals app;

internal_fn void generate(const char* path, BlurDistance blur_distance, u32 blur_iterations, f32 threshold)
{
	app.sett.blur_distance = blur_distance;
	app.sett.blur_iterations = blur_iterations;
	app.sett.threshold = threshold;

	Image original = load_image(path);
	DEFER(image_free(original));

	if (image_is_invalid(original)) {
		printf("Can't load the image %s\n", path);
		return;
	}

	app_save_intermediate(original, "original");

	Image gray = image_copy(original, ImageFormat_I8);
	app_save_intermediate(gray, "gray");
	DEFER(image_free(gray));

	Image blur = gray;
	for (int it = 0; it < app.sett.blur_iterations; ++it) {
		Image new_blur = image_apply_gaussian_blur(blur, app.sett.blur_distance);
		app_save_intermediate(new_blur, "blur");
		if (it != 0) image_free(blur);
		blur = new_blur;
	}

	Image sobel = image_apply_sobel_convolution(blur);
	app_save_intermediate(sobel, "sobel");
	DEFER(image_free(sobel));

	Image result = image_apply_threshold(sobel, app.sett.threshold);
	app_save_intermediate(result, "result");

	//if (save_image("images/result.png", result)) printf("Saved image\n");
	//else printf("Can't save image\n");
}

void main()
{
	os_initialize();
	app.sett.save_intermediates = true;
	app.intermediate_path = "images/result/";

	os_remove_folder(app.intermediate_path);
	os_create_folder(app.intermediate_path);

	generate("images/samples/valencia.jpg", BlurDistance_5, 1, 0.2f);
	generate("images/samples/city.png", BlurDistance_5, 3, 0.3f);
	generate("images/samples/fruit_low_res.png", BlurDistance_3, 0, 0.4f);
	generate("images/samples/glimmer_chain_asset.png", BlurDistance_5, 1, 0.3f);
	generate("images/samples/taj.png", BlurDistance_5, 1, 0.4f);

	os_shutdown();
}

void app_save_intermediate(Image image, const char* name)
{
	if (!app.sett.save_intermediates) return;

	String path = string_format(app.temp_arena, "%s/%u_%s.png", app.intermediate_path.data, app.intermediate_image_saves_counter++, name);

	if (save_image(path, image)) printf("Saved intermediate: %s\n", name);
	else printf("Can't save intermediate image\n");
}

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

Image image_copy(Image src, ImageFormat format)
{
	if (image_is_invalid(src)) return IMAGE_INVALID;
	
	Image dst = image_alloc(src.width, src.height, format);

	if (dst.format == src.format) {
		memory_copy(dst._data, src._data, image_calculate_size(src));
		return dst;
	}

	if (dst.format == ImageFormat_I8 && src.format == ImageFormat_RGBA8)
	{
		u32 pixel_count = src.width * src.height;
		u32 src_pixel_stride = image_format_get_pixel_stride(src.format);
		u32 dst_pixel_stride = image_format_get_pixel_stride(dst.format);

		Array<u8> s = image_get_data<u8>(src);
		Array<u8> d = image_get_data<u8>(dst);

		for (u32 i = 0; i < pixel_count; ++i)
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

		return dst;
	}

	assert(0);
	printf("Invalid image copy formats\n");
	return IMAGE_INVALID;
}

void image_mult(Image dst, f32 mult)
{
	if (dst.format != ImageFormat_I8) {
		assert(0);
		return;
	}

	Array<u8> d = image_get_data<u8>(dst);
	for (u32 i = 0; i < d.count; ++i) {
		d[i] = (u8)f32_clamp(0.f, 255.f, d[i] * mult);
	}
}

Image image_apply_sobel_convolution(Image src)
{
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
	if (src.format != ImageFormat_I8) {
		return IMAGE_INVALID;
	}

	Image dst = image_alloc(src.width, src.height, ImageFormat_I8);

	Array<u8> s = image_get_data<u8>(src);
	Array<u8> d = image_get_data<u8>(dst);

	u8 threshold_u8 = (u8)(f32_clamp01(threshold) * 255.f);

	for (u32 y = 0; y < src.height; ++y) {
		for (u32 x = 0; x < src.width; ++x)
		{
			u32 off = x + y * src.width;
			u8 value = s[off];

			d[off] = (value > threshold_u8) * 255;
		}
	}

	return dst;
}

Image image_apply_gaussian_blur(Image src, BlurDistance distance)
{
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
	if (src0.width != src1.width || src0.height != src1.height) {
		assert(0);
		return IMAGE_INVALID;
	}

	if (src0.format != src1.format) {
		assert(0);
		return IMAGE_INVALID;
	}

	if (src0.format == ImageFormat_I8 && src1.format == ImageFormat_I8)
	{
		Image dst = image_alloc(src0.width, src0.height, ImageFormat_I8);

		Array<u8> s0 = image_get_data<u8>(src0);
		Array<u8> s1 = image_get_data<u8>(src1);
		Array<u8> d = image_get_data<u8>(dst);

		for (u32 y = 0; y < dst.height; ++y) {
			for (u32 x = 0; x < dst.width; ++x)
			{
				u32 off = x + y * dst.width;

				f32 v0 = s0[off] * (1.f / 255.f);
				f32 v1 = s1[off] * (1.f / 255.f);

				f32 v = (v0 * (1.f - factor)) + (v1 * factor);
				d[off] = (u8)(v * 255.f);
			}
		}

		return dst;
	}

	assert(0);
	return IMAGE_INVALID;
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
	return ABS(res);
}

Image image_apply_1pass_kernel3x3(Image src, Image kernel, u32 normalize_factor, b32 include_border)
{
	if (src.format != ImageFormat_I8) {
		return IMAGE_INVALID;
	}

	if (kernel.format != ImageFormat_I8 || kernel.width != 3 || kernel.height != 3) {
		return IMAGE_INVALID;
	}

	Image dst = image_alloc(src.width, src.height, src.format);
	if (!include_border) memory_zero(dst._data, image_calculate_size(dst));

	Array<u8> s = image_get_data<u8>(src);
	Array<u8> d = image_get_data<u8>(dst);

	Kernel3x3Indices k;
	{
		Array<i8> buffer = image_get_data<i8>(kernel);
		k.lt = buffer[0 + 0 * kernel.width];
		k.ct = buffer[1 + 0 * kernel.width];
		k.rt = buffer[2 + 0 * kernel.width];
		k.lc = buffer[0 + 1 * kernel.width];
		k.cc = buffer[1 + 1 * kernel.width];
		k.rc = buffer[2 + 1 * kernel.width];
		k.lb = buffer[0 + 2 * kernel.width];
		k.cb = buffer[1 + 2 * kernel.width];
		k.rb = buffer[2 + 2 * kernel.width];
	}

	Kernel3x3Indices def_off;
	def_off.lt = -1 - 1 * src.width;
	def_off.ct = +0 - 1 * src.width;
	def_off.rt = +1 - 1 * src.width;
	def_off.lc = -1 + 0 * src.width;
	def_off.cc = +0 + 0 * src.width;
	def_off.rc = +1 + 0 * src.width;
	def_off.lb = -1 + 1 * src.width;
	def_off.cb = +0 + 1 * src.width;
	def_off.rb = +1 + 1 * src.width;

	if (include_border)
	{
		// Top-Left
		{
			Kernel3x3Indices off = def_off;
			off.lt = 0 + 0 * src.width;
			off.ct = 0 + 0 * src.width;
			off.rt = 1 + 0 * src.width;
			off.lc = 0 + 0 * src.width;
			off.lb = 0 + 1 * src.width;

			u32 base = 0;
			d[base] = sample_1pass_kernel3x3(s, base, off, k, normalize_factor);
		}

		// Top
		for (u32 x = 1; x < src.width - 1; ++x)
		{
			Kernel3x3Indices off = def_off;
			off.lt = -1 + 0 * src.width;
			off.ct = 0 + 0 * src.width;
			off.rt = 1 + 0 * src.width;

			u32 base = x;
			d[base] = sample_1pass_kernel3x3(s, base, off, k, normalize_factor);
		}

		// Top-Right
		{
			Kernel3x3Indices off = def_off;
			off.lt = -1 + 0 * src.width;
			off.ct = 0 + 0 * src.width;
			off.rt = 0 + 0 * src.width;
			off.rc = 0 + 0 * src.width;
			off.rb = 0 + 1 * src.width;

			u32 base = (src.width - 1) + 0 * src.width;
			d[base] = sample_1pass_kernel3x3(s, base, off, k, normalize_factor);
		}
	}
	

	// Center
	for (u32 y = 1; y < src.height - 1; ++y) {
		for (u32 x = 1; x < src.width - 1; ++x)
		{
			u32 base = x + y * src.width;
			d[base] = sample_1pass_kernel3x3(s, base, def_off, k, normalize_factor);
		}
	}

	if (include_border)
	{
		// Left
		for (u32 y = 1; y < src.height - 1; ++y)
		{
			Kernel3x3Indices off = def_off;
			off.lt = 0 - 1 * src.width;
			off.lc = 0 + 0 * src.width;
			off.lb = 0 + 1 * src.width;

			u32 base = 0 + y * src.width;
			d[base] = sample_1pass_kernel3x3(s, base, off, k, normalize_factor);
		}

		// Right
		for (u32 y = 1; y < src.height - 1; ++y)
		{
			Kernel3x3Indices off = def_off;
			off.rt = 0 - 1 * src.width;
			off.rc = 0 + 0 * src.width;
			off.rb = 0 + 1 * src.width;

			u32 base = (src.width - 1) + y * src.width;
			d[base] = sample_1pass_kernel3x3(s, base, off, k, normalize_factor);
		}
		
		// Bottom-Left
		{
			Kernel3x3Indices off = def_off;
			off.lb = 0 + 0 * src.width;
			off.cb = 0 + 0 * src.width;
			off.rb = 1 + 0 * src.width;
			off.lc = 0 + 0 * src.width;
			off.lt = 0 - 1 * src.width;

			u32 base = 0 + (src.height - 1) * src.width;
			d[base] = sample_1pass_kernel3x3(s, base, off, k, normalize_factor);
		}

		// Bottom
		for (u32 x = 1; x < src.width - 1; ++x)
		{
			Kernel3x3Indices off = def_off;
			off.lb = -1 + 0 * src.width;
			off.cb = 0 + 0 * src.width;
			off.rb = 1 + 0 * src.width;

			u32 base = x + (src.height - 1) * src.width;
			d[base] = sample_1pass_kernel3x3(s, base, off, k, normalize_factor);
		}

		// Bottom-Right
		{
			Kernel3x3Indices off = def_off;
			off.lb = -1 + 0 * src.width;
			off.cb = 0 + 0 * src.width;
			off.rb = 0 + 0 * src.width;
			off.rc = 0 + 0 * src.width;
			off.rt = 0 - 1 * src.width;

			u32 base = (src.width - 1) + (src.height - 1) * src.width;
			d[base] = sample_1pass_kernel3x3(s, base, off, k, normalize_factor);
		}
	}

	return dst;
}

struct Kernel5Indices {
	i32 v[5];
};

inline_fn i32 sample_2pass_kernel5x5(Array<u8> s, i32 base, Kernel5Indices off, Kernel5Indices k, u32 normalize_factor)
{
	i32 vl1 = (i32)s[base + off.v[0]] * k.v[0];
	i32 vl0 = (i32)s[base + off.v[1]] * k.v[1];
	i32 vc  = (i32)s[base + off.v[2]] * k.v[2];
	i32 vr0 = (i32)s[base + off.v[3]] * k.v[3];
	i32 vr1 = (i32)s[base + off.v[4]] * k.v[4];

	i32 res = vl1 + vl0 + vc + vr0 + vr1;
	res /= (i32)normalize_factor;
	return ABS(res);
}

Image image_apply_2pass_kernel5x5(Image src, Image kernel, u32 normalize_factor)
{
	if (src.format != ImageFormat_I8) {
		return IMAGE_INVALID;
	}

	if (kernel.format != ImageFormat_I8 || kernel.width != 5 || kernel.height != 1) {
		return IMAGE_INVALID;
	}

	Image inter = image_copy(src, src.format);
	DEFER(image_free(inter));

	Image dst = image_copy(src, src.format);

	Kernel5Indices k;
	{
		Array<i8> buffer = image_get_data<i8>(kernel);
		k.v[0] = buffer[0];
		k.v[1] = buffer[1];
		k.v[2] = buffer[2];
		k.v[3] = buffer[3];
		k.v[4] = buffer[4];
	}

	// Horizontal
	{
		Array<u8> s = image_get_data<u8>(src);
		Array<u8> d = image_get_data<u8>(inter);

		Kernel5Indices def_off;
		def_off.v[0] = -2;
		def_off.v[1] = -1;
		def_off.v[2] = 0;
		def_off.v[3] = 1;
		def_off.v[4] = 2;

		for (u32 y = 0; y < src.height; ++y) {
			for (u32 x = 2; x < src.width - 2; ++x)
			{
				u32 base = x + y * src.width;
				d[base] = sample_2pass_kernel5x5(s, base, def_off, k, normalize_factor);
			}
		}
	}

	app_save_intermediate(inter, "inter_blur");

	// Vertical
	{
		Array<u8> s = image_get_data<u8>(inter);
		Array<u8> d = image_get_data<u8>(dst);

		Kernel5Indices def_off;
		def_off.v[0] = 0 + -2 * inter.width;
		def_off.v[1] = 0 + -1 * inter.width;
		def_off.v[2] = 0 + 0 * inter.width;
		def_off.v[3] = 0 + 1 * inter.width;
		def_off.v[4] = 0 + 2 * inter.width;

		for (u32 y = 2; y < src.height - 2; ++y) {
			for (u32 x = 0; x < src.width; ++x)
			{
				u32 base = x + y * src.width;
				d[base] = sample_2pass_kernel5x5(s, base, def_off, k, normalize_factor);
			}
		}
	}

	return dst;
}