#include "../support/ReferenceScene.h"
#include "backends/metal/MetalBackend.h"
#include "host/HostComponent.h"

#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <iostream>

@interface PrismKeyWindow : NSWindow
@end

@implementation PrismKeyWindow
- (BOOL)canBecomeKeyWindow {
    return YES;
}
@end

@interface PrismEventRecorder : NSView
@property(nonatomic) int mouseDowns;
@property(nonatomic) int scrolls;
@property(nonatomic) int keyDowns;
@end

@implementation PrismEventRecorder
- (BOOL)acceptsFirstResponder {
    return YES;
}
- (void)mouseDown:(NSEvent *)event {
    ++self.mouseDowns;
}
- (void)scrollWheel:(NSEvent *)event {
    ++self.scrolls;
}
- (void)keyDown:(NSEvent *)event {
    ++self.keyDowns;
}
@end

namespace {
using namespace prism;

NSEvent *mouseDown(NSWindow *window, NSPoint point) {
    return [NSEvent mouseEventWithType:NSEventTypeLeftMouseDown
                              location:point
                         modifierFlags:0
                             timestamp:0
                          windowNumber:window.windowNumber
                               context:nil
                           eventNumber:0
                            clickCount:1
                              pressure:1];
}

NSEvent *keyDown(NSWindow *window) {
    return [NSEvent keyEventWithType:NSEventTypeKeyDown
                            location:NSZeroPoint
                       modifierFlags:0
                           timestamp:0
                        windowNumber:window.windowNumber
                             context:nil
                          characters:@"a"
         charactersIgnoringModifiers:@"a"
                           isARepeat:NO
                             keyCode:0];
}

bool checkDirectRecovery(host::HostComponent &component, backends::MetalDevice &device) {
    render::DrawList list;
    list.roundedRect({0, 0, 400, 300}, {}, render::packColor(19, 19, 19, 255));
    list.roundedRect({20, 20, 300, 200}, {8, 8, 8, 8}, render::packColor(255, 169, 35, 180));
    render::VectorPath path;
    path.moveTo({10, 230});
    path.lineTo({370, 25});
    path.lineTo({370, 270});
    path.close();
    list.fillPath(path, render::packColor(88, 205, 251, 220));
    auto &surface = static_cast<backends::MetalSurface &>(*component.surface());
    const auto extent = surface.extent();
    std::vector<float> actual(static_cast<std::size_t>(extent.width) * extent.height * 4);
    auto reference = device.createRenderSurface({extent});
    const math::PremultipliedColor clear{0.125, 0.25, 0.5, 0.75};
    if (!component.renderFrame(test::viewOf(list), nullptr, clear)) {
        return false;
    }
    surface.waitIdle();
    if (surface.readback(actual)) {
        std::cerr << "Native frame did not use the framebuffer-only direct target\n";
        return false;
    }
    hal::DamageRegion partial({0, 0, 8, 8});
    if (!component.renderFrame(test::viewOf(list), &partial, clear)) {
        return false;
    }
    surface.waitIdle();
    if (!surface.readback(actual) ||
        actual != test::renderFull(*reference, test::viewOf(list), clear)) {
        std::cerr << "Partial frame did not rebuild stale backing after direct rendering\n";
        return false;
    }
    if (!component.renderFrame(test::viewOf(list), nullptr, clear)) {
        return false;
    }
    surface.waitIdle();
    if (surface.readback(actual)) {
        return false;
    }
    list.setBlend(hal::BlendMode::multiply);
    list.roundedRect({40, 30, 340, 260}, {}, render::packColor(245, 247, 217, 200));
    if (!component.renderFrame(test::viewOf(list), &partial, clear)) {
        return false;
    }
    surface.waitIdle();
    if (!surface.readback(actual) ||
        actual != test::renderFull(*reference, test::viewOf(list), clear)) {
        std::cerr << "Nonlinear blend did not preserve linear-sRGB backing semantics\n";
        return false;
    }
    if (!component.renderFrame(test::viewOf(list), nullptr, clear)) {
        return false;
    }
    surface.waitIdle();
    const bool programmable = [MTLCreateSystemDefaultDevice() supportsFamily:MTLGPUFamilyApple1];
    if (surface.readback(actual) == programmable) {
        std::cerr << "Full nonlinear frame selected the wrong presentation path\n";
        return false;
    }
    std::cout << "Native direct frames, partial recovery and nonlinear blending passed\n";
    return true;
}
}

int main() {
    @autoreleasepool {
        auto created = backends::MetalDevice::create();
        if (!created) {
            std::cout << "No native GPU device available\n";
            return 77;
        }
        [NSApplication sharedApplication];
        auto &device = static_cast<backends::MetalDevice &>(*created);
        NSWindow *window = [[PrismKeyWindow alloc] initWithContentRect:NSMakeRect(0, 0, 400, 300)
                                                             styleMask:NSWindowStyleMaskBorderless
                                                               backing:NSBackingStoreBuffered
                                                                 defer:NO];
        window.releasedWhenClosed = NO;
        PrismEventRecorder *peer =
            [[PrismEventRecorder alloc] initWithFrame:NSMakeRect(0, 0, 400, 300)];
        window.contentView = peer;
        [window makeKeyAndOrderFront:nil];
        render::DrawList list;
        test::buildReferenceScene(list, 0);
        const auto view = test::viewOf(list);
        host::HostComponent component(device);
        bool ok = true;
        int presented = 0;
        for (int cycle = 0; cycle < 25 && ok; ++cycle) {
            ok = component.attachToPeer((__bridge void *)peer, {0, 0, 400, 300}) &&
                 peer.subviews.count == 1;
            NSView *passive = (__bridge NSView *)component.surfaceHost().nativeView();
            CAMetalLayer *layer = (CAMetalLayer *)passive.layer;
            ok = ok && layer.framebufferOnly && layer.displaySyncEnabled &&
                 layer.maximumDrawableCount == 3 &&
                 layer.pixelFormat == MTLPixelFormatRGBA16Float &&
                 CFEqual(CGColorSpaceGetName(layer.colorspace),
                         kCGColorSpaceExtendedLinearDisplayP3);
            [window makeFirstResponder:passive];
            const bool refusedFocus =
                window.firstResponder != passive && ![passive acceptsFirstResponder];
            NSView *target = [peer.superview hitTest:NSMakePoint(200, 150)];
            ok = ok && target == peer && refusedFocus && [window makeFirstResponder:peer];
            [target mouseDown:mouseDown(window, NSMakePoint(200, 150))];
            [window sendEvent:keyDown(window)];
            ok = ok && component.renderFrame(view, nullptr, {0, 0, 0, 1});
            if (cycle == 0 && ok) {
                ok = checkDirectRecovery(component, device);
            }
            component.setLiveResize(true);
            component.setBounds({0, 0, 333, 211});
            const hal::Extent drawable = component.surfaceHost().drawableExtent();
            const double scale = component.surfaceHost().contentsScale();
            ok = ok && drawable.width == static_cast<std::uint32_t>(333 * scale) &&
                 component.surface()->extent().width == drawable.width &&
                 component.renderFrame(view, nullptr, {0, 0, 0, 1});
            component.setLiveResize(false);
            component.setBounds({0, 0, 400, 300});
            component.surface()->loseDevice();
            ok = ok && component.renderFrame(view, nullptr, {0, 0, 0, 1}) &&
                 component.state() == host::SurfaceState::surfaceReady;
            component.detach();
            ok =
                ok && peer.subviews.count == 0 && component.state() == host::SurfaceState::detached;
            presented = static_cast<int>(component.presentedFrames());
        }
        std::cout << "Peer received " << peer.mouseDowns << " mouse and " << peer.keyDowns
                  << " key events; presented " << presented << " frames; drains "
                  << component.drains() << '\n';
        if (!ok || peer.mouseDowns != 25 || peer.keyDowns != 25 || component.drains() != 25 ||
            presented < 50) {
            std::cerr << "Passive surface contract failed\n";
            return 1;
        }
    }
    return 0;
}
