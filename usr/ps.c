#include "ps.h"
#include <driver/ps2.h>
#include <driver/sd.h>
#include <driver/vga.h>
#include <page.h>
#include <zjunix/bootmm.h>
#include <zjunix/buddy.h>
#include <zjunix/fs/fat.h>
#include <zjunix/semaphore.h>
#include <zjunix/slab.h>
#include <zjunix/time.h>
#include <zjunix/utils.h>
#include <zjunix/vm.h>
#include <zjunix/vfs/vfs.h>
#include "../usr/ls.h"
#include "exec.h"
#include "myvi.h"

char ps_buffer[64];
int ps_buffer_index;

void test_proc() {
  unsigned int timestamp;
  unsigned int currTime;
  unsigned int data;
  asm volatile("mfc0 %0, $9, 6\n\t" : "=r"(timestamp));
  data = timestamp & 0xff;
  while (1) {
    asm volatile("mfc0 %0, $9, 6\n\t" : "=r"(currTime));
    if (currTime - timestamp > 100000000) {
      timestamp += 100000000;
      *((unsigned int *)0xbfc09018) = data;
    }
  }
}

int proc_demo_create() {
  task_create("demo_test_proc", test_proc, 0, 0, 0, 0);
  return 0;
}

void empty_test() {
  int i = 0;
  while (1) {
  }
}

void create_test_prog() {
  task_create("1111", empty_test, 0, 0, 5, 1);
  //   task_create("2222", empty_test, 0, 0, 0, 1);
  //   task_create("3333", empty_test, 0, 0, 20, 1);
  //   task_create("4444", empty_test, 0, 0, -5, 1);
  //   task_create("5555", empty_test, 0, 0, 10, 1);
}

void get_a_str(char *a, char **p) {
  while (**p == ' ') (*p)++;
  int i;
  for (i = 0; **p != 0 && **p != ' '; (*p)++) a[i++] = **p;
  a[i] = '\0';
}

void get_num(int *num, char **p) {
  for (*num = 0; **p != 0 && **p != ' '; (*p)++)
    *num = (*num) * 10 + (**p) - '0';
}

void ps() {
  kernel_printf("Press any key to enter shell.\n");
  kernel_getchar();
  char c;
  ps_buffer_index = 0;
  ps_buffer[0] = 0;
  kernel_clear_screen(31);
  kernel_puts("PowerShell\n", 0xfff, 0);
  kernel_puts("PS>", 0xfff, 0);
  while (1) {
    c = kernel_getchar();
    if (c == '\n') {
      ps_buffer[ps_buffer_index] = 0;
      if (kernel_strcmp(ps_buffer, "exit") == 0) {
        ps_buffer_index = 0;
        ps_buffer[0] = 0;
        kernel_printf("\nPowerShell exit.\n");
      } else
        parse_cmd();
      ps_buffer_index = 0;
      kernel_puts("PS>", 0xfff, 0);
    } else if (c == 0x08) {
      if (ps_buffer_index) {
        ps_buffer_index--;
        kernel_putchar_at(' ', 0xfff, 0, cursor_row, cursor_col - 1);
        cursor_col--;
        kernel_set_cursor();
      }
    } else {
      if (ps_buffer_index < 63) {
        ps_buffer[ps_buffer_index++] = c;
        kernel_putchar(c, 0xfff, 0);
      }
    }
  }
}

void parse_cmd() {
  unsigned int result = 0;
  char dir[32];
  char c;
  kernel_putchar('\n', 0, 0);
  char sd_buffer[8192];
  int i = 0;
  char *param;
  for (i = 0; i < 63; i++) {
    if (ps_buffer[i] == ' ') {
      ps_buffer[i] = 0;
      break;
    }
  }
  if (i == 63) {
    ps_buffer[62] = 0;
    param = ps_buffer + 62;
  } else {
    param = ps_buffer + i + 1;
  }
  if (ps_buffer[0] == 0) {
    return;
  } else if (kernel_strcmp(ps_buffer, "clear") == 0) {
    kernel_clear_screen(31);
  } else if (kernel_strcmp(ps_buffer, "echo") == 0) {
    kernel_printf("%s\n", param);
  } else if (kernel_strcmp(ps_buffer, "gettime") == 0) {
    char buf[10];
    get_time(buf, sizeof(buf));
    kernel_printf("%s\n", buf);
  } else if (kernel_strcmp(ps_buffer, "sdwi") == 0) {
    for (i = 0; i < 512; i++) sd_buffer[i] = i;
    sd_write_block(sd_buffer, 7, 1);
    kernel_puts("sdwi\n", 0xfff, 0);
  } else if (kernel_strcmp(ps_buffer, "sdr") == 0) {
    sd_read_block(sd_buffer, 7, 1);
    for (i = 0; i < 512; i++) {
      kernel_printf("%d ", sd_buffer[i]);
    }
    kernel_putchar('\n', 0xfff, 0);
  } else if (kernel_strcmp(ps_buffer, "sdwz") == 0) {
    for (i = 0; i < 512; i++) {
      sd_buffer[i] = 0;
    }
    sd_write_block(sd_buffer, 7, 1);
    kernel_puts("sdwz\n", 0xfff, 0);
  } else if (kernel_strcmp(ps_buffer, "mminfo") == 0) {
    bootmap_info("bootmm");
    buddy_info();
  } else if (kernel_strcmp(ps_buffer, "mmtest") == 0) {
    kernel_printf("kmalloc : %x, size = 1KB\n", kmalloc(1024));
  } else if (kernel_strcmp(ps_buffer, "ps") == 0) {
    result = print_proc();
    kernel_printf("ps return with %d\n", result);
  } else if (kernel_strcmp(ps_buffer, "kill") == 0) {
    int pid = param[0] - '0';
    kernel_printf("Killing process %d\n", pid);
    task_kill(pid);
    kernel_printf("kill return with %d\n", result);
  } else if (kernel_strcmp(ps_buffer, "wait") == 0) {
    int pid = param[0] - '0';
    kernel_printf("wait process %d\n", pid);
    task_wait(pid);
  } else if (kernel_strcmp(ps_buffer, "wake") == 0) {
    int pid = param[0] - '0';
    kernel_printf("wake up process %d\n", pid);
    task_wakeup(pid);
  } else if (kernel_strcmp(ps_buffer, "time") == 0) {
    task_create("time_proc", system_time_proc, 0, 0, 0, 0);
  } else if (kernel_strcmp(ps_buffer, "proc") == 0) {
    result = proc_demo_create();
    kernel_printf("proc return with %d\n", result);
  } else if (kernel_strcmp(ps_buffer, "test") == 0) {
    create_test_prog();
  } else if (kernel_strcmp(ps_buffer, "cat") == 0) {
    unsigned int old_ie = disable_interrupts();
    result = fs_cat(param);
    kernel_printf("cat return with %d\n", result);
    if (old_ie) {
      enable_interrupts();
    }
  } else if (kernel_strcmp(ps_buffer, "ls") == 0) {
    unsigned int old_ie = disable_interrupts();
    result = vfs_ls(param);
    if (old_ie) {
      enable_interrupts();
    }
  } else if (kernel_strcmp(ps_buffer, "cd") == 0) {
    unsigned int old_ie = disable_interrupts();
    result = vfs_cd(param);
    if (old_ie) {
      enable_interrupts();
    }
  } else if (kernel_strcmp(ps_buffer, "vi") == 0) {
    result = myvi(param);
    kernel_printf("vi return with %d\n", result);
  } else if (kernel_strcmp(ps_buffer, "exec") == 0) {
    char filename[20];
    get_a_str(filename, &param);
    result = task_exec_from_file(filename);
    kernel_printf("exec return with %d\n", result);
  } else if (kernel_strcmp(ps_buffer, "pgtable") == 0) {
    print_pgtable();
  } else if (kernel_strcmp(ps_buffer, "vma") == 0) {
    task_create("vma_proc", vma_proc, 0, 0, 0, 1);
  } else if (kernel_strcmp(ps_buffer, "pageshare") == 0) {
    task_create("page_share_proc_1", page_share_proc_1, 0, 0, 0, 1);
    sleep(1000 * 1000 * 10);
    task_create("page_share_proc_2", page_share_proc_2, 0, 0, 0, 1);
  } else if (kernel_strcmp(ps_buffer, "buffer") == 0) {
    unsigned int init_gp;
    asm volatile("la %0, _gp\n\t" : "=r"(init_gp));
    task_create("buffer_proc", buffer_proc, 0, 0, 0, 1);
  } else if (kernel_strcmp(ps_buffer, "customer") == 0) {
    unsigned int init_gp;
    asm volatile("la %0, _gp\n\t" : "=r"(init_gp));
    task_create("customer_proc", customer_proc, 0, 0, 0, 1);
  } else if (kernel_strcmp(ps_buffer, "producer") == 0) {
    unsigned int init_gp;
    asm volatile("la %0, _gp\n\t" : "=r"(init_gp));
    task_create("producer_proc", producer_proc, 0, 0, 0, 1);
  } else {
    kernel_puts(ps_buffer, 0xfff, 0);
    kernel_puts(": command not found\n", 0xfff, 0);
  }
}
