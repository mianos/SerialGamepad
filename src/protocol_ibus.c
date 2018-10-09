/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <xythobuz@xythobuz.de> wrote this file.  As long as you retain this notice
 * you can do whatever you want with this stuff. If we meet some day, and you
 * think this stuff is worth it, you can buy me a beer in return.   Thomas Buck
 * ----------------------------------------------------------------------------
 *
 *
 *
 *  This file (c) Rob Fowler rfo@mianos.com
 */

#include <stdint.h>
#include <stdio.h>
#include <signal.h>
#include <unistd.h>

#include "serial.h"

#define BAUDRATE 115200
#define IBUS_HEADERBYTES 2
#define IBUS_HEADERBYTE_A 0x20
#define IBUS_HEADERBYTE_B 0x40
#define IBUS_CHANNELS 14

static int running = 1;

static void signalHandler(int signo) {
    running = 0;
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        printf("Usage:\n\t%s /dev/serial_port\n", argv[0]);
        return 1;
    }
    printf("Opening serial port...\n");
    int fd = serialOpen(argv[1], BAUDRATE);
    if (fd == -1) {
        return 1;
    }
    printf("port open ...\n");
    if (signal(SIGINT, signalHandler) == SIG_ERR) {
        perror("Couldn't register signal handler");
        return 1;
    }

    enum {HDR_A, HDR_B, INT_LO, INT_HI, CHECK_A, CHECK_B} state = HDR_A;
    int vals[IBUS_CHANNELS];
    int lasts[IBUS_CHANNELS];
    int channel = 0;
    unsigned int chksum;
    unsigned int wire_checksum;

    const int buffer_size = 1000;
    unsigned char buffer[buffer_size];

    int wcount = 0;
    while (running != 0) {
        if (!serialHasChar(fd, 1)) {
            continue;
        }
        int bread = read(fd, buffer, buffer_size);

        for (int ii = 0; ii < bread; ii++) {
            unsigned char cc = buffer[ii];

            switch (state) {
            case HDR_A:
                if (cc == IBUS_HEADERBYTE_A) {
                    state = HDR_B;
                    chksum = 0xFFFF;
                    chksum -= cc;
                }
                break;
            case HDR_B:
                if (cc == IBUS_HEADERBYTE_B) {
                    state = INT_LO;
                    chksum -= cc;
                } else {
                    state = HDR_A;
                }
                channel = 0;
                break;
            case INT_LO:
                vals[channel] = cc;
                chksum -= cc;
                state = INT_HI;
                break;
            case INT_HI:
                vals[channel++] |= ((int)cc) << 8;
                chksum -= cc;
                if (channel == IBUS_CHANNELS) {
                    state = CHECK_A;
                } else {
                    state = INT_LO;
                }
                break;
            case CHECK_A:
                wire_checksum = cc;
                state = CHECK_B;
                break;
            case CHECK_B:
                wire_checksum |= ((int)cc) << 8;
                if (wire_checksum == chksum) {
                    if (wcount++ > 10) {
                        for (int ii = 0; ii < IBUS_CHANNELS; ii++) {
                            printf("%8x ", vals[ii]);
                        }
                        printf("\n");
                        for (int ii = 0; ii < IBUS_CHANNELS; ii++) {
                            printf("%8d ", vals[ii] - lasts[ii]);
                            lasts[ii] = vals[ii];
                        }
                        printf("\n");
                        wcount = 0;
                    }
                } else {
                    printf("bad checksum wire checksump %04x, checksum %04x\n", wire_checksum, chksum);
                }
                state = HDR_A;
                chksum = 0xFFFF;
                break;
            }
        }
    }
    printf("Closing serial port...                    \n");
    serialClose(fd);
    return 0;
}

