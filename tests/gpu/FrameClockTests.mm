#include "host/PassiveSurfaceHost.h"

#import <AppKit/AppKit.h>

#include <cmath>
#include <iostream>

namespace {
struct ClockState {
    int count = 0;
    double last = 0;
    bool valid = true;
};

void tick(void *context, double timestamp) {
    auto &state = *static_cast<ClockState *>(context);
    state.valid = state.valid and [NSThread isMainThread] and std::isfinite(timestamp) and
                  timestamp > state.last;
    state.last = timestamp;
    ++state.count;
}

bool pumpUntil(ClockState &state, int count) {
    NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:2];
    while (state.count < count and deadline.timeIntervalSinceNow > 0) {
        [NSRunLoop.mainRunLoop runMode:NSDefaultRunLoopMode
                            beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.02]];
    }
    return state.count >= count and state.valid;
}
}

int main() {
    @autoreleasepool {
        [NSApplication sharedApplication];
        NSWindow *window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 160, 100)
                                                       styleMask:NSWindowStyleMaskBorderless
                                                         backing:NSBackingStoreBuffered
                                                           defer:NO];
        window.releasedWhenClosed = NO;
        [window orderFrontRegardless];
        prism::host::PassiveSurfaceHost host;
        if (not host.attach((__bridge void *)window.contentView, {0, 0, 160, 100})) {
            return 1;
        }
        for (const bool legacy : {false, true}) {
            ClockState state;
            if (not host.startFrameClock({tick, &state}, legacy) or not pumpUntil(state, 4)) {
                std::cerr << "Display clock failed: legacy=" << legacy << '\n';
                return 1;
            }
            host.stopFrameClock();
            const int stopped = state.count;
            [NSRunLoop.mainRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
            if (state.count != stopped) {
                return 1;
            }
            std::cout << (legacy ? "CVDisplayLink" : "CADisplayLink")
                      << " delivered monotonic main-thread target timestamps\n";
        }

        ClockState state;
        if (not host.startFrameClock({tick, &state}) or not pumpUntil(state, 2)) {
            return 1;
        }
        host.detach();
        const int stopped = state.count;
        [NSRunLoop.mainRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
        if (state.count != stopped or host.startFrameClock({tick, &state})) {
            return 1;
        }
        [window close];
    }
    return 0;
}
