// magicmousefix.c — re-enables multi-touch mode on an Apple Magic Mouse under macOS 27.
//
// The bug: on macOS 27 the AppleMultitouchMouseHIDEventDriver never sends the feature
// report that switches the mouse out of boot-mouse mode into multi-touch mode. The mouse
// pairs, connects and reports battery level, but delivers no motion, clicks or gestures.
// macOS 26 sent that report. The Linux hid-magicmouse driver sends the same one.
//
// This program sends the report on the driver's behalf. It does not read input, write
// files, or make network calls.
//
// Build:
//   clang -O2 -Wall -o magicmousefix magicmousefix.c -framework IOKit -framework CoreFoundation
//
// Modes:
//   ./magicmousefix                  send the report once and exit
//   ./magicmousefix --agent          stay resident, send the report every time a
//                                    Magic Mouse connects (used by the LaunchDaemon)
//   ./magicmousefix --agent --heartbeat 60
//                                    same, plus a repeat every 60 seconds (only needed
//                                    if the mouse goes silent again after sleep)
//
// Running as your own user requires Input Monitoring permission for the calling app
// (Terminal). Running as root — via sudo or the LaunchDaemon — does not.
//
// SPDX-License-Identifier: MIT

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hid/IOHIDManager.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Feature report that enables multi-touch. Byte 0 is the report ID.
static const uint8_t kEnableMultitouch[] = { 0xF1, 0x06, 0x01, 0x37 };

// The driver is still settling when the device shows up, so send a short burst.
#define BURST_COUNT     5
#define BURST_INTERVAL  1.5

static IOHIDManagerRef   g_manager     = NULL;
static int               g_burst_left  = 0;
static CFRunLoopTimerRef g_burst_timer = NULL;

static int is_magic_mouse(IOHIDDeviceRef dev) {
    CFTypeRef prop = IOHIDDeviceGetProperty(dev, CFSTR(kIOHIDProductKey));
    if (!prop || CFGetTypeID(prop) != CFStringGetTypeID()) return 0;

    char name[256];
    if (!CFStringGetCString((CFStringRef)prop, name, sizeof(name), kCFStringEncodingUTF8))
        return 0;
    return strstr(name, "Magic Mouse") != NULL;
}

static void log_line(const char *fmt, ...) {
    char stamp[32];
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &tm_now);

    va_list ap;
    va_start(ap, fmt);
    printf("%s ", stamp);
    vprintf(fmt, ap);
    printf("\n");
    va_end(ap);
    fflush(stdout);
}

// Sends the report to every Magic Mouse HID interface currently present.
// Returns the number of interfaces that accepted it.
static int send_to_all(void) {
    CFSetRef devices = IOHIDManagerCopyDevices(g_manager);
    if (!devices) return 0;

    CFIndex count = CFSetGetCount(devices);
    IOHIDDeviceRef *list = calloc((size_t)count, sizeof(IOHIDDeviceRef));
    if (!list) { CFRelease(devices); return 0; }
    CFSetGetValues(devices, (const void **)list);

    int found = 0, sent = 0;
    for (CFIndex i = 0; i < count; i++) {
        if (!is_magic_mouse(list[i])) continue;
        found++;
        IOReturn r = IOHIDDeviceSetReport(list[i],
                                          kIOHIDReportTypeFeature,
                                          kEnableMultitouch[0],
                                          kEnableMultitouch,
                                          (CFIndex)sizeof(kEnableMultitouch));
        if (r == kIOReturnSuccess) sent++;
    }

    free(list);
    CFRelease(devices);

    if (found) log_line("Magic Mouse: %d interface(s), %d accepted the report", found, sent);
    return sent;
}

static void burst_tick(CFRunLoopTimerRef timer, void *info) {
    (void)info;
    send_to_all();
    if (--g_burst_left <= 0) {
        CFRunLoopTimerInvalidate(timer);
        CFRelease(g_burst_timer);
        g_burst_timer = NULL;
    }
}

static void start_burst(void) {
    send_to_all();

    if (g_burst_timer) {              // a burst is already running — just extend it
        g_burst_left = BURST_COUNT;
        return;
    }
    g_burst_left = BURST_COUNT;
    g_burst_timer = CFRunLoopTimerCreate(kCFAllocatorDefault,
                                         CFAbsoluteTimeGetCurrent() + BURST_INTERVAL,
                                         BURST_INTERVAL, 0, 0,
                                         burst_tick, NULL);
    CFRunLoopAddTimer(CFRunLoopGetCurrent(), g_burst_timer, kCFRunLoopDefaultMode);
}

static void on_device_matched(void *ctx, IOReturn res, void *sender, IOHIDDeviceRef dev) {
    (void)ctx; (void)sender;
    if (res != kIOReturnSuccess || !is_magic_mouse(dev)) return;
    log_line("Magic Mouse appeared — sending report");
    start_burst();
}

static void heartbeat_tick(CFRunLoopTimerRef timer, void *info) {
    (void)timer; (void)info;
    send_to_all();
}

static int open_manager(void) {
    g_manager = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
    if (!g_manager) {
        fprintf(stderr, "IOHIDManagerCreate failed\n");
        return 0;
    }
    IOHIDManagerSetDeviceMatching(g_manager, NULL);   // every HID device

    IOReturn opened = IOHIDManagerOpen(g_manager, kIOHIDOptionsTypeNone);
    if (opened != kIOReturnSuccess) {
        fprintf(stderr,
                "IOHIDManagerOpen failed: 0x%08x\n"
                "Not enough privileges: grant Terminal the Input Monitoring permission, "
                "or run with sudo.\n",
                opened);
        return 0;
    }
    return 1;
}

int main(int argc, char **argv) {
    int    agent     = 0;
    double heartbeat = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--agent") == 0) {
            agent = 1;
        } else if (strcmp(argv[i], "--heartbeat") == 0 && i + 1 < argc) {
            heartbeat = atof(argv[++i]);
        } else {
            fprintf(stderr, "usage: %s [--agent] [--heartbeat SECONDS]\n", argv[0]);
            return 2;
        }
    }

    if (!open_manager()) return 1;

    if (!agent) {
        int sent = send_to_all();
        if (!sent) fprintf(stderr, "No Magic Mouse interface accepted the report.\n");
        IOHIDManagerClose(g_manager, kIOHIDOptionsTypeNone);
        CFRelease(g_manager);
        return sent ? 0 : 1;
    }

    log_line("agent started");

    IOHIDManagerRegisterDeviceMatchingCallback(g_manager, on_device_matched, NULL);
    IOHIDManagerScheduleWithRunLoop(g_manager, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);

    start_burst();                    // the mouse may already be connected

    if (heartbeat > 0) {
        CFRunLoopTimerRef hb = CFRunLoopTimerCreate(kCFAllocatorDefault,
                                                    CFAbsoluteTimeGetCurrent() + heartbeat,
                                                    heartbeat, 0, 0,
                                                    heartbeat_tick, NULL);
        CFRunLoopAddTimer(CFRunLoopGetCurrent(), hb, kCFRunLoopDefaultMode);
        log_line("heartbeat every %.0f s", heartbeat);
    }

    CFRunLoopRun();                   // does not return
    return 0;
}
