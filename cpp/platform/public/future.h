#ifndef PLATFORM_PUBLIC_FUTURE_H_
#define PLATFORM_PUBLIC_FUTURE_H_

#include "platform/public/settable_future.h"

namespace location {
namespace nearby {

template <typename T>
class Future final {
 public:
  virtual bool Set(T value) { return impl_->Set(std::move(value)); }
  virtual bool SetException(Exception exception) {
    return impl_->SetException(exception);
  }
  virtual ExceptionOr<T> Get() { return impl_->Get(); }
  virtual ExceptionOr<T> Get(absl::Duration timeout) {
    return impl_->Get(timeout);
  }
  void AddListener(Runnable runnable, api::Executor* executor) {
    impl_->AddListener(std::move(runnable), executor);
  }
  bool IsSet() const {
    return impl_->IsSet();
  }

 private:
  // Instance of future implementation is wrapped in shared_ptr<> to make
  // it possible to pass Future by value and share the implementation.
  // This allows for the following constructions:
  // 1)
  // Future<bool> future;
  // RunOnXyzThread([future]() { future.Set(DoTheJobAndReport()); });
  // if (future.Get().Ok()) { /*...*/ }
  // 2)
  // Future<bool> future = DoSomeAsyncWork(); // Returns future, but keeps copy.
  // if (future.Get().Ok()) { /*...*/ }
  std::shared_ptr<SettableFuture<T>> impl_{new SettableFuture<T>()};
};

}  // namespace nearby
}  // namespace location

#endif  // PLATFORM_PUBLIC_FUTURE_H_