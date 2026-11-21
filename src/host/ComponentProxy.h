#pragma once

#include "core/FunctionRef.h"
#include "render/DrawList.h"
#include "render/FrameView.h"

#include <cstdint>
#include <span>
#include <vector>

namespace prism::host {
enum class ProxyEventKind : std::uint8_t { mouseDown, mouseDrag, mouseUp, mouseWheel, keyDown };

struct ProxyEvent {
    ProxyEventKind kind;
    float x;
    float y;
    float delta = 0;
    std::uint32_t key = 0;
};

class ComponentProxy {
  public:
    ComponentProxy(std::uint32_t width, std::uint32_t height)
        : width_(width), height_(height),
          texels_(static_cast<std::size_t>(width) * height * 4, 0.0f) {}

    void repaint(FunctionRef<void(std::span<float>, std::uint32_t, std::uint32_t)> painter) {
        painter(texels_, width_, height_);
        ++generation_;
    }

    [[nodiscard]] render::TextureView texture() const noexcept {
        return {width_, height_, 4, texels_};
    }
    [[nodiscard]] std::uint64_t generation() const noexcept { return generation_; }

    void place(render::DrawList &list, render::RectF rect, std::uint32_t textureSlot,
               std::uint32_t tint) {
        rect_ = rect;
        list.texturedQuad(render::PrimitiveKind::texturedQuad, rect, {0, 0, 1, 1}, textureSlot,
                          tint);
    }

    bool forward(const ProxyEvent &event, FunctionRef<void(const ProxyEvent &)> receiver) const {
        if (event.x < rect_.x0 || event.x >= rect_.x1 || event.y < rect_.y0 ||
            event.y >= rect_.y1) {
            return false;
        }
        ProxyEvent local = event;
        local.x = (event.x - rect_.x0) * static_cast<float>(width_) / (rect_.x1 - rect_.x0);
        local.y = (event.y - rect_.y0) * static_cast<float>(height_) / (rect_.y1 - rect_.y0);
        receiver(local);
        return true;
    }

  private:
    std::uint32_t width_;
    std::uint32_t height_;
    std::vector<float> texels_;
    std::uint64_t generation_ = 0;
    render::RectF rect_{0, 0, 0, 0};
};
}
