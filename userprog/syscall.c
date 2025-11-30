#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"

/* 사용자 주소의 유효성을 검사하는 함수 */
static void
check_address(const void *addr) {
    if (addr == NULL || !is_user_vaddr(addr)) {
        exit(-1);
    }
}

/* 버퍼의 유효성을 검사하는 함수 */
static void
check_buffer(const void *buffer, unsigned size) {
    for (unsigned i = 0; i < size; i++) {
        check_address(buffer + i);
    }
}

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

void exit(int status) {
	struct thread *cur = thread_current();
	cur->exit_status = status;
	printf("%s: exit(%d)\n", cur->name, status);
	thread_exit();
}

int write(int fd, const void *buffer, unsigned size) {
	check_buffer(buffer, size); // 버퍼 유효성 검사
	if (fd == 1) { // STDOUT
		putbuf(buffer, size);
		return size;
	}
	return -1;
}
/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f) {
	// 시스템 콜 번호는 rax 레지스터에 저장됩니다.
	uint64_t syscall_no = f->R.rax;

	switch (syscall_no) {
		case SYS_EXIT:
			exit(f->R.rdi);
			break;
		case SYS_WRITE:
			check_address(f->R.rsi); // buffer 주소 유효성 검사
			f->R.rax = write(f->R.rdi, (void *)f->R.rsi, f->R.rdx);
			break;
		case SYS_HALT:
			power_off();
			break;
		default:
			thread_exit ();
	}
}
