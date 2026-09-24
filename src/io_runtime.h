#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
// FILE* is opaque at the ABI. Standard selector: 1=stdin, 2=stdout, 3=stderr.
void *gloin_io_standard(int32_t which);
// Open mode: 0=read, 1=exclusive create, 2=truncate/create, 3=append/create.
// Counts validate the full path before a temporary NUL-terminated native copy.
// Owned streams are unbuffered; native stream storage is released by close.
void *gloin_io_open(const char *path, uint64_t length, int32_t mode, int32_t *status,
                    int32_t *os_error);
// Read mode: 0=line, 1=chunk, 2=all. Buffer holds capacity+1 bytes.
// Line failure clears bytes; chunk/all preserve progress even on I/O failure.
// read_all TOO_LONG preserves capacity bytes and pushes back the excess probe.
int32_t gloin_io_read(void *stream, char *bytes, uint64_t capacity, int32_t mode, uint64_t *length,
                      int32_t *os_error);
// all=0 makes one fwrite; all=1 repeats short successful writes. No implicit flush.
int32_t gloin_io_write(void *stream, const char *bytes, uint64_t length, int32_t all,
                       uint64_t *written, int32_t *os_error);
int32_t gloin_io_flush(void *stream, int32_t *os_error);
// Consumes an owned stream even on failure. Never retry the native pointer.
int32_t gloin_io_close(void *stream, int32_t *os_error);
// Destination has 256 bytes; returns bounded localized OS text without Gloin allocation.
int32_t gloin_io_error_message(int32_t code, char *bytes, uint64_t *length);
#ifdef __cplusplus
}
#endif
