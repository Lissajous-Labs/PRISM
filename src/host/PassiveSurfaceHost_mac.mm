#include "host/PassiveSurfaceHost.h"

#include "backends/metal/MetalBackend.h"

#import <AppKit/AppKit.h>
#import <CoreVideo/CoreVideo.h>
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>

#include <atomic>
#include <cmath>

@interface PrismPassiveSurfaceView : NSView {
    id displayLink_;
    CVDisplayLinkRef legacyLink_;
    CFRunLoopSourceRef source_;
    std::atomic<double> timestamp_;
    std::atomic<double> legacyPeriod_;
    prism::host::FrameTick tick_;
}
- (BOOL)startFrameClock:(prism::host::FrameTick)tick legacy:(BOOL)legacy;
- (void)stopFrameClock;
- (void)deliverFrame:(double)timestamp;
@property(nonatomic) double framePeriod;
@end

@implementation PrismPassiveSurfaceView

- (void)deliverFrame:(double)timestamp {
    const auto tick = tick_;
    if (tick.function != nullptr && self.window != nil && !self.hiddenOrHasHiddenAncestor) {
        tick.function(tick.context, timestamp);
    }
}

- (void)displayTick:(CADisplayLink *)link API_AVAILABLE(macos(14.0)) {
    self.framePeriod = link.targetTimestamp - link.timestamp;
    [self deliverFrame:link.targetTimestamp];
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
- (BOOL)startFrameClock:(prism::host::FrameTick)tick legacy:(BOOL)legacy {
    [self stopFrameClock];
    if (tick.function == nullptr || self.window == nil) {
        return NO;
    }
    tick_ = tick;
    self.framePeriod = 1.0 / std::max<NSInteger>(self.window.screen.maximumFramesPerSecond, 1);
    if (@available(macOS 14.0, *)) {
        if (!legacy) {
            CADisplayLink *link = [self displayLinkWithTarget:self
                                                     selector:@selector(displayTick:)];
            link.preferredFrameRateRange = CAFrameRateRangeMake(60, 120, 120);
            displayLink_ = link;
            [link addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes];
            return YES;
        }
    }

    CFRunLoopSourceContext context{};
    context.info = (__bridge void *)self;
    context.perform = [](void *info) {
        PrismPassiveSurfaceView *view = (__bridge PrismPassiveSurfaceView *)info;
        const double timestamp = view->timestamp_.load(std::memory_order_acquire);
        view.framePeriod = view->legacyPeriod_.load(std::memory_order_relaxed);
        [view deliverFrame:timestamp];
    };
    source_ = CFRunLoopSourceCreate(kCFAllocatorDefault, 0, &context);
    NSNumber *number = self.window.screen.deviceDescription[@"NSScreenNumber"];
    if (source_ == nullptr || number == nil ||
        CVDisplayLinkCreateWithCGDisplay([number unsignedIntValue], &legacyLink_) !=
            kCVReturnSuccess) {
        [self stopFrameClock];
        return NO;
    }
    CFRunLoopAddSource(CFRunLoopGetMain(), source_, kCFRunLoopCommonModes);
    const auto callback = [](CVDisplayLinkRef, const CVTimeStamp *, const CVTimeStamp *output,
                             CVOptionFlags, CVOptionFlags *, void *info) -> CVReturn {
        PrismPassiveSurfaceView *view = (__bridge PrismPassiveSurfaceView *)info;
        const double seconds = static_cast<double>(output->hostTime) / CVGetHostClockFrequency();
        if (output->videoTimeScale > 0) {
            view->legacyPeriod_.store(static_cast<double>(output->videoRefreshPeriod) /
                                          static_cast<double>(output->videoTimeScale),
                                      std::memory_order_relaxed);
        }
        view->timestamp_.store(seconds, std::memory_order_release);
        CFRunLoopSourceSignal(view->source_);
        CFRunLoopWakeUp(CFRunLoopGetMain());
        return kCVReturnSuccess;
    };
    if (CVDisplayLinkSetOutputCallback(legacyLink_, callback, (__bridge void *)self) !=
            kCVReturnSuccess ||
        CVDisplayLinkStart(legacyLink_) != kCVReturnSuccess) {
        [self stopFrameClock];
        return NO;
    }
    return YES;
}

- (void)stopFrameClock {
    if (@available(macOS 14.0, *)) {
        [(CADisplayLink *)displayLink_ invalidate];
    }
    displayLink_ = nil;
    if (legacyLink_ != nullptr) {
        CVDisplayLinkStop(legacyLink_);
        CVDisplayLinkRelease(legacyLink_);
        legacyLink_ = nullptr;
    }
    if (source_ != nullptr) {
        CFRunLoopSourceInvalidate(source_);
        CFRelease(source_);
        source_ = nullptr;
    }
    tick_ = {};
}
#pragma clang diagnostic pop

- (instancetype)initWithFrame:(NSRect)frame {
    self = [super initWithFrame:frame];
    if (self != nil) {
        self.wantsLayer = YES;
        self.layerContentsRedrawPolicy = NSViewLayerContentsRedrawNever;
    }
    return self;
}

- (CALayer *)makeBackingLayer {
    CAMetalLayer *layer = [CAMetalLayer layer];
    layer.pixelFormat = MTLPixelFormatRGBA16Float;
    layer.framebufferOnly = YES;
    layer.displaySyncEnabled = YES;
    layer.maximumDrawableCount = 3;
    CGColorSpaceRef colorSpace = CGColorSpaceCreateWithName(kCGColorSpaceExtendedLinearDisplayP3);
    layer.colorspace = colorSpace;
    CGColorSpaceRelease(colorSpace);
    layer.opaque = NO;
    return layer;
}

- (NSView *)hitTest:(NSPoint)point {
    return nil;
}

- (BOOL)acceptsFirstResponder {
    return NO;
}

- (BOOL)becomeFirstResponder {
    return NO;
}

- (BOOL)canBecomeKeyView {
    return NO;
}

- (BOOL)acceptsFirstMouse:(NSEvent *)event {
    return NO;
}

- (BOOL)isOpaque {
    return NO;
}

- (void)syncDrawable {
    CAMetalLayer *layer = (CAMetalLayer *)self.layer;
    const CGFloat scale = self.window != nil ? self.window.backingScaleFactor : layer.contentsScale;
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    layer.contentsScale = scale;
    layer.drawableSize = CGSizeMake(std::round(self.bounds.size.width * scale),
                                    std::round(self.bounds.size.height * scale));
    [CATransaction commit];
}

- (void)viewDidChangeBackingProperties {
    [super viewDidChangeBackingProperties];
    [self syncDrawable];
}

- (void)setFrameSize:(NSSize)size {
    [super setFrameSize:size];
    [self syncDrawable];
}

- (void)viewWillStartLiveResize {
    [super viewWillStartLiveResize];
    ((CAMetalLayer *)self.layer).presentsWithTransaction = YES;
}

- (void)viewDidEndLiveResize {
    [super viewDidEndLiveResize];
    ((CAMetalLayer *)self.layer).presentsWithTransaction = NO;
}

@end

namespace prism::host {
struct PassiveSurfaceHost::Impl {
    PrismPassiveSurfaceView *view = nil;
};

PassiveSurfaceHost::PassiveSurfaceHost() : impl_(std::make_unique<Impl>()) {}

PassiveSurfaceHost::~PassiveSurfaceHost() { detach(); }

bool PassiveSurfaceHost::supported() noexcept { return true; }

bool PassiveSurfaceHost::attach(void *peerView, PeerBounds bounds) {
    if (impl_->view != nil || peerView == nullptr) {
        return false;
    }
    @autoreleasepool {
        NSView *peer = (__bridge NSView *)peerView;
        impl_->view = [[PrismPassiveSurfaceView alloc]
            initWithFrame:NSMakeRect(bounds.x, bounds.y, bounds.width, bounds.height)];
        [peer addSubview:impl_->view positioned:NSWindowBelow relativeTo:nil];
        [impl_->view syncDrawable];
    }
    return true;
}

void PassiveSurfaceHost::setBounds(PeerBounds bounds) {
    if (impl_->view != nil) {
        [impl_->view setFrame:NSMakeRect(bounds.x, bounds.y, bounds.width, bounds.height)];
        [impl_->view syncDrawable];
    }
}

void PassiveSurfaceHost::setLiveResize(bool active) {
    if (impl_->view != nil) {
        ((CAMetalLayer *)impl_->view.layer).presentsWithTransaction = active ? YES : NO;
    }
}

bool PassiveSurfaceHost::startFrameClock(FrameTick tick, bool preferLegacy) {
    return [NSThread isMainThread] && impl_->view != nil &&
           [impl_->view startFrameClock:tick legacy:preferLegacy];
}

void PassiveSurfaceHost::stopFrameClock() { [impl_->view stopFrameClock]; }

void PassiveSurfaceHost::detach() {
    if (impl_->view == nil) {
        return;
    }
    @autoreleasepool {
        stopFrameClock();
        [impl_->view removeFromSuperview];
        impl_->view = nil;
    }
}

bool PassiveSurfaceHost::attached() const noexcept { return impl_->view != nil; }

double PassiveSurfaceHost::contentsScale() const noexcept {
    return impl_->view != nil ? impl_->view.layer.contentsScale : 0.0;
}

hal::Extent PassiveSurfaceHost::drawableExtent() const noexcept {
    if (impl_->view == nil) {
        return {0, 0};
    }
    const CGSize size = ((CAMetalLayer *)impl_->view.layer).drawableSize;
    return {static_cast<std::uint32_t>(size.width), static_cast<std::uint32_t>(size.height)};
}

void *PassiveSurfaceHost::nativeView() const noexcept { return (__bridge void *)impl_->view; }

bool PassiveSurfaceHost::renderFrame(render::RenderSurface &surface,
                                     const render::FrameRequest &request, bool &presented) {
    presented = false;
    auto *metal = dynamic_cast<backends::MetalSurface *>(&surface);
    if (impl_->view == nil || metal == nullptr) {
        return false;
    }
    return metal->renderToLayer(request, (__bridge void *)impl_->view.layer,
                                impl_->view.framePeriod, presented);
}
}
