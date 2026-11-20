// thank you osdev.org

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <limine.h>
#include <stdarg.h>
#define PORT 0x3f8 // 안뇽 난 COM1이라고 해 난 정말 큐티해
// Set the base revision to 6, this is recommended as this is the latest
// base revision described by the Limine boot protocol specification.
// See specification for further info.

#define SEG_DESCTYPE(x)  ((x) << 0x04)
#define SEG_PRES(x)      ((x) << 0x07)
#define SEG_LONG(x)      ((x) << 0x0D) // 64-bit Long Mode!
#define SEG_SIZE(x)      ((x) << 0x0E) // 64-bit 코드 세그먼트에선 반드시 0!
#define SEG_GRAN(x)      ((x) << 0x0F)
#define SEG_PRIV(x)     (((x) &  0x03) << 0x05)

#define SEG_DATA_RDWR    0x02
#define SEG_CODE_EXRD    0x0A

// 64비트 커널 코드 (0x08): LONG(1), SIZE(0), PRIV(0)
#define GDT_CODE_PL0 (SEG_DESCTYPE(1) | SEG_PRES(1) | SEG_LONG(1) | SEG_SIZE(0) | SEG_GRAN(1) | SEG_PRIV(0) | SEG_CODE_EXRD)
// 64비트 커널 데이터 (0x10): LONG(0), SIZE(0), PRIV(0)
#define GDT_DATA_PL0 (SEG_DESCTYPE(1) | SEG_PRES(1) | SEG_LONG(0) | SEG_SIZE(0) | SEG_GRAN(1) | SEG_PRIV(0) | SEG_DATA_RDWR)
// 64비트 유저 데이터 (0x18): PRIV(3)
#define GDT_DATA_PL3 (SEG_DESCTYPE(1) | SEG_PRES(1) | SEG_LONG(0) | SEG_SIZE(0) | SEG_GRAN(1) | SEG_PRIV(3) | SEG_DATA_RDWR)
// 64비트 유저 코드 (0x20): LONG(1), SIZE(0), PRIV(3)
#define GDT_CODE_PL3 (SEG_DESCTYPE(1) | SEG_PRES(1) | SEG_LONG(1) | SEG_SIZE(0) | SEG_GRAN(1) | SEG_PRIV(3) | SEG_CODE_EXRD)

#define PAGE_SIZE 0x1000
#define FRAME_FREE 0x00
#define FRAME_USED 0x01
struct gdtr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

// IDT 64-Bit
struct InterruptDescriptor64 {
   uint16_t offset_1;        // offset bits 0..15
   uint16_t selector;        // a code segment selector in GDT or LDT
   uint8_t  ist;             // bits 0..2 holds Interrupt Stack Table offset, rest of bits zero.
   uint8_t  type_attributes; // gate type, dpl, and p fields
   uint16_t offset_2;        // offset bits 16..31
   uint32_t offset_3;        // offset bits 32..63
   uint32_t zero;            // reserved
};

struct idtr {
   uint16_t limit;
   uint64_t base;
} __attribute__((packed));

__attribute__((aligned(0x10)))

typedef struct {
    uint16_t isr_low;
    uint16_t kernel_cs;
    uint8_t  ist;
    uint8_t  attributes;
    uint16_t isr_mid;
    uint32_t isr_high;
    uint32_t reserved;
} __attribute__((packed)) idt_entry_t;

typedef struct {
    uint16_t limit;
    uint64_t base; // ✅ 반드시 64비트!
} __attribute__((packed)) idtr_t;

typedef struct {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t int_no, err_code;
    uint64_t rip, cs, rflags, rsp, ss;
} __attribute__((packed)) registers_t;

__attribute__((aligned(0x10)))
static idtr_t idtr; // 이거 안넣으면 대가리 빡통임 씹

__attribute__((aligned(0x10)))
static idt_entry_t idt[256];

extern void *isr_stub_table[];

static const char *exception_messages[] = {
    "Division By Zero", "Debug", "Non Maskable Interrupt", "Breakpoint",
    "Into Detected Overflow", "Out of Bounds", "Invalid Opcode", "No Coprocessor",
    "Double Fault", "Coprocessor Segment Overrun", "Bad TSS", "Segment Not Present",
    "Stack Fault", "General Protection Fault", "Page Fault", "Unknown Interrupt",
    "Coprocessor Fault", "Alignment Check", "Machine Check", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved"
};


static uint64_t gdt[5];
static struct gdtr gdt_ptr;

static uint64_t gdt[5];
static struct gdtr gdt_ptr;

static uint64_t create_descriptor(uint32_t base, uint32_t limit, uint16_t flag) {
    uint64_t descriptor;
    descriptor  =  limit       & 0x000F0000;
    descriptor |= (flag <<  8) & 0x00F0FF00;
    descriptor |= (base >> 16) & 0x000000FF;
    descriptor |=  base        & 0xFF000000;
    descriptor <<= 32;
    descriptor |= base  << 16;
    descriptor |= limit  & 0x0000FFFF;
    return descriptor;
}

void init_gdt(void) {
    gdt[0] = create_descriptor(0, 0, 0);                 // 0x00: Null
    gdt[1] = create_descriptor(0, 0x000FFFFF, GDT_CODE_PL0); // 0x08: Kernel Code
    gdt[2] = create_descriptor(0, 0x000FFFFF, GDT_DATA_PL0); // 0x10: Kernel Data
    gdt[3] = create_descriptor(0, 0x000FFFFF, GDT_DATA_PL3); // 0x18: User Data
    gdt[4] = create_descriptor(0, 0x000FFFFF, GDT_CODE_PL3); // 0x20: User Code

    gdt_ptr.limit = sizeof(gdt) - 1;
    gdt_ptr.base  = (uint64_t)&gdt;

    // 1. lgdt로 새 GDT 등록
    // 2. retfq(far return)로 CS 레지스터를 0x08(Kernel Code)로 갱신
    // 3. DS, ES, FS, GS, SS 레지스터를 0x10(Kernel Data)로 갱신
    asm volatile (
        "lgdt %0\n\t"
        "pushq $0x08\n\t"
        "leaq 1f(%%rip), %%rax\n\t"
        "pushq %%rax\n\t"
        "lretq\n\t"
        "1:\n\t"
        "movw $0x10, %%ax\n\t"
        "movw %%ax, %%ds\n\t"
        "movw %%ax, %%es\n\t"
        "movw %%ax, %%fs\n\t"
        "movw %%ax, %%gs\n\t"
        "movw %%ax, %%ss\n\t"
        :
        : "m"(gdt_ptr)
        : "rax", "memory"
    );
}

/* the linear framebuffer */
extern char *fb;
/* number of bytes in each line, it's possible it's not screen width * bytesperpixel! */
extern int scanline;
/* import the font contained in the object file created above */
extern char _binary_font_start[];

#define PSF1_FONT_MAGIC 0x0436

typedef struct {
    uint16_t magic;
    uint8_t fontMode;
    uint8_t characterSize;
} __attribute__((packed)) PSF1_Header;

extern char _binary_font_psf_start;

static struct limine_framebuffer *g_fb = NULL;
static uint32_t cursor_x = 16;
static uint32_t cursor_y = 16;

#define PIXEL uint32_t   /* pixel pointer */

__attribute__((used, section(".limine_requests")))
static volatile uint64_t limine_base_revision[] = LIMINE_BASE_REVISION(6);

// The Limine requests can be placed anywhere, but it is important that
// the compiler does not optimise them away, so, usually, they should
// be made volatile or equivalent, _and_ they should be accessed at least
// once or marked as used with the "used" attribute as done here.

__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
    .revision = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST_ID,
    .revision = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST_ID,
    .revision = 0
};

// Finally, define the start and end markers for the Limine requests.
// These can also be moved anywhere, to any .c file, as seen fit.

__attribute__((used, section(".limine_requests_start")))
static volatile uint64_t limine_requests_start_marker[] = LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t limine_requests_end_marker[] = LIMINE_REQUESTS_END_MARKER;

// GCC and Clang reserve the right to generate calls to the following
// 4 functions even if they are not directly called.
// Implement them as the C specification mandates.
// DO NOT remove or rename these functions, or stuff will eventually break!
// They CAN be moved to a different .c file.

void *memcpy(void *restrict dest, const void *restrict src, size_t n) {
    uint8_t *pdest = dest;
    const uint8_t *psrc = src;

    for (size_t i = 0; i < n; i++) {
        pdest[i] = psrc[i];
    }

    return dest;
}

void *memset(void *s, int c, size_t n) {
    uint8_t *p = s;

    for (size_t i = 0; i < n; i++) {
        p[i] = (uint8_t)c;
    }

    return s;
}

void *memmove(void *dest, const void *src, size_t n) {
    uint8_t *pdest = dest;
    const uint8_t *psrc = src;

    if ((uintptr_t)src > (uintptr_t)dest) {
        for (size_t i = 0; i < n; i++) {
            pdest[i] = psrc[i];
        }
    } else if ((uintptr_t)src < (uintptr_t)dest) {
        for (size_t i = n; i > 0; i--) {
            pdest[i-1] = psrc[i-1];
        }
    }

    return dest;
}

int memcmp(const void *s1, const void *s2, size_t n) {
    const uint8_t *p1 = s1;
    const uint8_t *p2 = s2;

    for (size_t i = 0; i < n; i++) {
        if (p1[i] != p2[i]) {
            return p1[i] < p2[i] ? -1 : 1;
        }
    }

    return 0;
}

// Halt and catch fire function.
static void hcf(void) {
    for (;;) {
        asm ("hlt");
    }
}

void idt_set_descriptor(uint8_t vector, void *isr, uint8_t flags) {
    idt_entry_t *desc = &idt[vector];
    uint64_t addr = (uint64_t)isr;

    desc->isr_low    = addr & 0xFFFF;
    desc->kernel_cs  = 0x08; // GDT Kernel Code
    desc->ist        = 0;
    desc->attributes = flags;
    desc->isr_mid    = (addr >> 16) & 0xFFFF;
    desc->isr_high   = (addr >> 32) & 0xFFFFFFFF;
    desc->reserved   = 0;
}

void init_idt(void) {
    idtr.base  = (uintptr_t)&idt[0];
    idtr.limit = (uint16_t)sizeof(idt_entry_t) * 256 - 1;

    for (uint8_t vector = 0; vector < 32; vector++) {
        idt_set_descriptor(vector, isr_stub_table[vector], 0x8E);
    }

    asm volatile ("lidt %0" : : "m"(idtr));
}

typedef uint64_t pageframe_t;

static uint8_t *frame_map = NULL;
static uint64_t npages = 0;
static uint64_t pmm_hhdm_offset = 0;

// 20개씩 미리 뽑아두는 캐시 (튜토리얼 핵심 최적화!)
static pageframe_t pre_frames[20];
static uint8_t allocate_cache = 1;
static uint8_t pframe_idx = 0;

void pmm_init(struct limine_memmap_response *memmap, uint64_t hhdm_offset) {
    pmm_hhdm_offset = hhdm_offset;

    // 1. USABLE 구역 중 가장 높은 물리 주소 계산
    uint64_t highest_addr = 0;
    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *entry = memmap->entries[i];
        if (entry->type == LIMINE_MEMMAP_USABLE) {
            uint64_t top = entry->base + entry->length;
            if (top > highest_addr) highest_addr = top;
        }
    }

    npages = highest_addr / PAGE_SIZE;

    // 2. frame_map(페이지당 1바이트)을 저장할 USABLE 구역 찾기
    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *entry = memmap->entries[i];
        if (entry->type == LIMINE_MEMMAP_USABLE && entry->length >= npages) {
            frame_map = (uint8_t *)(entry->base + pmm_hhdm_offset);
            break;
        }
    }

    // 3. 일단 모든 페이지를 USED(0x01)로 잠그기
    for (uint64_t i = 0; i < npages; i++) {
        frame_map[i] = FRAME_USED;
    }

    // 4. USABLE 구역만 FREE(0x00)로 열어주기
    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *entry = memmap->entries[i];
        if (entry->type == LIMINE_MEMMAP_USABLE) {
            uint64_t start = entry->base / PAGE_SIZE;
            uint64_t count = entry->length / PAGE_SIZE;
            for (uint64_t p = 0; p < count; p++) {
                frame_map[start + p] = FRAME_FREE;
            }
        }
    }

    // 5. frame_map 자기 자신이 차지한 페이지들은 다시 USED로 보호!
    uint64_t map_phys = (uint64_t)frame_map - pmm_hhdm_offset;
    uint64_t map_start = map_phys / PAGE_SIZE;
    uint64_t map_pages = (npages + PAGE_SIZE - 1) / PAGE_SIZE;
    for (uint64_t p = 0; p < map_pages; p++) {
        frame_map[map_start + p] = FRAME_USED;
    }
    frame_map[0] = FRAME_USED; // 0x0(NULL) 보호
}

static pageframe_t kalloc_frame_int(void) {
    for (uint64_t i = 1; i < npages; i++) {
        if (frame_map[i] == FRAME_FREE) {
            frame_map[i] = FRAME_USED;
            return i * PAGE_SIZE; // 64비트 물리 주소 반환
        }
    }
    return 0; // Out of Memory!
}

pageframe_t kalloc_frame(void) {
    if (pframe_idx == 20) {
        allocate_cache = 1;
    }
    if (allocate_cache == 1) {
        for (int i = 0; i < 20; i++) {
            pre_frames[i] = kalloc_frame_int();
        }
        pframe_idx = 0;
        allocate_cache = 0;
    }
    return pre_frames[pframe_idx++];
}

void kfree_frame(pageframe_t addr) {
    if (addr == 0) return;
    uint64_t index = addr / PAGE_SIZE;
    if (index < npages) {
        frame_map[index] = FRAME_FREE;
    }
}

// Scale an 8-bit colour channel value to the size the framebuffer gives the
// channel and move it into place within a pixel.
static uint32_t fb_channel(uint8_t value, uint8_t mask_size, uint8_t mask_shift) {
    uint64_t max = ((uint64_t)1 << mask_size) - 1;
    return (uint32_t)((value * max / 255) << mask_shift);
}

// Build a pixel from 8-bit red, green and blue values following the channel
// layout of the framebuffer.
static uint32_t fb_pixel(struct limine_framebuffer *fb, uint8_t red, uint8_t green, uint8_t blue) {
    return fb_channel(red, fb->red_mask_size, fb->red_mask_shift)
         | fb_channel(green, fb->green_mask_size, fb->green_mask_shift)
         | fb_channel(blue, fb->blue_mask_size, fb->blue_mask_shift);
}

// Print a nice pattern to a framebuffer as an example.
static void fb_pattern(struct limine_framebuffer *fb) {
    volatile uint32_t *fb_ptr = fb->address;
    for (size_t y = 0; y < fb->height; y++) {
        for (size_t x = 0; x < fb->width; x++) {
            uint8_t nX = x * 255 / fb->width;
            uint8_t nY = y * 255 / fb->height;
            fb_ptr[y * (fb->pitch / 4) + x] = fb_pixel(fb, 0, nY, nX);
        }
    }
}

static void fb_draw_char(char c, uint32_t x, uint32_t y, uint32_t fg_color) {
    if (!g_fb) return;

    PSF1_Header *font = (PSF1_Header *)&_binary_font_psf_start;
    if (font->magic != PSF1_FONT_MAGIC) return;

    // 헤더(4바이트) 바로 뒤가 비트맵 시작점!
    uint8_t *glyph = (uint8_t *)&_binary_font_psf_start
                   + sizeof(PSF1_Header)
                   + ((uint8_t)c * font->characterSize);

    volatile uint32_t *fb_ptr = g_fb->address;
    uint32_t pitch_pixels = g_fb->pitch / 4;

    for (uint32_t row = 0; row < font->characterSize; row++) {
        uint8_t bits = glyph[row];
        for (uint32_t col = 0; col < 8; col++) {
            // 왼쪽 끝 비트(0x80)부터 하나씩 검사해서 1이면 픽셀 찍기
            if (bits & (0x80 >> col)) {
                if ((x + col) < g_fb->width && (y + row) < g_fb->height) {
                    fb_ptr[(y + row) * pitch_pixels + (x + col)] = fg_color;
                }
            }
        }
    }
}

static void fb_putchar(char c, uint32_t fg_color) {
    if (!g_fb) return;
    PSF1_Header *font = (PSF1_Header *)&_binary_font_psf_start;

    if (c == '\n') {
        cursor_x = 16;
        cursor_y += font->characterSize + 4; // 줄간격 4px 여유
        return;
    }
    if (c == '\r') {
        cursor_x = 16;
        return;
    }

    fb_draw_char(c, cursor_x, cursor_y, fg_color);
    cursor_x += 8; // PSF1은 가로 무조건 8px!

    // 화면 오른쪽 끝에 닿으면 자동 줄바꿈
    if (cursor_x + 8 >= g_fb->width - 16) {
        cursor_x = 16;
        cursor_y += font->characterSize + 4;
    }
}

static void fb_print_num(uint64_t num, int base, bool is_signed, uint32_t fg) {
    char buf[32];
    const char *digits = "0123456789abcdef";
    int i = 0;

    if (is_signed && (int64_t)num < 0) {
        fb_putchar('-', fg);
        num = -(int64_t)num;
    }
    if (num == 0) {
        fb_putchar('0', fg);
        return;
    }
    while (num > 0) {
        buf[i++] = digits[num % base];
        num /= base;
    }
    while (i > 0) {
        fb_putchar(buf[--i], fg);
    }
}

void kprintf(const char *fmt, ...) {
    uint32_t white = fb_pixel(g_fb, 255, 255, 255);
    va_list args;
    va_start(args, fmt);

    for (size_t i = 0; fmt[i] != '\0'; i++) {
        if (fmt[i] != '%') {
            fb_putchar(fmt[i], white);
            continue;
        }
        i++;
        switch (fmt[i]) {
            case 's': {
                const char *s = va_arg(args, const char *);
                if (!s) s = "(null)";
                for (size_t j = 0; s[j] != '\0'; j++) fb_putchar(s[j], white);
                break;
            }
            case 'c': {
                fb_putchar((char)va_arg(args, int), white);
                break;
            }
            case 'd': {
                fb_print_num((uint64_t)va_arg(args, int64_t), 10, true, white);
                break;
            }
            case 'x': {
                fb_putchar('0', white);
                fb_putchar('x', white);
                fb_print_num(va_arg(args, uint64_t), 16, false, white);
                break;
            }
            default:
                fb_putchar(fmt[i], white);
                break;
        }
    }
    va_end(args);
}

static inline void outb(uint16_t port, uint8_t val) {
    asm volatile ( "outb %b0, %w1" : : "a"(val), "Nd"(port) : "memory" );
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm volatile ( "inb %w1, %b0" : "=a"(ret) : "Nd"(port) : "memory" );
    return ret;
}

static int init_serial() {
   outb(PORT + 1, 0x00);    // Disable all interrupts
   outb(PORT + 3, 0x80);    // Enable DLAB (set baud rate divisor)
   outb(PORT + 0, 0x03);    // Set divisor to 3 (lo byte) 38400 baud
   outb(PORT + 1, 0x00);    //                  (hi byte)
   outb(PORT + 3, 0x03);    // 8 bits, no parity, one stop bit
   outb(PORT + 2, 0xC7);    // Enable FIFO, clear them, with 14-byte threshold
   outb(PORT + 4, 0x0B);    // IRQs enabled, RTS/DSR set
   outb(PORT + 4, 0x1E);    // Set in loopback mode, test the serial chip
   outb(PORT + 0, 0xAE);    // Test serial chip (send byte 0xAE and check if serial returns same byte)

   // Check if serial is faulty (i.e: not same byte as sent)
   if(inb(PORT + 0) != 0xAE) {
      return 1;
   }

   // If serial is not faulty set it in normal operation mode
   // (not-loopback with IRQs enabled and OUT#1 and OUT#2 bits enabled)
   outb(PORT + 4, 0x0F);
   return 0;
}

int serial_received() {
   return inb(PORT + 5) & 1;
}

char read_serial() {
   while (serial_received() == 0);

   return inb(PORT);
}

int is_transmit_empty() {
   return inb(PORT + 5) & 0x20;
}

void write_serial(char a) {
   while (is_transmit_empty() == 0);

   outb(PORT,a);
}

void serial_print(const char *str) {
    for (size_t i = 0; str[i] != '\0'; i++) {
        write_serial(str[i]);
    }
}

static void serial_print_num(uint64_t num, int base, bool is_signed) {
    char buf[32];
    const char *digits = "0123456789abcdef";
    int i = 0;

    if (is_signed && (int64_t)num < 0) {
        write_serial('-');
        num = -(int64_t)num;
    }

    if (num == 0) {
        write_serial('0');
        return;
    }

    while (num > 0) {
        buf[i++] = digits[num % base];
        num /= base;
    }

    while (i > 0) {
        write_serial(buf[--i]);
    }
}

void serial_printf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);

    for (size_t i = 0; fmt[i] != '\0'; i++) {
        if (fmt[i] != '%') {
            write_serial(fmt[i]);
            continue;
        }

        i++; // '%' 다음 글자 확인
        switch (fmt[i]) {
            case 's': {
                const char *s = va_arg(args, const char *);
                serial_print(s ? s : "(null)");
                break;
            }
            case 'c': {
                char c = (char)va_arg(args, int);
                write_serial(c);
                break;
            }
            case 'd': {
                int64_t val = va_arg(args, int64_t);
                serial_print_num((uint64_t)val, 10, true);
                break;
            }
            case 'x': {
                uint64_t val = va_arg(args, uint64_t);
                serial_print("0x");
                serial_print_num(val, 16, false);
                break;
            }
            case '%': {
                write_serial('%');
                break;
            }
            default: {
                write_serial('%');
                write_serial(fmt[i]);
                break;
            }
        }
    }

    va_end(args);
}

void exception_handler(registers_t *regs) {
    const char *msg = (regs->int_no < 32) ? exception_messages[regs->int_no] : "Unknown Exception";

    serial_printf("\r\n[KERNEL PANIC] Exception #%d: %s (err: %x, RIP: %x)\r\n",
                  regs->int_no, msg, regs->err_code, regs->rip);
    kprintf("\n[KERNEL PANIC] Exception #%d: %s!\n", regs->int_no, msg);
    kprintf("Error Code: %x | RIP: %x\n", regs->err_code, regs->rip);

    hcf(); // 안전하게 시스템 정지
}

// TODO: 시리얼 포트 가능하게 수정해라 블슈야
// ㄴ 했어
// The following will be our kernel's entry point.
// If renaming kmain() to something else, make sure to change the
// linker script accordingly.
void kmain(void) {
    // Ensure the bootloader actually understands our base revision (see spec).
    if (LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision) == false) {
        hcf();
    }

    // Ensure we got a framebuffer.
    if (framebuffer_request.response == NULL
     || framebuffer_request.response->framebuffer_count < 1) {
        hcf();
    }

    // Print the pattern to every framebuffer.
    for (uint64_t i = 0; i < framebuffer_request.response->framebuffer_count; i++) {
        struct limine_framebuffer *framebuffer = framebuffer_request.response->framebuffers[i];
        uint64_t hhdm_offset = hhdm_request.response->offset;
        // Ensure the framebuffer has 32-bit RGB pixels, the only kind we handle.
        if (framebuffer->memory_model != LIMINE_FRAMEBUFFER_RGB || framebuffer->bpp != 32) {
            hcf();
        }
        init_serial();
        serial_printf("Serial Port initialized!!\r\n");
        init_gdt();
        init_idt();
        serial_printf("GDT & IDT initialized!!\r\n");
        g_fb = framebuffer;
        fb_pattern(framebuffer);
        pmm_init(memmap_request.response, hhdm_offset);
        serial_printf("PMM initialized!! (Total pages: %d)\n", npages);

        // 2. 페이지 프레임 2개 할당 테스트 (4KB = 0x1000 차이로 나오는지 확인!)
        pageframe_t p1 = kalloc_frame();
        pageframe_t p2 = kalloc_frame();
        kprintf("Alloc 1: %x | Alloc 2: %x\n", p1, p2);
        serial_printf("PMM Test -> p1: %x, p2: %x\r\n", p1, p2);

        // 3. 실제 할당받은 물리 메모리에 HHDM을 통해 데이터 써보기! (진짜 쓸 수 있는 땅인지 검증!)
        uint64_t *test_ptr = (uint64_t *)(p1 + hhdm_offset);
        *test_ptr = 0xDEADBEEFCAFEBABE;
        kprintf("Write/Read test at %x: %x\n", p1, *test_ptr);

        // 4. 반납 테스트
        kfree_frame(p1);
        kfree_frame(p2);
        kprintf("Frames freed successfully!\n");
        serial_printf("Welcome to %s!\r\n", "Sphinx");
        serial_printf("Framebuffer: %dx%d (addr: %x)\r\n",
                    (uint64_t)framebuffer->width,
                    (uint64_t)framebuffer->height,
                    (uint64_t)framebuffer->address);
        kprintf("Welcome to %s v0.1!\n", "Sphinx");
        kprintf("Resolution: %dx%d | FB Addr: %x\n",
            (uint64_t)framebuffer->width,
            (uint64_t)framebuffer->height,
            (uint64_t)framebuffer->address);
        // asm volatile ("int $0x0");    
        if (memmap_request.response == NULL || hhdm_request.response == NULL) {
            kprintf("[PANIC] Missing Memmap or HHDM response!\n");
            hcf();
        }
        uint64_t total_usable = 0;

        kprintf("HHDM Offset: %x\n", hhdm_offset);
        serial_printf("\r\n--- Physical Memory Map ---\r\n");

        for (uint64_t i = 0; i < memmap_request.response->entry_count; i++) {
            struct limine_memmap_entry *entry = memmap_request.response->entries[i];
            serial_printf("Entry %d: base=%x, len=%x, type=%d\r\n",
                        i, entry->base, entry->length, entry->type);

            if (entry->type == LIMINE_MEMMAP_USABLE) {
                total_usable += entry->length;
            }
        }

        kprintf("Usable RAM: %d MB (%d bytes)\n",
                total_usable / (1024 * 1024), total_usable);
    }

    // We're done, just hang...
    hcf();
}