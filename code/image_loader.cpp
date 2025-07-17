#include "inc.h"

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
    if (image_is_invalid(image)) return false;

    u32 number_of_channels = image_format_get_number_of_channels(image.format);
    u32 pixel_stride = image_format_get_pixel_stride(image.format);
    u32 row_stride_in_bytes = image.width * pixel_stride;

    String path0 = string_copy(app.temp_arena, path);

    if (!stbi_write_png(path0.data, image.width, image.height, number_of_channels, image._data, row_stride_in_bytes)) return false;
    return true;
}