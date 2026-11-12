// A second thread for slow work that would stall a frame: music streaming, decoding sounds,
// writing saves to the SD card, and reading files ahead of time. It runs on the New 3DS's spare core
// when there is one.
#pragma once
#include <stdbool.h>
#include <stddef.h>

void worker_start(void);
void worker_stop(void); // waits for it, then writes any save still queued

// Reading ahead: prefetch() queues a whole file to be read into memory (a piece at a time, so the
// music keeps flowing). prefetch_take() hands the bytes over (free() them), waiting if they are not
// read yet; NULL when the file was never asked for or could not be read, so the caller reads it
// itself. prefetch_drop() frees whatever nobody took.
void prefetch(const char *path);
bool prefetch_ready(const char *path);
void *prefetch_take(const char *path, size_t *size);
void prefetch_drop(void);
