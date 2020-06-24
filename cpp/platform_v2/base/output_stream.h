#ifndef PLATFORM_V2_BASE_OUTPUT_STREAM_H_
#define PLATFORM_V2_BASE_OUTPUT_STREAM_H_

#include "platform_v2/base/byte_array.h"
#include "platform_v2/base/exception.h"

namespace location {
namespace nearby {

// An OutputStream represents an output stream of bytes.
//
// https://docs.oracle.com/javase/8/docs/api/java/io/OutputStream.html
class OutputStream {
 public:
  virtual ~OutputStream() = default;

  virtual Exception Write(const ByteArray& data) = 0;  // throws Exception::kIo
  virtual Exception Flush() = 0;                       // throws Exception::kIo
  virtual Exception Close() = 0;                       // throws Exception::kIo
};

}  // namespace nearby
}  // namespace location

#endif  // PLATFORM_V2_BASE_OUTPUT_STREAM_H_