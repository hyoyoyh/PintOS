/* 이 파일은 Nachos 교육용 운영체제의 소스 코드에서 파생된 것입니다.
   아래는 Nachos의 저작권 고지문 전체입니다. */

/* 저작권 (c) 1992-1996 캘리포니아 대학교 이사회(The Regents of the University of California)
   모든 권리 보유.

   본 소프트웨어 및 문서를 어떠한 목적에서든 사용, 복사, 수정, 배포할 수 있는
   권한이 무료로, 서면 계약 없이 부여됩니다.
   단, 위의 저작권 고지문과 아래 두 단락이 모든 복사본에 포함되어야 합니다.

   캘리포니아 대학교는 본 소프트웨어 및 문서의 사용으로 인해 발생한
   직접적, 간접적, 특별한, 부수적, 또는 결과적 손해에 대해 책임을 지지 않습니다.
   이는 캘리포니아 대학교가 그러한 손해의 가능성을 미리 통보받았더라도 마찬가지입니다.

   캘리포니아 대학교는 상품성 또는 특정 목적에의 적합성에 대한
   묵시적 보증을 포함하되 이에 한정되지 않는 모든 보증을 명시적으로 부인합니다.
   본 소프트웨어는 “있는 그대로(as is)” 제공되며,
   캘리포니아 대학교는 유지보수, 지원, 업데이트, 개선, 수정에 대한 의무를 지지 않습니다.
*/

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

/* 세마포어 SEMA를 VALUE로 초기화합니다.
   세마포어는 음수가 아닌 정수이며, 두 가지 원자적 연산으로 다룹니다:

   - down 또는 "P": 값이 양수가 될 때까지 기다린 후, 1 감소시킴
   - up 또는 "V": 값을 1 증가시키고, 대기 중인 스레드가 있다면 하나를 깨움 */
   
void
sema_init (struct semaphore *sema, unsigned value) {
	ASSERT (sema != NULL);

	sema->value = value;
	list_init (&sema->waiters);
}


/* down 또는 "P" 연산:
   세마포어의 값이 양수가 될 때까지 기다린 후,
   원자적으로 값을 1 감소시킵니다.

   이 함수는 sleep할 수 있으므로 인터럽트 핸들러 내에서는 호출할 수 없습니다.
   인터럽트가 비활성화된 상태에서도 호출할 수 있지만,
   sleep하게 되면 다음에 스케줄된 스레드가 인터럽트를 다시 활성화할 수 있습니다.
   (이것이 sema_down 함수입니다.) */
void
sema_down (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL);
	ASSERT (!intr_context ());

	old_level = intr_disable ();
	if (sema->value == 0) { // 세마 0 티켓이 없읋때
		list_insert_ordered (&sema->waiters, &thread_current ()->elem, compare_priority,NULL); // 대기자 명단에 우선순위 순으로 넣음
		thread_block ();
	}
	sema->value--;
	intr_set_level (old_level);
}

/* 세마포어의 값이 0이 아닐 때만 down(P) 연산을 시도합니다.
   세마포어 값이 감소되면 true를, 그렇지 않으면 false를 반환합니다.

   이 함수는 인터럽트 핸들러에서도 호출될 수 있습니다. */
bool
sema_try_down (struct semaphore *sema) {
	enum intr_level old_level;
	bool success;

	ASSERT (sema != NULL);

	old_level = intr_disable ();
	if (sema->value > 0)
	{
		sema->value--;
		success = true;
	}
	else
		success = false;
	intr_set_level (old_level);

	return success;
}

/* up 또는 "V" 연산:
   세마포어 값을 1 증가시키고,
   대기 중인 스레드가 있다면 하나를 깨웁니다.

   이 함수는 인터럽트 핸들러 내에서도 호출될 수 있습니다. */
void
sema_up (struct semaphore *sema) {
	enum intr_level old_level;
	struct thread *front;
	ASSERT (sema != NULL);

	old_level = intr_disable ();
	if (!list_empty (&sema->waiters)) {// 기다리는 애가있으면 기다리고 있는 애중 우선순위 순으로 block 인애 꺠우기
	list_sort(&sema->waiters, compare_priority, NULL);
	front = list_entry (list_pop_front (&sema->waiters), struct thread, elem);
	thread_unblock (front);
	}
	sema->value++;
	if (front->priority > thread_current()->priority) {
        thread_yield(); // 우선순위 반영;
    }
	intr_set_level (old_level);
}

static void sema_test_helper (void *sema_);

/* 세마포어의 동작을 테스트하기 위한 함수입니다.
   두 스레드가 번갈아가며 제어권을 주고받으며 동작을 확인합니다.
   printf()를 사용하여 동작 과정을 볼 수 있습니다. */
void
sema_self_test (void) {
	struct semaphore sema[2];
	int i;

	printf ("Testing semaphores...");
	sema_init (&sema[0], 0);
	sema_init (&sema[1], 0);
	thread_create ("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
	for (i = 0; i < 10; i++)
	{
		sema_up (&sema[0]);
		sema_down (&sema[1]);
	}
	printf ("done.\n");
}

/* sema_self_test()에서 사용하는 스레드 함수입니다. */
static void
sema_test_helper (void *sema_) {
	struct semaphore *sema = sema_;
	int i;

	for (i = 0; i < 10; i++)
	{
		sema_down (&sema[0]);
		sema_up (&sema[1]);
	}
}

/* 락(LOCK)을 초기화합니다.
   락은 한 번에 단 하나의 스레드만 보유할 수 있습니다.
   재귀적 락은 아니므로, 락을 보유한 스레드가 다시 획득하려 하면 오류입니다.

   락은 세마포어(초기값 1)의 특수한 형태입니다.
   차이점은 다음과 같습니다:
   1) 세마포어는 값이 1보다 클 수 있지만, 락은 오직 하나의 스레드만 가질 수 있음.
   2) 세마포어는 소유자가 없지만, 락은 반드시 동일한 스레드가 acquire와 release를 모두 수행해야 함.
   이러한 제약이 불편할 경우 세마포어를 사용하는 것이 좋습니다. */
void
lock_init (struct lock *lock) {
	ASSERT (lock != NULL);
	lock->holder = NULL;
	sema_init (&lock->semaphore, 1);
}

/* 락을 획득(acquire)합니다.
   필요하다면 락이 풀릴 때까지 기다립니다.
   현재 스레드가 이미 이 락을 가지고 있어서는 안 됩니다.

   이 함수는 sleep할 수 있으므로 인터럽트 핸들러 내에서 호출하면 안 됩니다.
   인터럽트가 비활성화된 상태에서도 호출 가능하지만,
   sleep 중 인터럽트가 다시 켜질 수 있습니다. */

void
lock_acquire (struct lock *lock) {
	// 
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (!lock_held_by_current_thread (lock));
	struct thread *cur = thread_current();
	if (lock->holder != NULL) { // 이미 락 호더가 있다면?
		cur->waiton_lock = lock; // 현재 쓰레드가 어떤 락을 기다리는지 저장하고
		list_insert_ordered(&lock->holder->donators, &cur->for_donating_elem, compare_donate_priority, NULL); // 삽입정렬로 현재 쓰레드르 락 호더의 기부자로 등록
		donate_priority(cur, 0);// 현재 쓰레드를 기준으로 기부
	}
	sema_down (&lock->semaphore);
	cur->waiton_lock = NULL;
	lock->holder = thread_current ();

}




/* 락 획득을 시도하고, 성공하면 true를, 실패하면 false를 반환합니다.
   현재 스레드가 이미 락을 보유하고 있어서는 안 됩니다.

   이 함수는 sleep하지 않으므로 인터럽트 핸들러 내에서도 호출할 수 있습니다. */
bool
lock_try_acquire (struct lock *lock) {
	bool success;

	ASSERT (lock != NULL);
	ASSERT (!lock_held_by_current_thread (lock));

	success = sema_try_down (&lock->semaphore);
	if (success)
		lock->holder = thread_current ();
	return success;
}

/* 락을 해제(release)합니다.
   현재 스레드가 반드시 이 락을 보유 중이어야 합니다.
   (lock_release 함수)

   인터럽트 핸들러에서는 락을 획득할 수 없으므로,
   락을 해제하는 것도 의미가 없습니다. */
void
lock_release (struct lock *lock) {
	ASSERT (lock != NULL);
	ASSERT (lock_held_by_current_thread (lock));
	struct thread *cur = thread_current();
	donate_return(lock);
	lock->holder = NULL;
	sema_up (&lock->semaphore);
}

/* 현재 스레드가 LOCK을 보유 중이면 true, 아니면 false를 반환합니다.
   (다른 스레드가 보유 중인 락을 검사하는 것은 경쟁 상태를 유발할 수 있습니다.) */
bool
lock_held_by_current_thread (const struct lock *lock) {
	ASSERT (lock != NULL);

	return lock->holder == thread_current ();
}

/* 조건 변수 리스트 내의 세마포어 요소입니다. */
struct semaphore_elem {
	struct list_elem elem;              /* 리스트 요소 */
	struct semaphore semaphore;         /* 해당 세마포어 */
};

/* 조건 변수(COND)를 초기화합니다.
   조건 변수는 한 코드 블록이 어떤 조건을 “신호(signal)”로 알리고,
   다른 코드 블록이 그 신호를 받아 처리할 수 있도록 합니다. */
void
cond_init (struct condition *cond) {
	ASSERT (cond != NULL);

	list_init (&cond->waiters);
}

/* LOCK을 원자적으로 해제한 뒤, COND가 신호될 때까지 기다립니다.
   신호가 도착하면 LOCK을 다시 획득한 후 반환합니다.
   LOCK은 호출 전에 반드시 보유되어 있어야 합니다.

   이 함수는 “Mesa” 스타일 모니터를 구현합니다.
   즉, 신호를 보낸 스레드와 받는 스레드의 동작이 원자적이지 않습니다.
   따라서 wait가 끝난 후 조건을 다시 확인하고 필요 시 재대기해야 합니다.

   조건 변수 하나는 하나의 락과 연관되지만,
   하나의 락은 여러 조건 변수와 연관될 수 있습니다.
   즉, 락과 조건 변수는 1:N 관계입니다.

   이 함수는 sleep할 수 있으므로 인터럽트 핸들러 내에서는 호출할 수 없습니다.
   인터럽트가 비활성화된 상태에서도 호출 가능하지만,
   sleep 시 인터럽트가 다시 활성화될 수 있습니다. */
void
cond_wait (struct condition *cond, struct lock *lock) {
	struct semaphore_elem waiter;

	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	sema_init (&waiter.semaphore, 0);
	list_insert_ordered (&cond->waiters, &waiter.elem, compare_priority, NULL);
	lock_release (lock);
	sema_down (&waiter.semaphore);
	lock_acquire (lock);
}

/* COND(LOCK으로 보호됨)를 기다리는 스레드가 있다면,
   그 중 하나를 깨워서 wait 상태에서 벗어나게 합니다.
   호출 전 LOCK을 반드시 보유하고 있어야 합니다.

   인터럽트 핸들러는 락을 획득할 수 없으므로,
   조건 변수에 신호를 보내는 것도 의미 없습니다. */
void
cond_signal (struct condition *cond, struct lock *lock UNUSED) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));
	
	if (!list_empty (&cond->waiters))
		list_sort(&cond->waiters, compare_priority, NULL);
		sema_up (&list_entry (list_pop_front (&cond->waiters),
					struct semaphore_elem, elem)->semaphore);
}

/* COND(LOCK으로 보호됨)를 기다리는 모든 스레드를 깨웁니다.
   호출 전 LOCK을 반드시 보유하고 있어야 합니다.

   인터럽트 핸들러에서는 락을 획득할 수 없으므로,
   조건 변수에 신호를 보내는 것도 의미 없습니다. */
void
cond_broadcast (struct condition *cond, struct lock *lock) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);

	while (!list_empty (&cond->waiters))
		cond_signal (cond, lock);
}
