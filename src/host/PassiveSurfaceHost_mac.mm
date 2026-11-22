#include "host/PassiveSurfaceHost.h"

#include "backends/metal/MetalBackend.h"

#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>

#include <cmath>

@interface PrismPassiveSurfaceView : NSView
@end

@implementation PrismPassiveSurfaceView

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

void PassiveSurfaceHost::detach() {
    if (impl_->view == nil) {
        return;
    }
    @autoreleasepool {
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

bool PassiveSurfaceHost::present(render::RenderSurface &surface) {
    auto *metal = dynamic_cast<backends::MetalSurface *>(&surface);
    if (impl_->view == nil || metal == nullptr) {
        return false;
    }
    return metal->presentToLayer((__bridge void *)impl_->view.layer);
}
}
