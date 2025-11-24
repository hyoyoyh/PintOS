#include "devices/timer.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <string.h>
#include "list.h"
#include "stdbool.h"
#include "threads/interrupt.h"
#include "threads/io.h"
#include "threads/synch.h"
#include "threads/thread.h"

/* See [8254] for hardware details of the 8254 timer chip. */

#if TIMER_FREQ < 19
#error 8254 timer requires TIMER_FREQ >= 19
#endif
#if TIMER_FREQ > 1000
#error TIMER_FREQ <= 1000 recommended
#endif

/* Number of timer ticks since OS booted. */
// OS가 부팅된 이후 타이머 틱 수
static int64_t ticks;

static struct list sleep_list;

/* Number of loops per timer tick.
   Initialized by timer_calibrate(). */
static unsigned loops_per_tick;

static intr_handler_func timer_interrupt;
static bool too_many_loops (unsigned loops);
static void busy_wait (int64_t loops);
static void real_time_sleep (int64_t num, int32_t denom);
static void thread_wake_up(void);
static bool sleep_list_less(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED);

bool sleep_list_less(const struct list_elem *a, const struct list_elem *b, void *aux) {
	const struct thread *t1 = list_entry(a, struct thread, elem);
	const struct thread *t2 = list_entry(b, struct thread, elem);

	return t1->wakeTime < t2->wakeTime;
};




/* Sets up the 8254 Programmable Interval Timer (PIT) to
   interrupt PIT_FREQ times per second, and registers the
   corresponding interrupt. */

/*
8254 타이머 하드웨어를 설정해서 초당 PIT_PREQ번 인터럽트를 발생시키고, 
그 인터럽트를 처리할 함수를 OS에 등록한다. 
*/
void
timer_init (void) {
	/* 8254 input frequency divided by TIMER_FREQ, rounded to
	   nearest. */
	uint16_t count = (1193180 + TIMER_FREQ / 2) / TIMER_FREQ;
	list_init(&sleep_list);
	outb (0x43, 0x34);    /* CW: counter 0, LSB then MSB, mode 2, binary. */
	outb (0x40, count & 0xff);
	outb (0x40, count >> 8);
	

	intr_register_ext (0x20, timer_interrupt, "8254 Timer");
}

/* Calibrates loops_per_tick, used to implement brief delays. */
void
timer_calibrate (void) {
	unsigned high_bit, test_bit;

	ASSERT (intr_get_level () == INTR_ON);
	printf ("Calibrating timer...  ");

	/* Approximate loops_per_tick as the largest power-of-two
	   still less than one timer tick. */
	loops_per_tick = 1u << 10;
	while (!too_many_loops (loops_per_tick << 1)) {
		loops_per_tick <<= 1;
		ASSERT (loops_per_tick != 0);
	}

	/* Refine the next 8 bits of loops_per_tick. */
	high_bit = loops_per_tick;
	for (test_bit = high_bit >> 1; test_bit != high_bit >> 10; test_bit >>= 1)
		if (!too_many_loops (high_bit | test_bit))
			loops_per_tick |= test_bit;

	printf ("%'"PRIu64" loops/s.\n", (uint64_t) loops_per_tick * TIMER_FREQ);
}

/* Returns the number of timer ticks since the OS booted. */
// OS가 부팅된 이후 타이머 틱의 수를 반환하는 함수?
int64_t
timer_ticks (void) {
	enum intr_level old_level = intr_disable (); //intr_disable(): 현재 인터럽트를 비활성하고 이전 인터럽트 상태를 반환
	int64_t t = ticks; 
	intr_set_level (old_level); // 이전 인터럽트 상태를 반환
	barrier (); 
	return t;
}

/* Returns the number of timer ticks elapsed since THEN, which
   should be a value once returned by timer_ticks(). */

// 프로그램(OS이지 않을까?)이 실행된 이후 흐른 틱 수를 반환하는 함수
int64_t
timer_elapsed (int64_t then) {
	return timer_ticks () - then;
}

/* Suspends execution for approximately TICKS timer ticks. */

/*
현재 문제
	busy waiting이 발생하고 있음
	busy waiting: 조건이 만족될 때까지 계속 검사만 하는 것

우리의 목표
	1. busy waiting을 없애는 것
	2. 효율적으로 스레드를 재운 뒤 지정된 시간에 깨우는 timer_sleep을 구현하는 것

방법
	1. 스레드를 깨울 시간을 계산
	2. 현재 스레드를 재움 어떻게? thread_current()함수를 통해 현재 스레드 확인
	3. 그리고 스레드 구조체에 wakeuptime을 넘겨서 깨울 시간 설정
*/
void
timer_sleep (int64_t ticks) {
	int64_t WakeUpTime = timer_ticks() + ticks;	// 종료시간 계산
	struct thread *curr = thread_current();		// 현재 실행 중인 스레드 정보 가져옴	
	curr->wakeTime = WakeUpTime;	// 현재 실행 중인 스레드의 종료시간을 설정
	
	// wakeTime을 비교해서 오름차순 정렬
	list_insert_ordered(&sleep_list, &curr->elem, sleep_list_less, NULL);
	
	// 이후 인터럽트를 disable하고 스레드 blocked 한 다음, 인터럽트 enable
	enum intr_level old_level = intr_disable (); 
	thread_block();
	intr_set_level (old_level);
	
	// ASSERT(): 에러 검출용 함수 
	// intr_get_level(): 인터럽트 활성화 상태를 반환하는 함수
	// 인터럽트 : CPU가 현재 실행 중인 프로그램을 잠시 멈추고, 긴급하게 처리해야 할 다른 작업을 먼저 처리하게 하는 신호나 메커니즘
}

/* Suspends execution for approximately MS milliseconds. */
void
timer_msleep (int64_t ms) {
	real_time_sleep (ms, 1000);
}

/* Suspends execution for approximately US microseconds. */
void
timer_usleep (int64_t us) {
	real_time_sleep (us, 1000 * 1000);
}

/* Suspends execution for approximately NS nanoseconds. */
void
timer_nsleep (int64_t ns) {
	real_time_sleep (ns, 1000 * 1000 * 1000);
}

/* Prints timer statistics. */
void
timer_print_stats (void) {
	printf ("Timer: %"PRId64" ticks\n", timer_ticks ());
}

/* Timer interrupt handler. */
static void
timer_interrupt (struct intr_frame *args UNUSED) {
	ticks++;
	thread_tick();

	thread_wake_up();
}

void thread_wake_up(void) {
	struct thread *sleep_thread; 

	while (!list_empty(&sleep_list)) {
		sleep_thread = list_entry(list_front(&sleep_list), struct thread, elem);
		if (timer_ticks() >= sleep_thread->wakeTime && !list_empty(&sleep_list)) {
			list_pop_front(&sleep_list);
	
			enum intr_level old_level = intr_disable (); 
			thread_unblock(sleep_thread);
			intr_set_level (old_level);
		} else {
			break;
		}
	}
}
/* Returns true if LOOPS iterations waits for more than one timer
   tick, otherwise false. */
static bool
too_many_loops (unsigned loops) {
	/* Wait for a timer tick. */
	int64_t start = ticks;
	while (ticks == start)
		barrier ();

	/* Run LOOPS loops. */
	start = ticks;
	busy_wait (loops);

	/* If the tick count changed, we iterated too long. */
	barrier ();
	return start != ticks;
}

/* Iterates through a simple loop LOOPS times, for implementing
   brief delays.

   Marked NO_INLINE because code alignment can significantly
   affect timings, so that if this function was inlined
   differently in different places the results would be difficult
   to predict. */
static void NO_INLINE
busy_wait (int64_t loops) {
	while (loops-- > 0)
		barrier ();
}

/* Sleep for approximately NUM/DENOM seconds. */
static void
real_time_sleep (int64_t num, int32_t denom) {
	/* Convert NUM/DENOM seconds into timer ticks, rounding down.

	   (NUM / DENOM) s
	   ---------------------- = NUM * TIMER_FREQ / DENOM ticks.
	   1 s / TIMER_FREQ ticks
	   */
	int64_t ticks = num * TIMER_FREQ / denom;

	ASSERT (intr_get_level () == INTR_ON);
	if (ticks > 0) {
		/* We're waiting for at least one full timer tick.  Use
		   timer_sleep() because it will yield the CPU to other
		   processes. */
		timer_sleep (ticks);
	} else {
		/* Otherwise, use a busy-wait loop for more accurate
		   sub-tick timing.  We scale the numerator and denominator
		   down by 1000 to avoid the possibility of overflow. */
		ASSERT (denom % 1000 == 0);
		busy_wait (loops_per_tick * num / 1000 * TIMER_FREQ / (denom / 1000));
	}
}