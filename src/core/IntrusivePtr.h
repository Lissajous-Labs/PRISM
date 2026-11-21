#pragma once

#include <atomic>
#include <cstdint>
#include <utility>

namespace prism {
class RefCounted {
  public:
    RefCounted() = default;
    RefCounted(const RefCounted &) = delete;
    RefCounted &operator=(const RefCounted &) = delete;

    void retain() const noexcept { count_.fetch_add(1, std::memory_order_relaxed); }

    [[nodiscard]] bool release() const noexcept {
        return count_.fetch_sub(1, std::memory_order_acq_rel) == 1;
    }

    [[nodiscard]] std::uint32_t useCount() const noexcept {
        return count_.load(std::memory_order_acquire);
    }

  protected:
    ~RefCounted() = default;

  private:
    mutable std::atomic<std::uint32_t> count_{0};
};

template <class T> class IntrusivePtr {
  public:
    IntrusivePtr() noexcept = default;
    explicit IntrusivePtr(T *object) noexcept : object_(object) {
        if (object_) {
            object_->retain();
        }
    }
    IntrusivePtr(const IntrusivePtr &other) noexcept : IntrusivePtr(other.object_) {}
    IntrusivePtr(IntrusivePtr &&other) noexcept : object_(std::exchange(other.object_, nullptr)) {}
    ~IntrusivePtr() { reset(); }

    IntrusivePtr &operator=(IntrusivePtr other) noexcept {
        std::swap(object_, other.object_);
        return *this;
    }

    void reset() noexcept {
        if (object_ && object_->release()) {
            delete object_;
        }
        object_ = nullptr;
    }

    [[nodiscard]] T *get() const noexcept { return object_; }
    T *operator->() const noexcept { return object_; }
    T &operator*() const noexcept { return *object_; }
    explicit operator bool() const noexcept { return object_ != nullptr; }

  private:
    T *object_ = nullptr;
};

template <class T, class... Arguments> IntrusivePtr<T> makeIntrusive(Arguments &&...arguments) {
    return IntrusivePtr<T>(new T(std::forward<Arguments>(arguments)...));
}
}
