#include "main.h"
#include <stddef.h>
#include <sys/stat.h>
#include <errno.h>
extern UART_HandleTypeDef huart1;
extern char _end, _heap_limit;
int _write(int fd, char *data, int length) {
    (void)fd;
    return HAL_UART_Transmit(&huart1, (uint8_t *)data, length, 1000) == HAL_OK ? length : -1;
}
void *_sbrk(ptrdiff_t count) {
    static char *next;
    if (!next) next = &_end;
    if (count < 0 || count > &_heap_limit - next) { errno = ENOMEM; return (void *)-1; }
    char *previous = next; next += count; return previous;
}
void _init(void) {}
void _fini(void) {}
