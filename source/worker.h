// A second thread for slow work that would stall a frame: music streaming, decoding sounds,
// and writing saves to the SD card. It runs on the New 3DS's spare core when there is one.
#pragma once
void worker_start(void);
void worker_stop(void); // waits for it, then writes any save still queued
