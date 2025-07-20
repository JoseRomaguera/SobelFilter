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
    app.os.logic_core_count = MAX(system_info.dwNumberOfProcessors, 1);

    app.os.simd_granularity = 32; // AVX-256
    app.os.pixels_per_thread = u32_divide_high(5000, 64) * 64;
    app.os.pixels_padding = app.os.simd_granularity;

    LARGE_INTEGER windows_clock_frequency;
    QueryPerformanceFrequency(&windows_clock_frequency);
    app.os.timer_frequency = windows_clock_frequency.QuadPart;

    LARGE_INTEGER windows_begin_time;
    QueryPerformanceCounter(&windows_begin_time);
    app.os.timer_start_counter = windows_begin_time.QuadPart;

    app.temp_arena = arena_alloc();
    app.static_arena = arena_alloc();
}

void os_shutdown()
{
    arena_free(app.temp_arena);
    arena_free(app.static_arena);
}

Arena* arena_alloc()
{
	Arena* arena = (Arena*)memory_allocate(sizeof(Arena));

	u64 reserve_size = GB(16); // Win64 has 256TB of virtual memory addresses!
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
    u32 commited_pages_needed = (u32)u64_divide_high(new_size, app.os.page_size);

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

void* os_allocate_image_memory(u32 pixels, u32 pixel_stride)
{
    // Extra memory to safely overflow the buffer using SIMD
    u32 pixels_extra = u32_divide_high(app.os.pixels_padding, pixel_stride);
    u64 size = (u64)(pixels + pixels_extra) * (u64)pixel_stride;
    
    return VirtualAlloc(NULL, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
}

void os_free_image_memory(void* ptr)
{
    VirtualFree(ptr, 0, MEM_RELEASE);
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

u64 os_get_time_counter()
{
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return now.QuadPart;
}

int windows_thread_main(void* thread_data)
{
    ThreadFn* fn = *(ThreadFn**)thread_data;
    void* user_data = (ThreadFn**)thread_data + 1;

    i32 ret = fn(user_data);

    memory_free(thread_data);

    return ret;
}

Thread os_thread_start(ThreadFn* fn, RawBuffer data)
{
    u32 size_needed = sizeof(ThreadFn*);
    size_needed += (u32)data.size;

    ThreadFn** thread_data = (ThreadFn**)memory_allocate(size_needed, true);
    *thread_data = fn;

    u8* user_data = (u8*)(thread_data + 1);
    memory_copy(user_data, data.data, data.size);

    HANDLE handle = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)windows_thread_main, thread_data, 0, NULL);

    if (handle == NULL) {
        memory_free(thread_data);
        return {};
    }

    Thread thread{};
    thread.value = (u64)handle;
    return thread;
}

void os_thread_wait(Thread thread)
{
    if (thread.value) WaitForSingleObject((HANDLE)thread.value, INFINITE);
    assert(thread.value && "The thread must be valid");
}

void os_thread_wait_array(Thread* threads, u32 count) {
    for (u32 i = 0; i < count; ++i) {
        os_thread_wait(threads[i]);
    }
}

void os_thread_yield() {
    SwitchToThread();
}

Semaphore os_semaphore_create(u32 initial_count, u32 max_count)
{
    HANDLE s = CreateSemaphoreExA(NULL, initial_count, max_count, NULL, 0, SEMAPHORE_ALL_ACCESS);
    Semaphore sem{};
    sem.value = (u64)s;
    return sem;
}

void os_semaphore_wait(Semaphore semaphore, u32 millis)
{
    assert(semaphore.value);
    if (semaphore.value == 0) return;

    HANDLE s = (HANDLE)semaphore.value;
    WaitForSingleObjectEx(s, millis, FALSE);
}

b32 os_semaphore_release(Semaphore semaphore, u32 count)
{
    assert(semaphore.value);
    if (semaphore.value == 0) return FALSE;

    HANDLE s = (HANDLE)semaphore.value;
    return ReleaseSemaphore(s, count, 0) ? 1 : 0;
}

void os_semaphore_destroy(Semaphore semaphore)
{
    if (semaphore.value == 0) return;

    HANDLE s = (HANDLE)semaphore.value;
    CloseHandle(s);
}

u32 interlock_increment_u32(volatile u32* n) {
    return InterlockedIncrement((volatile LONG*)n);
}
u32 interlock_decrement_u32(volatile u32* n) {
    return InterlockedDecrement((volatile LONG*)n);
}
u32 interlock_exchange_u32(volatile u32* dst, u32 compare, u32 exchange) {
    return InterlockedCompareExchange(dst, exchange, compare);
}
