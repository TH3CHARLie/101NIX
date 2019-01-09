#include "page.h"
#include <driver/vga.h>
#include <intr.h>
#include <zjunix/utils.h>
#include "arch.h"

#pragma GCC push_options
#pragma GCC optimize("O0")

void init_pgtable() {
    asm volatile(
        "mtc0 $zero, $2\n\t"
        "mtc0 $zero, $3\n\t"
        "mtc0 $zero, $5\n\t"
        "mtc0 $zero, $6\n\t"
        "mtc0 $zero, $10\n\t"

        "move $v0, $zero\n\t"
        "li $v1, 32\n"

        "init_pgtable_L1:\n\t"
        "mtc0 $v0, $0\n\t"
        "addi $v0, $v0, 1\n\t"
        "tlbwi\n\t"
        "bne $v0, $v1, init_pgtable_L1\n\t"
        "nop");
}

void print_pgtable() {
    unsigned int original_entry_hi;
    int i;
    unsigned int old_ie;
    old_ie = disable_interrupts();
    asm volatile(
        "mfc0 %0, $10\n\t"
        "nop"
        : "=r"(original_entry_hi));
    for (i = 0; i < 32; i++) {
        unsigned int page_mask;
        unsigned int entry_hi;
        unsigned int entry_lo1;
        unsigned int entry_lo2;
        asm volatile(
            "mtc0 %4, $0\n\t"
            "nop\n\t"
            "tlbr\n\t"
            "nop\n\t"
            "mfc0 %0, $5\n\t"
            "mfc0 %1, $10\n\t"
            "mfc0 %2, $2\n\t"
            "mfc0 %3, $3\n\t"
            "nop"
            : "=r"(page_mask), "=r"(entry_hi), "=r"(entry_lo1), "=r"(entry_lo2)
            : "r"(i));
        kernel_printf(
            "index: %d, page mask:%x, entry_hi: %x, entry_lo1: %x, entry_lo2: "
            "%x\n",
            i, page_mask, entry_hi, entry_lo1, entry_lo2);
        sleep(1000 * 1000 * 3);
    }
    asm volatile(
        "mtc0 %0, $10\n\t"
        "nop"
        :
        : "r"(original_entry_hi));
    if (old_ie) {
        enable_interrupts();
    }
}

#pragma GCC pop_options