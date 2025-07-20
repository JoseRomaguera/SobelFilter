#include "inc.h"

AppGlobals app;

internal_fn void generate(const char* path, BlurDistance blur_distance, u32 blur_iterations, f32 threshold)
{
	PROFILE_SCOPE("Generate");

	// Reset Temp Arena
	arena_pop_to(app.temp_arena, 0);

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
	for (u32 it = 0; it < app.sett.blur_iterations; ++it) {
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
}

int main()
{
	os_initialize();
	app.sett.save_intermediates = true;
	app.sett.enable_profiler = true;
	app.intermediate_path = "images/result/";

	PROFILE_BEGIN("Main");

	if (!task_initialize()) return -1;

	os_remove_folder(app.intermediate_path);
	os_create_folder(app.intermediate_path);

	generate("images/samples/valencia.jpg", BlurDistance_5, 1, 0.2f);
	generate("images/samples/city.png", BlurDistance_5, 3, 0.3f);
	generate("images/samples/fruit_low_res.png", BlurDistance_3, 0, 0.7f);
	generate("images/samples/glimmer_chain_asset.png", BlurDistance_5, 1, 0.3f);
	generate("images/samples/taj.png", BlurDistance_5, 1, 0.4f);

	task_shutdown();

	PROFILE_END();

	os_shutdown();
	return 0;
}

void app_save_intermediate(Image image, String name)
{
	if (!app.sett.save_intermediates) return;

	const char* cname = string_copy(app.temp_arena, name).data;

	String path = string_format(app.temp_arena, "%s/%u_%s.png", app.intermediate_path.data, app.intermediate_image_saves_counter++, cname);

	if (save_image(path, image)) printf("Saved intermediate: %s\n", cname);
	else printf("Can't save intermediate image\n");
}

