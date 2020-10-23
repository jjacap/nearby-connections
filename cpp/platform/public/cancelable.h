#ifndef PLATFORM_PUBLIC_CANCELABLE_H_
#define PLATFORM_PUBLIC_CANCELABLE_H_

#include <memory>
#include <utility>

#include "platform/api/cancelable.h"

namespace location {
namespace nearby {

// An interface to provide a cancellation mechanism for objects that represent
// long-running operations.
class Cancelable final {
 public:
  Cancelable() = default;
  Cancelable(const Cancelable&) = default;
  Cancelable& operator=(const Cancelable& other) = default;

  ~Cancelable() = default;

  // This constructor is used internally only,
  // by other classes in "//platform/public/".
  explicit Cancelable(std::shared_ptr<api::Cancelable> impl)
      : impl_(std::move(impl)) {}

  bool Cancel() { return impl_ ? impl_->Cancel() : false; }

  bool IsValid() { return impl_ != nullptr; }

 private:
  std::shared_ptr<api::Cancelable> impl_;
};

}  // namespace nearby
}  // namespace location

#endif  // PLATFORM_PUBLIC_CANCELABLE_H_