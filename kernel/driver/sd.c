#include "sd.h"
#include <driver/vga.h>
#include <intr.h>
#include <zjunix/type.h>

#pragma GCC push_opitons
#pragma GCC optimize("O0")

static volatile unsigned int* const SD_CTRL = (unsigned int*)0xbfc09100;
static volatile unsigned int* const SD_BUF = (unsigned int*)0xbfc08000;

static int sd_send_cmd_blocking(int cmd, int argument) {
    // Send cmd
    SD_CTRL[1] = cmd;
    SD_CTRL[0] = argument;
    // Wait for cmd transmission
    int t = 4096;
    while (t--)
        ;

    // Read CMD_EVENT_STATUS
    int cmd_event_status = 0;
    do {
        cmd_event_status = SD_CTRL[13];
    } while (cmd_event_status == 0);

    // Check if sending success
    if (cmd_event_status & 1) {
        // success
        return 0;
    } else {
        // fail
        return cmd_event_status;
    }
}

int sd_read_sector_blocking(int id, void* buffer) {
    // Disable interrupts
    disable_interrupts();
    int result = 0;

    // Set dma_address
    SD_CTRL[24] = 0;
    // Clear data_event_status
    SD_CTRL[15] = 0;
    // Tell sd ready to read
    result = sd_send_cmd_blocking(0x1139, id);
    if (result != 0) {
        goto ret;
    }

    // Read data_event_status
    int des = 0;
    do {
        des = SD_CTRL[15];
    } while (des == 0);

    if (des & 1) {
        // Start reading
        int* buffer_int = (int*)buffer;
        for (int i = 0; i < 128; i++) {
            buffer_int[i] = SD_BUF[i];
        }
        result = 0;
    } else {
        // Error encountered
        result = des;
    }
ret:
    // Enable interrupts
    enable_interrupts();
    return result;
}

int sd_write_sector_blocking(int id, void* buffer) {
    // Disable interrupts
    disable_interrupts();
    int result = 0;

    // Set dma_address
    SD_CTRL[24] = 0;
    // Clear data_event_status
    SD_CTRL[15] = 0;
    // Wait bus until clear
    asm volatile(
        "nop\n\t"
        "nop\n\t");

    // Start writing
    int* buffer_int = (int*)buffer;
    for (int i = 0; i < 128; i++) {
        SD_BUF[i] = buffer_int[i];
    }
    // Tell sd ready to write
    result = sd_send_cmd_blocking(0x1859, id);
    if (result != 0) {
        goto ret;
    }

    // Read data_event_status
    int des = 0;
    do {
        des = SD_CTRL[15];
    } while (des == 0);
    
    if (des & 1) {
        result = 0;
    } else {
        result = des;
    }
ret:
    // Enable interrupts
    enable_interrupts();
    return result;
}

u32 sd_read_block(unsigned char* buf, unsigned long addr, unsigned long count) {
    // Read single/multiple block
    u32 result;
    for (int i = 0; i < count; ++i) {
        result = sd_read_sector_blocking(addr + i, buf + i * SECSIZE);
        if (0 != result) {
            goto error;
        }
    }
ok:
    return 0;
error:
    return 1;
}

u32 sd_write_block(unsigned char* buf, unsigned long addr, unsigned long count) {
    // Write single/multiple block
    u32 result;
    for (int i = 0; i < count; ++i) {
        result = sd_write_sector_blocking(addr + i, buf + i * SECSIZE);
        if (0 != result) {
            goto error;
        }
    }
ok:
    return 0;
error:
    return 1;
}

#pragma GCC pop_options