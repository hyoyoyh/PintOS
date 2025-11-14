/* This file is derived from source code for the Nachos
   instructional operating system.  The Nachos copyright notice
   is reproduced in full below. */

/* Copyright (c) 1992-1996 The Regents of the University of California.
   All rights reserved.

   Permission to use, copy, modify, and distribute this software
   and its documentation for any purpose, without fee, and
   without written agreement is hereby granted, provided that the
   above copyright notice and the following two paragraphs appear
   in all copies of this software.

   IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO
   ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR
   CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF THIS SOFTWARE
   AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA
   HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY
   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
   PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
   BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
   PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
   MODIFICATIONS.
   */

   #include "threads/synch.h"
   #include <stdio.h>
   #include <string.h>
   #include "threads/interrupt.h"
   #include "threads/thread.h"
   
   static bool priority_less(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED) {
	   const struct thread *t1 = list_entry(a, struct thread, elem);
	   const struct thread *t2 = list_entry(b, struct thread, elem);
   
	   return t1->priority > t2->priority;
   } 
   
   void priority_donation(struct lock *lock_){
	   lock_->holder->priority = thread_current()->priority;
   };
   
   /*
   세마포어 SEMA를 VALUE로 초기화합니다.
   세마포어는 음수가 아닌 정수와, 그 값을 제어하기 위한 두 가지 **원자적 연산(atomic operation)**으로 구성됩니다
	   down 또는 "P" 연산: 값이 양수가 될 때까지 기다린 후, 그 값을 1 감소시킵니다.
	   up 또는 "V" 연산: 값을 1 증가시키며, 대기 중인 스레드가 있다면 하나를 깨웁니다.
   */
   void
   sema_init (struct semaphore *sema, unsigned value) {
	   ASSERT (sema != NULL);
   
	   sema->value = value;  // 세마포어의 value를 넘겨받은 인자로 설정
	   list_init (&sema->waiters); // 세마포어의 대기 스레드 리스트를 초기화
   }
   
   /*
   세마포어에 대한 down 또는 "P" 연산을 수행합니다.
   이 연산은 SEMA의 값이 양수가 될 때까지 기다린 후, 그 값을 원자적으로 1 감소시킵니다.
   
   이 함수는 대기(sleep) 상태에 들어갈 수 있으므로, 인터럽트 핸들러 내에서 호출해서는 안 됩니다.
   인터럽트가 비활성화된 상태에서 호출할 수는 있지만, 만약 이 함수가 대기 상태에 들어가면 다음에 스케줄된 스레드가 인터럽트를 다시 활성화할 가능성이 높습니다.
   */
   void
   sema_down (struct semaphore *sema) {
	   enum intr_level old_level;
   
	   ASSERT (sema != NULL);
	   ASSERT (!intr_context ());
   
	   old_level = intr_disable ();  // 인터럽트 비활성화
	   while (sema->value == 0) {	// 세마포어의 value == 0이면 이미 실행 중인 프로세스가 있다는 뜻.
		   list_insert_ordered(&sema->waiters, &thread_current()->elem, priority_less, NULL);// 그래서 대기 리스트에 push하고 block
		   thread_block ();
	   }
	   sema->value--; // value--를 해서 
	   intr_set_level (old_level); // 인터럽트 활성화
   }
   
   /*
   세마포어에 대해 down(또는 “P”) 연산을 수행하되, 세마포어 값이 이미 0인 경우에는 수행하지 않는다. 
   세마포어 값을 감소시켰다면 true를, 그렇지 않다면 false를 반환한다.
   
   이 함수는 인터럽트 핸들러에서 호출될 수 있다.
   */
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
   
   /* 세마포어에 대해 up(또는 “V”) 연산을 수행한다.
	  이 연산은 SEMA의 값을 1 증가시키며, SEMA를 기다리고 있는 스레드가 있다면 그중 하나를 깨워 실행 가능 상태로 만든다.
	  이 함수는 인터럽트 핸들러에서도 호출될 수 있다. */
   void
   sema_up (struct semaphore *sema) {
	   enum intr_level old_level;
   
	   ASSERT (sema != NULL);
   
	   old_level = intr_disable ();
	   if (!list_empty (&sema->waiters)) // 대기 중인 스레드가 있다면
		   thread_unblock(list_entry (list_pop_front (&sema->waiters), // 스레드를 ready 상태로 바꿈. 
					   struct thread, elem));
	   sema->value++; 
	   intr_set_level (old_level);
   }
   
   static void sema_test_helper (void *sema_);
   
   /* Self-test for semaphores that makes control "ping-pong"
	  between a pair of threads.  Insert calls to printf() to see
	  what's going on. */
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
   
   /* Thread function used by sema_self_test(). */
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
   
   /* LOCK을 초기화한다.
	  락(lock)은 한 번에 오직 하나의 스레드만이 소유할 수 있다. 
	  우리의 락은 “재귀적(recursive)”이지 않으므로, 이미 락을 보유한 스레드가 다시 그 락을 획득하려 시도하면 오류가 발생한다.
   
	  락은 초기값이 1인 세마포어(semaphore)의 특수한 형태이다. 그러나 락과 세마포어 사이에는 두 가지 중요한 차이점이 있다.
	  첫째, 세마포어의 값은 1보다 클 수 있지만, 락은 동시에 하나의 스레드만이 소유할 수 있다.
	  둘째, 세마포어는 소유자(owner)의 개념이 없어 한 스레드가 세마포어를 “down”하고 다른 스레드가 “up”할 수 있다.
	  반면, 락은 동일한 스레드가 반드시 락을 획득(acquire)하고 해제(release)해야 한다.
   
	  이러한 제약이 너무 불편하다면, 락 대신 세마포어를 사용하는 것이 더 적절하다는 신호일 수 있다. */
   void
   lock_init (struct lock *lock) {
	   ASSERT (lock != NULL);
   
	   lock->holder = NULL; // lock
	   sema_init (&lock->semaphore, 1);
   }
   
   /* LOCK을 획득(acquire)한다.
	  필요하다면, 락이 사용 가능해질 때까지 스레드는 잠들어(sleep) 대기한다.
	  현재 스레드는 이미 이 락을 보유하고 있어서는 안 된다.
   
	  이 함수는 잠들 수(sleep) 있으므로, 인터럽트 핸들러 내에서는 호출할 수 없다.
	  인터럽트가 비활성화된 상태에서 호출할 수는 있지만, 대기(sleep)가 필요한 경우 인터럽트는 다시 활성화된다. */
   
   /* lock을 요청함 -> sema_down에서 lock을 점유하고 있는 스레드가 있으면
	  block하고 wait_list에 넣음. */
   
   void
   lock_acquire (struct lock *lock) {
	   ASSERT (lock != NULL);
	   ASSERT (!intr_context ());
	   enum intr_level old_level;	// 인터럽트 상태를 확인하기 위한 변수 선언
   
	   while(!sema_try_down(&lock->semaphore))	// !sema_try_down(&lock->semaphore)로 lock 점유 시도
	   {	
		   // 락 점유 실패 시 현재 스레드 block
		   old_level = intr_disable(); // 인터럽트를 비활성화
		   priority_donation(lock);
		   list_insert_ordered(&lock->semaphore.waiters, &thread_current()->elem, priority_less, NULL); // 대기 리스트에 우선순위를 기준으로 현재 스레드를 내림차순 삽입정렬
		   thread_block (); // 현재 스레드 block
		   intr_set_level(old_level); // 인터럽트 활성화
	   }
   
	   // 락 점유 성공 시 holder 변경
	   lock->holder = thread_current ();
   }
   
   /* LOCK을 획득(try acquire)하려 시도하고, 성공하면 true, 실패하면 false를 반환한다.
	  현재 스레드는 이미 이 락을 보유하고 있어서는 안 된다.
   
	  이 함수는 절대 잠들지(sleep) 않으므로, 인터럽트 핸들러 내에서도 호출할 수 있다. */
   
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
   
   /* 현재 스레드가 보유하고 있는 LOCK을 해제합니다.
	  이 함수는 lock_release 함수입니다.
   
	  인터럽트 핸들러에서는 락을 획득할 수 없기 때문에,
	  인터럽트 핸들러 내에서 락을 해제하려는 시도는 의미가 없습니다. */
   void
   lock_release (struct lock *lock) {
	   ASSERT (lock != NULL);
	   ASSERT (lock_held_by_current_thread (lock)); // 현재 스레드가 lock을 점유하고 있는지 확인
	   
	   lock->holder = NULL; // 현재 lock을 점유하고 있는 스레드를 제거
	   sema_up (&lock->semaphore); // sema_up을 통해 value++를 함(lock을 점유하고 있는 스레드가 없음을 의미)
	   
	   thread_set_priority(thread_current()->priority_before_donation); // 현재 스레드의 priority를 기부 받기 전 priority로 변경
   }
   
   /* 현재 스레드가 LOCK을 보유하고 있다면 true,
	  그렇지 않다면 false를 반환합니다.
   
	  (참고: 다른 스레드가 락을 보유하고 있는지 확인하는 것은
	  경쟁 상태(race condition)가 발생할 수 있으므로 안전하지 않습니다.) */
   bool
   lock_held_by_current_thread (const struct lock *lock) {
	   ASSERT (lock != NULL);
   
	   return lock->holder == thread_current ();
   }
   
   /* One semaphore in a list. */
   struct semaphore_elem {
	   struct list_elem elem;              /* List element. */
	   struct semaphore semaphore;         /* This semaphore. */
   };
   
   /* 조건 변수 COND를 초기화합니다.
	  조건 변수(condition variable)는 한 코드가 특정 조건의 발생을 신호(signal)로 보낼 수 있게 하고,
	  다른 코드가 그 신호를 받아 해당 조건에 따라 동작할 수 있도록 해주는 동기화 도구입니다. */
   void
   cond_init (struct condition *cond) {
	   ASSERT (cond != NULL);
   
	   list_init (&cond->waiters);
   }
   
   /* 이 함수는 LOCK을 원자적으로 해제한 뒤,
	  다른 코드가 COND(조건 변수)에 신호를 보낼 때까지 대기(wait)합니다.
	  COND가 신호를 받으면, 함수는 다시 LOCK을 획득한 후 반환됩니다.
	  이 함수를 호출하기 전에는 반드시 LOCK을 보유하고 있어야 합니다.
   
	  동작 방식 (Mesa 스타일 모니터)
   
	  이 함수가 구현하는 모니터는 "Mesa 스타일”이며, “Hoare 스타일”이 아닙니다.
	  즉, 신호(signal)를 보내는 쪽과 대기(wait)하는 쪽의 동작이 원자적으로 연결되지 않습니다.
   
	  따라서, wait가 끝난 후에도 조건이 여전히 만족되지 않을 수 있습니다.
	  그렇기 때문에 일반적으로 wait가 끝난 뒤에는 조건을 다시 검사하고,
	  필요하다면 다시 대기(wait)해야 합니다.
   
	  락과 조건 변수의 관계
   
	  하나의 조건 변수(CONDITION VARIABLE)는 오직 하나의 락(LOCK)에만 연관됩니다.
   
	  하지만 하나의 락은 여러 개의 조건 변수와 연결될 수 있습니다.
	  → 즉, “하나의 락 ↔ 여러 조건 변수” 구조입니다.
   
	  주의사항
   
	  이 함수는 슬립(sleep) 상태에 들어갈 수 있으므로,
	  인터럽트 핸들러 내에서는 호출할 수 없습니다.
   
	  인터럽트가 비활성화된 상태에서도 호출할 수는 있지만,
	  만약 슬립이 필요해진다면 인터럽트는 다시 활성화됩니다. */
   void
   cond_wait (struct condition *cond, struct lock *lock) {
	   struct semaphore_elem waiter;
   
	   ASSERT (cond != NULL);
	   ASSERT (lock != NULL);
	   ASSERT (!intr_context ());
	   ASSERT (lock_held_by_current_thread (lock));
   
	   sema_init (&waiter.semaphore, 0);
	   list_push_back (&cond->waiters, &waiter.elem);
	   lock_release (lock);
	   sema_down (&waiter.semaphore);
	   lock_acquire (lock);
   }
   
   /* 만약 LOCK으로 보호된 COND(조건 변수)를 기다리고 있는 스레드가 있다면,
	  이 함수는 그중 하나를 깨워 대기 상태에서 벗어나도록 신호(signal)를 보냅니다.
	  이 함수를 호출하기 전에는 반드시 LOCK을 보유하고 있어야 합니다.
	  인터럽트 핸들러는 락을 획득할 수 없기 때문에,
	  인터럽트 핸들러 내에서 조건 변수에 신호를 보내려는 시도는 의미가 없습니다. */
   
   void
   cond_signal (struct condition *cond, struct lock *lock UNUSED) {
	   ASSERT (cond != NULL);
	   ASSERT (lock != NULL);
	   ASSERT (!intr_context ());
	   ASSERT (lock_held_by_current_thread (lock));
   
	   if (!list_empty (&cond->waiters))
		   sema_up (&list_entry (list_pop_front (&cond->waiters),
					   struct semaphore_elem, elem)->semaphore);
   }
   
   /* LOCK으로 보호된 COND(조건 변수)를 기다리고 있는 모든 스레드(있다면 모두)를 깨웁니다.
	  이 함수를 호출하기 전에는 반드시 LOCK을 보유하고 있어야 합니다.
	  인터럽트 핸들러는 락을 획득할 수 없기 때문에,
	  인터럽트 핸들러 내에서 조건 변수에 신호를 보내려는 시도는 의미가 없습니다. */
   void
   cond_broadcast (struct condition *cond, struct lock *lock) {
	   ASSERT (cond != NULL);
	   ASSERT (lock != NULL);
   
	   while (!list_empty (&cond->waiters))
		   cond_signal (cond, lock);
   }   