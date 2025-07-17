#include "inc.h"

#define WIN32_LEAN_AND_MEAN
#define NOCOMM
#define NOSERVICE
#define NOATOM
#define NOMINMAX

#include "Windows.h"

internal_fn u32 windows_get_cache_line_size()
{
    DWORD buffer_size = 0;
    GetLogicalProcessorInformationEx(RelationCache, NULL, &buffer_size);

    if (buffer_size == 0) return 0;

    BYTE* buffer = (BYTE*)memory_allocate(buffer_size, 0);
    DEFER(memory_free(buffer));

    if (!GetLogicalProcessorInformationEx(RelationCache, (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)buffer, &buffer_size)) {
        return 0;
    }

    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX info = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)buffer;
    while (buffer_size > 0)
    {
        if (info->Relationship == RelationCache && info->Cache.Level == 1 && (info->Cache.Type == CacheUnified || info->Cache.Type == CacheData)) {
            return info->Cache.LineSize;
        }

        buffer_size -= info->Size;
        info = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)((BYTE*)info + info->Size);
    }

    return 0;
}

void os_initialize()
{
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    SYSTEM_INFO system_info;
    GetSystemInfo(&system_info);
    app.os.page_size = system_info.dwAllocationGranularity;
    app.os.cache_line_size = windows_get_cache_line_size();

    app.temp_arena = arena_alloc();
}

void os_shutdown()
{
    arena_free(app.temp_arena);
}

Arena* arena_alloc()
{
	Arena* arena = (Arena*)memory_allocate(sizeof(Arena));

	u64 reserve_size = GB(8);
    arena->reserved_pages = (u32)u64_divide_high(reserve_size, app.os.page_size);
	arena->committed_pages = 0;
	arena->data = (u8*)VirtualAlloc(NULL, (u64)arena->reserved_pages * (u64)app.os.page_size, MEM_RESERVE, PAGE_NOACCESS);
    arena->size = 0;
	return arena;
}

void arena_free(Arena* arena) {
	VirtualFree(arena->data, 0, MEM_RELEASE);
	memory_free(arena);
}

void* arena_push(Arena* arena, u64 size) {
    return arena_push_align(arena, size, app.os.cache_line_size);
}

void* arena_push_align(Arena* arena, u64 user_size, u64 alignment)
{
    u64 page_size = app.os.page_size;

    u64 align_offset = (arena->size % alignment == 0) ? 0 : (alignment - (arena->size % alignment));
    u64 aligned_size = user_size + align_offset;

    u64 new_size = arena->size + aligned_size;
    u32 commited_pages_needed = u64_divide_high(new_size, app.os.page_size);

    if (commited_pages_needed > arena->committed_pages)
    {
        u32 commit_pages = MAX(commited_pages_needed - arena->committed_pages, 4);

        u8* commit_ptr = arena->data + arena->committed_pages * page_size;
        VirtualAlloc(commit_ptr, commit_pages * page_size, MEM_COMMIT, PAGE_READWRITE);

        arena->committed_pages = commited_pages_needed;
    }

    u8* ptr = arena->data + arena->size + align_offset;
    arena->size += aligned_size;

    assert((u64)ptr % alignment == 0);

    return ptr;
}

void arena_pop_to(Arena* arena, u64 size)
{
    if (arena->size <= size) {
        assert(0);
        return;
    }

    u64 free_bytes_size = arena->size - size;

    memory_zero(arena->data + size, free_bytes_size);
    arena->size = size;
}

b32 os_remove_folder(String path)
{
    String path0 = string_copy(app.temp_arena, path);
    return (b8)RemoveDirectoryA(path0.data);
}

b32 os_create_folder(String path)
{
    String path0 = string_copy(app.temp_arena, path);

    b32 result = (b32)CreateDirectoryA(path0.data, NULL);

    if (!result) {
        b32 already_exists = ERROR_ALREADY_EXISTS == GetLastError();
        if (already_exists) result = true;
    }

    return result;
}