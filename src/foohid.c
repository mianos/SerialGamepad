/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <xythobuz@xythobuz.de> wrote this file.  As long as you retain this notice
 * you can do whatever you want with this stuff. If we meet some day, and you
 * think this stuff is worth it, you can buy me a beer in return.   Thomas Buck
 * ----------------------------------------------------------------------------
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>

#include <IOKit/IOKitLib.h>

#include "serial.h"

#define BAUDRATE 115200
#define PACKETSIZE 18
#define HEADERBYTES 2
#define HEADERBYTE_A 85
#define HEADERBYTE_B 252
#define CHECKSUMBYTES 2
#define PAYLOADBYTES (PACKETSIZE - HEADERBYTES - CHECKSUMBYTES)
#define CHANNELS 6
#define TESTCHANNEL 2
#define CHANNELMAXIMUM 1022


#define IBUS_HEADERBYTES 2
#define IBUS_HEADERBYTE_A 0x20
#define IBUS_HEADERBYTE_B 0x40
#define IBUS_CHANNELS 14


#define FOOHID_NAME "it_unbit_foohid"
#define FOOHID_CREATE 0
#define FOOHID_DESTROY 1
#define FOOHID_SEND 2
#define FOOHID_LIST 3
#define VIRTUAL_DEVICE_NAME "Virtual Serial Transmitter"
#define VIRTUAL_DEVICE_SERIAL "SN 123456"

struct gamepad_report_t {
    int16_t leftX;
    int16_t leftY;
    int16_t rightX;
    int16_t rightY;
    int16_t aux1;
    int16_t aux2;
};

static int running = 1;
static io_iterator_t iterator;
static io_service_t service;
static io_connect_t connect;
#define input_count 8
static uint64_t input[input_count];
static struct gamepad_report_t gamepad;

bool debug = false;
bool raw_ibus = false;

/*
 * This is my USB HID Descriptor for this emulated Gamepad.
 * For more informations refer to:
 * http://eleccelerator.com/tutorial-about-usb-hid-report-descriptors/
 * http://www.usb.org/developers/hidpage#HID%20Descriptor%20Tool
 */
static char report_descriptor[36] = {
    0x05, 0x01,                    // USAGE_PAGE (Generic Desktop)
    0x09, 0x05,                    // USAGE (Game Pad)
    0xa1, 0x01,                    // COLLECTION (Application)
    0xa1, 0x00,                    //   COLLECTION (Physical)
    0x05, 0x01,                    //     USAGE_PAGE (Generic Desktop)
    0x09, 0x30,                    //     USAGE (X)
    0x09, 0x31,                    //     USAGE (Y)
    0x09, 0x32,                    //     USAGE (Z)
    0x09, 0x33,                    //     USAGE (Rx)
    0x09, 0x34,                    //     USAGE (Ry)
    0x09, 0x35,                    //     USAGE (Rz)
    0x16, 0x01, 0xfe,              //     LOGICAL_MINIMUM (-511)
    0x26, 0xff, 0x01,              //     LOGICAL_MAXIMUM (511)
    0x75, 0x10,                    //     REPORT_SIZE (16)
    0x95, 0x06,                    //     REPORT_COUNT (6)
    0x81, 0x02,                    //     INPUT (Data,Var,Abs)
    0xc0,                          //     END_COLLECTION
    0xc0                           // END_COLLECTION
};

static int foohidInit() {
    printf("Searching for foohid Kernel extension...\n");

    // get a reference to the IOService
    kern_return_t ret = IOServiceGetMatchingServices(kIOMasterPortDefault,
                            IOServiceMatching(FOOHID_NAME), &iterator);
    if (ret != KERN_SUCCESS) {
        printf("Unable to access foohid IOService\n");
        exit(1);
    }

    int found = 0;
    while ((service = IOIteratorNext(iterator)) != IO_OBJECT_NULL) {
        ret = IOServiceOpen(service, mach_task_self(), 0, &connect);
        if (ret == KERN_SUCCESS) {
            found = 1;
            break;
        }
    }
    IOObjectRelease(iterator);
    if (!found) {
        printf("Unable to open foohid IOService\n");
        exit(1);
    }

    printf("Creating virtual HID device...\n");

    input[0] = (uint64_t)strdup(VIRTUAL_DEVICE_NAME);
    input[1] = strlen((char*)input[0]);

    input[2] = (uint64_t)report_descriptor;
    input[3] = sizeof(report_descriptor);

    input[4] = (uint64_t)strdup(VIRTUAL_DEVICE_SERIAL);
    input[5] = strlen((char*)input[4]);

    input[6] = (uint64_t)2; // vendor ID
    input[7] = (uint64_t)3; // device ID

    ret = IOConnectCallScalarMethod(connect, FOOHID_CREATE, input, input_count, NULL, 0);
    if (ret != KERN_SUCCESS) {
        printf("Unable to create virtual HID device\n");
        exit(1);
    }

    return 0;
}

static void foohidClose() {
    printf("Destroying virtual HID device\n");

    kern_return_t ret = IOConnectCallScalarMethod(connect, FOOHID_DESTROY, input, 2, NULL, 0);
    if (ret != KERN_SUCCESS) {
        printf("Unable to destroy virtual HID device\n");
    }
}

static void foohidSend(uint16_t *data, int channels, bool raw_ibus) {
    if (raw_ibus) {
        //values go from 1000 - 2000
        gamepad.leftX = data[3] - 1500;
        gamepad.leftY = data[2] - 1500;
        gamepad.rightX = data[0] - 1500;
        gamepad.rightY = data[1] - 1500;
        gamepad.aux1 = data[4] - 1500;
        gamepad.aux2 = data[5] - 1500;
    } else {
        for (int i = 0; i < channels; i++) {
            if (data[i] > CHANNELMAXIMUM) {
                data[i] = CHANNELMAXIMUM;
            }
        }
        gamepad.leftX = data[3] - 511;
        gamepad.leftY = data[2] - 511;
        gamepad.rightX = data[0] - 511;
        gamepad.rightY = data[1] - 511;
        gamepad.aux1 = data[4] - 511;
        gamepad.aux2 = data[5] - 511;
    }

    if (!debug) {
        input[2] = (uint64_t)&gamepad;
        input[3] = sizeof(struct gamepad_report_t);
        kern_return_t ret = IOConnectCallScalarMethod(connect, FOOHID_SEND, input, 4, NULL, 0);
        if (ret != KERN_SUCCESS) {
            fprintf(stderr, "Unable to send packet to virtual HID device\n");
        }
    } else {
        printf("Left X: %4d ", gamepad.leftX);
        printf("Left Y: %4d ", gamepad.leftY);
        printf("Right X: %4d ", gamepad.rightX);
        printf("Right Y: %4d ", gamepad.rightY);
        printf("Aux 1: %4d ", gamepad.aux1);
        printf("Aux 2: %4d\n", gamepad.aux2);
    }
}

static void signalHandler(int signo) {
    running = 0;
    printf("\n");
}

int main(int argc, char* argv[]) {
    char *serial_port = NULL;

    int opt;

    while ((opt = getopt(argc, argv, "p:di")) != EOF) {
        switch (opt) {
        case 'p':
            serial_port = optarg;
            break;
        case 'd':
            debug = true;
            break;
        case 'i':
            raw_ibus = true;
            break;
        }
    }
    if (serial_port == NULL) {
        fprintf(stderr, "Serial port -p <port> must be specified\n");
        exit(1);
    }

    printf("Opening serial port...\n");

    int fd = serialOpen(serial_port, BAUDRATE);
    if (fd == -1) {
        fprintf(stderr, "failed to open serial port\n");
        exit(1);
    }
 
    if (!debug) {
        if (foohidInit() != 0) {
            serialClose(fd);
            fprintf(stderr, "failed to init foohid\n");
            exit(1);
        }
    } else {
        printf("Debug mode, no driver\n");
    }
    if (signal(SIGINT, signalHandler) == SIG_ERR) {
        perror("Couldn't register signal handler");
        return 1;
    }

    printf("Entering main-loop...\n");

    if (raw_ibus) {
        enum {HDR_A, HDR_B, INT_LO, INT_HI, CHECK_A, CHECK_B} state = HDR_A;
        uint16_t vals[IBUS_CHANNELS];
        int channel = 0;
        unsigned int chksum;
        unsigned int wire_checksum;

        const int buffer_size = 1000;
        unsigned char buffer[buffer_size];

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
                        foohidSend(vals, IBUS_CHANNELS, raw_ibus);
                    } else {
                        printf("bad checksum wire checksump %04x, checksum %04x\n", wire_checksum, chksum);
                    }
                    state = HDR_A;
                    chksum = 0xFFFF;
                    break;
                }
            }
        }
    } else {
        while (running != 0) {
            if (serialHasChar(fd, 0)) {
                unsigned char c1;
                serialReadChar(fd, (char*)&c1);
                if (c1 == HEADERBYTE_A) {
                    // Found first byte of protocol start
                    while (!serialHasChar(fd, 0)) {
                        if (running == 0) {
                            serialClose(fd);
                            return 0;
                        }
                    }

                    unsigned char c2;
                    serialReadChar(fd, (char*)&c2);
                    if (c2 == HEADERBYTE_B) {
                        // Protocol start has been found, read payload
                        unsigned char data[PAYLOADBYTES];
                        int read = 0;
                        while ((read < PAYLOADBYTES) && (running != 0)) {
                            read += serialReadRaw(fd, (char*)data + read, PAYLOADBYTES - read);
                        }

                        // Read 16bit checksum
                        unsigned char checksumData[CHECKSUMBYTES];
                        read = 0;
                        while ((read < CHECKSUMBYTES) && (running != 0)) {
                            read += serialReadRaw(fd, (char*)checksumData + read,
                                    CHECKSUMBYTES - read);
                        }

                        // Check if checksum matches
                        uint16_t checksum = 0;
                        for (int i = 0; i < PAYLOADBYTES; i++) {
                            checksum += data[i];
                        }

                        if (checksum != ((checksumData[0] << 8) | checksumData[1])) {
                            printf("Wrong checksum: %d != %d\n",
                                    checksum, ((checksumData[0] << 8) | checksumData[1]));
                        } else {
                            // Decode channel values
                            uint16_t buff[CHANNELS + 1];
                            for (int i = 0; i < (CHANNELS + 1); i++) {
                                buff[i] = data[2 * i] << 8;
                                buff[i] |= data[(2 * i) + 1];

                                if (i < CHANNELS) {
                                    buff[i] -= 1000;
                                }
                            }

                            // Check Test Channel Value
                            if (buff[CHANNELS] != buff[TESTCHANNEL]) {
                                printf("Wrong test channel value: %d != %d\n",
                                       buff[CHANNELS], buff[TESTCHANNEL]);
                            }

                            foohidSend(buff, CHANNELS, raw_ibus);
                        }
                    }
                }
            }
            usleep(1000);
        } 
    }

    printf("Closing serial port...\n");
    serialClose(fd);
    if (!debug) {
        foohidClose();
    }

    return 0;
}

