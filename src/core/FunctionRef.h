#pragma once

#include <memory>
#include <type_traits>
#include <utility>

namespace prism {
template <class Signature> class FunctionRef;

template <class Result, class... Arguments> class FunctionRef<Result(Arguments...)> {
  public:
    template <class Callable>
        requires(!std::is_same_v<std::remove_cvref_t<Callable>, FunctionRef> &&
                 std::is_invocable_r_v<Result, Callable &, Arguments...>)
    constexpr FunctionRef(Callable &&callable) noexcept
        : object_(const_cast<void *>(static_cast<const void *>(std::addressof(callable)))),
          thunk_([](void *object, Arguments... arguments) -> Result {
              return (*static_cast<std::remove_reference_t<Callable> *>(object))(
                  std::forward<Arguments>(arguments)...);
          }) {}

    constexpr Result operator()(Arguments... arguments) const {
        return thunk_(object_, std::forward<Arguments>(arguments)...);
    }

  private:
    void *object_;
    Result (*thunk_)(void *, Arguments...);
};
}
