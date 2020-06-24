#include "core_v2/internal/client_proxy.h"

#include <cstdlib>
#include <limits>
#include <utility>

#include "platform_v2/base/base64_utils.h"
#include "platform_v2/base/prng.h"
#include "platform_v2/public/crypto.h"
#include "platform_v2/public/logging.h"
#include "platform_v2/public/mutex_lock.h"
#include "proto/connections_enums.pb.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/strings/str_cat.h"

namespace location {
namespace nearby {
namespace connections {

ClientProxy::ClientProxy() : client_id_(Prng().NextInt64()) {}

ClientProxy::~ClientProxy() { Reset(); }

std::int64_t ClientProxy::GetClientId() const { return client_id_; }

std::string ClientProxy::GenerateLocalEndpointId() {
  // 1) Concatenate the DeviceID with this ClientID.
  // 2) Compute a hash of that concatenation.
  // 3) Base64-encode that hash, to make it human-readable.
  // 4) Use only the first 4 bytes of that Base64 encoding.
  ByteArray id_hash(Crypto::Sha256(
      absl::StrCat(api::ImplementationPlatform::GetDeviceId(), GetClientId())));

  return Base64Utils::Encode(id_hash).substr(0, kEndpointIdLength);
}

void ClientProxy::Reset() {
  MutexLock lock(&mutex_);

  StoppedAdvertising();
  StoppedDiscovery();
  RemoveAllEndpoints();
}

void ClientProxy::StartedAdvertising(
    const std::string& service_id, Strategy strategy,
    const ConnectionListener& listener,
    absl::Span<proto::connections::Medium> mediums) {
  MutexLock lock(&mutex_);

  advertising_info_ = {service_id, listener};
}

void ClientProxy::StoppedAdvertising() {
  MutexLock lock(&mutex_);

  if (IsAdvertising()) {
    advertising_info_.Clear();
  }
}

bool ClientProxy::IsAdvertising() const {
  MutexLock lock(&mutex_);

  return !advertising_info_.IsEmpty();
}

std::string ClientProxy::GetAdvertisingServiceId() const {
  MutexLock lock(&mutex_);
  return advertising_info_.service_id;
}

void ClientProxy::StartedDiscovery(
    const std::string& service_id, Strategy strategy,
    const DiscoveryListener& listener,
    absl::Span<proto::connections::Medium> mediums) {
  MutexLock lock(&mutex_);

  discovery_info_ = DiscoveryInfo{service_id, listener};
}

void ClientProxy::StoppedDiscovery() {
  MutexLock lock(&mutex_);

  if (IsDiscovering()) {
    discovered_endpoint_ids_.clear();
    discovery_info_.Clear();
  }
}

bool ClientProxy::IsDiscoveringServiceId(const std::string& service_id) const {
  MutexLock lock(&mutex_);

  return IsDiscovering() && service_id == discovery_info_.service_id;
}

bool ClientProxy::IsDiscovering() const {
  MutexLock lock(&mutex_);

  return !discovery_info_.IsEmpty();
}

std::string ClientProxy::GetDiscoveryServiceId() const {
  MutexLock lock(&mutex_);

  return discovery_info_.service_id;
}

void ClientProxy::OnEndpointFound(const std::string& service_id,
                                  const std::string& endpoint_id,
                                  const std::string& endpoint_name,
                                  proto::connections::Medium medium) {
  MutexLock lock(&mutex_);

  if (!IsDiscoveringServiceId(service_id)) return;
  if (discovered_endpoint_ids_.count(endpoint_id)) {
    // TODO(tracyzhou): Add logging.
    return;
  }
  discovered_endpoint_ids_.insert(endpoint_id);
  discovery_info_.listener.endpoint_found_cb(endpoint_id, endpoint_name,
                                             service_id);
}

void ClientProxy::OnEndpointLost(const std::string& service_id,
                                 const std::string& endpoint_id) {
  MutexLock lock(&mutex_);

  if (!IsDiscoveringServiceId(service_id)) return;
  const auto it = discovered_endpoint_ids_.find(endpoint_id);
  if (it == discovered_endpoint_ids_.end()) return;
  discovered_endpoint_ids_.erase(it);
  discovery_info_.listener.endpoint_lost_cb(endpoint_id);
}

void ClientProxy::OnConnectionInitiated(const std::string& endpoint_id,
                                        const ConnectionResponseInfo& info,
                                        const ConnectionListener& listener) {
  MutexLock lock(&mutex_);

  // Whether this is incoming or outgoing, the local and remote endpoints both
  // still need to accept this connection, so set its establishment status to
  // PENDING.
  auto result = connections_.emplace(
      endpoint_id, Connection{
                       .is_incoming = info.is_incoming_connection,
                       .connection_listener = listener,
                   });
  // Instead of using structured binding which is nice, but banned
  // (can not use c++17 features, until chromium does) we unpack manually.
  auto& pair_iter = result.first;
  bool& inserted = result.second;
  DCHECK(inserted);
  const Connection& item = pair_iter->second;
  // Notify the client.
  //
  // Note: we allow devices to connect to an advertiser even after it stops
  // advertising, so no need to check IsAdvertising() here.
  item.connection_listener.initiated_cb(endpoint_id, info);
}

void ClientProxy::OnConnectionAccepted(const std::string& endpoint_id) {
  MutexLock lock(&mutex_);

  if (!HasPendingConnectionToEndpoint(endpoint_id)) {
    // TODO(tracyzhou): Add logging.
    return;
  }

  // Notify the client.
  Connection* item = LookupConnection(endpoint_id);
  if (item != nullptr) {
    item->connection_listener.accepted_cb(endpoint_id);
    item->status = Connection::kConnected;
  }
}

void ClientProxy::OnConnectionRejected(const std::string& endpoint_id,
                                       const Status& status) {
  MutexLock lock(&mutex_);

  if (!HasPendingConnectionToEndpoint(endpoint_id)) {
    NEARBY_LOG(INFO, "ClientProxy [Rejected]: no pending connection; id=%s",
               endpoint_id.c_str());
    return;
  }

  // Notify the client.
  const Connection* item = LookupConnection(endpoint_id);
  if (item != nullptr) {
    item->connection_listener.rejected_cb(endpoint_id, status);
    OnDisconnected(endpoint_id, false /* notify */);
  }
}

void ClientProxy::OnBandwidthChanged(const std::string& endpoint_id,
                                     std::int32_t quality) {
  MutexLock lock(&mutex_);

  const Connection* item = LookupConnection(endpoint_id);
  if (item != nullptr) {
    item->connection_listener.bandwidth_changed_cb(endpoint_id, quality);
  }
}

void ClientProxy::OnDisconnected(const std::string& endpoint_id, bool notify) {
  MutexLock lock(&mutex_);

  const Connection* item = LookupConnection(endpoint_id);
  if (item != nullptr) {
    if (notify) {
      item->connection_listener.disconnected_cb({endpoint_id});
    }
    connections_.erase(endpoint_id);
  }
}

bool ClientProxy::ConnectionStatusMatches(const std::string& endpoint_id,
                                          Connection::Status status) const {
  MutexLock lock(&mutex_);

  const Connection* item = LookupConnection(endpoint_id);
  if (item != nullptr) {
    return item->status == status;
  }
  return false;
}

bool ClientProxy::IsConnectedToEndpoint(const std::string& endpoint_id) const {
  return ConnectionStatusMatches(endpoint_id, Connection::kConnected);
}

std::vector<std::string> ClientProxy::GetMatchingEndpoints(
    std::function<bool(const Connection&)> pred) const {
  MutexLock lock(&mutex_);

  std::vector<std::string> connected_endpoints;

  for (const auto& pair : connections_) {
    const auto& endpoint_id = pair.first;
    const auto& connection = pair.second;
    if (pred(connection)) {
      connected_endpoints.push_back(endpoint_id);
    }
  }
  return connected_endpoints;
}

std::vector<std::string> ClientProxy::GetPendingConnectedEndpoints() const {
  return GetMatchingEndpoints([](const Connection& connection) {
    return connection.status != Connection::kConnected;
  });
}

std::vector<std::string> ClientProxy::GetConnectedEndpoints() const {
  return GetMatchingEndpoints([](const Connection& connection) {
    return connection.status == Connection::kConnected;
  });
}

std::int32_t ClientProxy::GetNumOutgoingConnections() const {
  return GetMatchingEndpoints([](const Connection& connection) {
           return connection.status == Connection::kConnected &&
                  !connection.is_incoming;
         })
      .size();
}

std::int32_t ClientProxy::GetNumIncomingConnections() const {
  return GetMatchingEndpoints([](const Connection& connection) {
           return connection.status == Connection::kConnected &&
                  connection.is_incoming;
         })
      .size();
}

bool ClientProxy::HasPendingConnectionToEndpoint(
    const std::string& endpoint_id) const {
  MutexLock lock(&mutex_);

  const Connection* item = LookupConnection(endpoint_id);
  if (item != nullptr) {
    return item->status != Connection::kConnected;
  }
  return false;
}

bool ClientProxy::HasLocalEndpointResponded(
    const std::string& endpoint_id) const {
  MutexLock lock(&mutex_);

  return ConnectionStatusesContains(
      endpoint_id,
      static_cast<Connection::Status>(Connection::kLocalEndpointAccepted |
                                      Connection::kLocalEndpointRejected));
}

bool ClientProxy::HasRemoteEndpointResponded(
    const std::string& endpoint_id) const {
  MutexLock lock(&mutex_);

  return ConnectionStatusesContains(
      endpoint_id,
      static_cast<Connection::Status>(Connection::kRemoteEndpointAccepted |
                                      Connection::kRemoteEndpointRejected));
}

void ClientProxy::LocalEndpointAcceptedConnection(
    const std::string& endpoint_id, const PayloadListener& listener) {
  MutexLock lock(&mutex_);

  if (HasLocalEndpointResponded(endpoint_id)) {
    // TODO(tracyzhou): Add logging.
    return;
  }

  AppendConnectionStatus(endpoint_id, Connection::kLocalEndpointAccepted);
  Connection* item = LookupConnection(endpoint_id);
  if (item != nullptr) {
    item->payload_listener = listener;
  }
}

void ClientProxy::LocalEndpointRejectedConnection(
    const std::string& endpoint_id) {
  MutexLock lock(&mutex_);

  if (HasLocalEndpointResponded(endpoint_id)) {
    // TODO(tracyzhou): Add logging.
    return;
  }

  AppendConnectionStatus(endpoint_id, Connection::kLocalEndpointRejected);
}

void ClientProxy::RemoteEndpointAcceptedConnection(
    const std::string& endpoint_id) {
  MutexLock lock(&mutex_);

  if (HasRemoteEndpointResponded(endpoint_id)) {
    // TODO(tracyzhou): Add logging.
    return;
  }

  AppendConnectionStatus(endpoint_id, Connection::kRemoteEndpointAccepted);
}

void ClientProxy::RemoteEndpointRejectedConnection(
    const std::string& endpoint_id) {
  MutexLock lock(&mutex_);

  if (HasRemoteEndpointResponded(endpoint_id)) {
    // TODO(tracyzhou): Add logging.
    return;
  }

  AppendConnectionStatus(endpoint_id, Connection::kRemoteEndpointRejected);
}

bool ClientProxy::IsConnectionAccepted(const std::string& endpoint_id) const {
  MutexLock lock(&mutex_);

  return ConnectionStatusesContains(endpoint_id,
                                    Connection::kLocalEndpointAccepted) &&
         ConnectionStatusesContains(endpoint_id,
                                    Connection::kRemoteEndpointAccepted);
}

bool ClientProxy::IsConnectionRejected(const std::string& endpoint_id) const {
  MutexLock lock(&mutex_);

  return ConnectionStatusesContains(
      endpoint_id,
      static_cast<Connection::Status>(Connection::kLocalEndpointRejected |
                                      Connection::kRemoteEndpointRejected));
}

bool ClientProxy::LocalConnectionIsAccepted(std::string endpoint_id) const {
  return ConnectionStatusesContains(
      endpoint_id, ClientProxy::Connection::kLocalEndpointAccepted);
}

bool ClientProxy::RemoteConnectionIsAccepted(std::string endpoint_id) const {
  return ConnectionStatusesContains(
      endpoint_id, ClientProxy::Connection::kRemoteEndpointAccepted);
}

void ClientProxy::OnPayload(const std::string& endpoint_id, Payload payload) {
  MutexLock lock(&mutex_);

  if (IsConnectedToEndpoint(endpoint_id)) {
    const Connection* item = LookupConnection(endpoint_id);
    if (item != nullptr) {
      item->payload_listener.payload_cb(endpoint_id, std::move(payload));
    }
  }
}

const ClientProxy::Connection* ClientProxy::LookupConnection(
    const std::string& endpoint_id) const {
  auto item = connections_.find(endpoint_id);
  return item != connections_.end() ? &item->second : nullptr;
}

ClientProxy::Connection* ClientProxy::LookupConnection(
    const std::string& endpoint_id) {
  auto item = connections_.find(endpoint_id);
  return item != connections_.end() ? &item->second : nullptr;
}

void ClientProxy::OnPayloadProgress(const std::string& endpoint_id,
                                    const PayloadProgressInfo& info) {
  MutexLock lock(&mutex_);

  if (IsConnectedToEndpoint(endpoint_id)) {
    Connection* item = LookupConnection(endpoint_id);
    if (item != nullptr) {
      item->payload_listener.payload_progress_cb(endpoint_id, info);
    }
  }
}

bool operator==(const ClientProxy& lhs, const ClientProxy& rhs) {
  return lhs.GetClientId() == rhs.GetClientId();
}

bool operator<(const ClientProxy& lhs, const ClientProxy& rhs) {
  return lhs.GetClientId() < rhs.GetClientId();
}

void ClientProxy::RemoveAllEndpoints() {
  MutexLock lock(&mutex_);

  // Note: we may want to notify the client of onDisconnected() for each
  // endpoint, in the case when this is called from stopAllEndpoints(). For now,
  // just remove without notifying.
  connections_.clear();
}

bool ClientProxy::ConnectionStatusesContains(
    const std::string& endpoint_id, Connection::Status status_to_match) const {
  const Connection* item = LookupConnection(endpoint_id);
  if (item != nullptr) {
    return (item->status & status_to_match) != 0;
  }
  return false;
}

void ClientProxy::AppendConnectionStatus(const std::string& endpoint_id,
                                         Connection::Status status_to_append) {
  Connection* item = LookupConnection(endpoint_id);
  if (item != nullptr) {
    item->status =
        static_cast<Connection::Status>(item->status | status_to_append);
  }
}

}  // namespace connections
}  // namespace nearby
}  // namespace location