#include "inc.h"

#define TASK_QUEUE_SIZE 6000

struct TaskThreadData
{
	Thread thread;
	u32 id;
};

struct TaskData
{
	TaskContext* context;
	TaskFn* fn;
	u32 index;
	b8 user_data[TASK_DATA_SIZE];
};

struct TaskSystemState
{
	TaskData tasks[TASK_QUEUE_SIZE];
	volatile u32 task_count;
	volatile u32 task_completed;
	volatile u32 task_next;

	Semaphore semaphore;

	TaskThreadData* thread_data;
	u32 thread_count;
	volatile u32 thread_initialized_count;

	b32 running;
};

TaskSystemState* task_system;

internal_fn i32 task_thread(void* arg);

b32 task_initialize()
{
	task_system = (TaskSystemState*)arena_push(app.static_arena, sizeof(TaskSystemState));
	task_system->running = true;

	u32 thread_count = MAX(app.os.logic_core_count - 1, 1);

	task_system->thread_data = (TaskThreadData*)arena_push(app.static_arena, sizeof(TaskThreadData) * thread_count);

	task_system->semaphore = os_semaphore_create(0, thread_count);

	if (task_system->semaphore.value == 0) {
		printf("Can't create task system semaphore\n");
		return false;
	}

	for (i32 t = 0; t < thread_count; ++t)
	{
		TaskThreadData* thread_data = task_system->thread_data + t;
		thread_data->id = t;

		thread_data->thread = os_thread_start(task_thread, {});

		if (thread_data->thread.value == 0)
		{
			printf("Can't create task thread\n");
			task_system->running = false;
			return false;
		}

		// Config thread
		u64 affinity_mask = 0;
		os_thread_configure(thread_data->thread, affinity_mask, ThreadPrority_Normal);
	}

	task_system->thread_count = thread_count;

	while (task_system->thread_initialized_count < task_system->thread_count)
		os_thread_yield();

	return true;
}

void task_shutdown()
{
	if (task_system == NULL) return;

	task_join();

	task_system->running = false;

	Thread* threads = (Thread*)arena_push(app.temp_arena, sizeof(Thread) * task_system->thread_count);
	for (i32 i = 0; i < task_system->thread_count; ++i)
		threads[i] = task_system->thread_data[i].thread;

	os_semaphore_release(task_system->semaphore, task_system->thread_count);

	os_thread_wait_array(threads, task_system->thread_count);

	os_semaphore_destroy(task_system->semaphore);
	task_system = NULL;
}

internal_fn b32 _task_thread_do_work()
{
	b32 done = false;

	u32 task_next = task_system->task_next;

	if (task_next < task_system->task_count)
	{
		u32 task_index = interlock_exchange_u32(&task_system->task_next, task_next, task_next + 1);
		cpu_read_barrier();

		if (task_index == task_next)
		{
			TaskData task = task_system->tasks[task_index % TASK_QUEUE_SIZE];

			assert(task.fn != NULL);
			task.fn(task.index, task.user_data);

			interlock_increment_u32(&task_system->task_completed);
			if (task.context != NULL) interlock_increment_u32((volatile u32*)&task.context->completed);
			done = true;
		}
	}

	return done;
}

internal_fn i32 task_thread(void* _)
{
	interlock_increment_u32(&task_system->thread_initialized_count);

	u32 failed_count = 0;

	while (task_system->running)
	{
		if (!_task_thread_do_work()) {
			if (task_running(NULL)) os_thread_yield();
			else os_semaphore_wait(task_system->semaphore, 100);
		}
	}

	return 0;
}

internal_fn void _task_add_queue(TaskFn* fn, RawBuffer data, u32 index, TaskContext* ctx)
{
	TaskData* task = task_system->tasks + task_system->task_count % TASK_QUEUE_SIZE;
	task->fn = fn;
	task->context = ctx;
	task->index = index;
	memory_copy(task->user_data, data.data, MIN(data.size, TASK_DATA_SIZE));

	cpu_write_barrier();
	++task_system->task_count;
}

void task_dispatch(TaskFn* fn, RawBuffer data, u32 task_count, TaskContext* context)
{
	if (context) {
		context->dispatched += task_count;
	}

	assert(data.size <= TASK_DATA_SIZE && "The task data size is too large");
	assert(fn != NULL && "Null task function");

	for (i32 i = 0; i < task_count; ++i) {
		_task_add_queue(fn, data, i, context);
	}

	if (!os_semaphore_release(task_system->semaphore, MIN(task_count, task_system->thread_count)))
	{
		u32 release_count = 0;
		while (os_semaphore_release(task_system->semaphore, 1) && release_count < task_system->thread_count) { release_count++; }
	}
}

void task_join()
{
	task_wait(NULL);
}

void task_wait(TaskContext* context)
{
	while (task_running(context))
		_task_thread_do_work();
}

b32 task_running(TaskContext* context) {
	if (context) return context->completed < context->dispatched;
	return task_system->task_completed < task_system->task_count;
}