#ifndef CORE_V2_INTERNAL_PCP_HANDLER_H_
#define CORE_V2_INTERNAL_PCP_HANDLER_H_

#include <vector>

#include "core_v2/internal/client_proxy.h"
#include "core_v2/internal/pcp.h"
#include "core_v2/listeners.h"
#include "core_v2/options.h"
#include "core_v2/params.h"
#include "core_v2/status.h"
#include "core_v2/strategy.h"
#include "proto/connections_enums.pb.h"

namespace location {
namespace nearby {
namespace connections {

// Defines the set of methods that need to be implemented to handle the
// per-PCP-specific operations in the OfflineServiceController.
//
// These methods are all meant to be synchronous, and should return only after
// knowing they've done what they were supposed to do (or unequivocally failed
// to do so).
//
// See details here:
// https://source.corp.google.com/piper///depot/google3/core_v2/core.h
class PcpHandler {
 public:
  virtual ~PcpHandler() = default;

  // Return strategy supported by this protocol.
  virtual Strategy GetStrategy() const = 0;

  // Return concrete variant of protocol.
  virtual Pcp GetPcp() const = 0;

  // We have been asked by the client to start advertising. Once we successfully
  // start advertising, we'll change the ClientProxy's state.
  // ConnectionListener (info.listener) will be notified in case of any event.
  // See
  // https://source.corp.google.com/piper///depot/google3/core_v2/listeners.h;bpv=1;bpt=1;l=71?gsn=ConnectionListener
  virtual Status StartAdvertising(ClientProxy* client,
                                  const std::string& service_id,
                                  const ConnectionOptions& options,
                                  const ConnectionRequestInfo& info) = 0;

  // If Advertising is active, stop it, and change CLientProxy state,
  // otherwise do nothing.
  virtual void StopAdvertising(ClientProxy* client) = 0;

  // Start discovery of endpoints that may be advertising.
  // Update ClientProxy state once discovery started.
  // DiscoveryListener will get called in case of any event.
  virtual Status StartDiscovery(ClientProxy* client,
                                const std::string& service_id,
                                const ConnectionOptions& options,
                                const DiscoveryListener& listener) = 0;

  // If Discovery is active, stop it, and change CLientProxy state,
  // otherwise do nothing.
  virtual void StopDiscovery(ClientProxy* client) = 0;

  // If remote endpoint has been successfully discovered, request it to form a
  // connection, update state on ClientProxy.
  virtual Status RequestConnection(ClientProxy* client,
                                   const std::string& endpoint_id,
                                   const ConnectionRequestInfo& info) = 0;

  // Either party may call this to accept connection on their part.
  // Until both parties call it, connection will not reach a data phase.
  // Update state in ClientProxy.
  virtual Status AcceptConnection(ClientProxy* clientProxy,
                                  const std::string& endpoint_id,
                                  const PayloadListener& payload_listener) = 0;

  // Either party may call this to reject connection on their part before
  // connection reaches data phase. If either party does call it, connection
  // will terminate. Update state in ClientProxy.
  virtual Status RejectConnection(ClientProxy* client,
                                  const std::string& endpoint_id) = 0;
};

}  // namespace connections
}  // namespace nearby
}  // namespace location

#endif  // CORE_V2_INTERNAL_PCP_HANDLER_H_