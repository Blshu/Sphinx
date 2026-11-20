.section .text
.extern exception_handler

// 에러 코드가 없는 예외용 매크로 (가짜 에러 코드 0 push)
.macro ISR_NOERR num
.global isr_stub_\num
isr_stub_\num:
    push $0
    push $\num
    jmp isr_common_stub
.endm

// CPU가 에러 코드를 자동으로 push하는 예외용 매크로
.macro ISR_ERR num
.global isr_stub_\num
isr_stub_\num:
    push $\num
    jmp isr_common_stub
.endm

// 0 ~ 31번 CPU 예외 스텁 생성
ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_ERR   21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_ERR   29
ISR_ERR   30
ISR_NOERR 31

// 공통 핸들러: 모든 범용 레지스터 백업 -> C 함수 호출 -> 복구 -> iretq
isr_common_stub:
    push %rax
    push %rbx
    push %rcx
    push %rdx
    push %rsi
    push %rdi
    push %rbp
    push %r8
    push %r9
    push %r10
    push %r11
    push %r12
    push %r13
    push %r14
    push %r15

    mov %rsp, %rdi          // 첫 번째 인자(RDI)에 현재 스택 포인터(registers_t*) 전달
    call exception_handler

    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %r11
    pop %r10
    pop %r9
    pop %r8
    pop %rbp
    pop %rdi
    pop %rsi
    pop %rdx
    pop %rcx
    pop %rbx
    pop %rax

    add $16, %rsp           // 스택에 넣었던 예외 번호(8B) + 에러 코드(8B) 정리
    iretq                   // 64비트 인터럽트 복귀!

// C에서 루프 돌며 등록하기 편하게 만든 함수 포인터 테이블
.section .rodata
.global isr_stub_table
isr_stub_table:
    .quad isr_stub_0,  isr_stub_1,  isr_stub_2,  isr_stub_3
    .quad isr_stub_4,  isr_stub_5,  isr_stub_6,  isr_stub_7
    .quad isr_stub_8,  isr_stub_9,  isr_stub_10, isr_stub_11
    .quad isr_stub_12, isr_stub_13, isr_stub_14, isr_stub_15
    .quad isr_stub_16, isr_stub_17, isr_stub_18, isr_stub_19
    .quad isr_stub_20, isr_stub_21, isr_stub_22, isr_stub_23
    .quad isr_stub_24, isr_stub_25, isr_stub_26, isr_stub_27
    .quad isr_stub_28, isr_stub_29, isr_stub_30, isr_stub_31