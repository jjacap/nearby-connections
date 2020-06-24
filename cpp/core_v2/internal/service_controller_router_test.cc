#include "core_v2/internal/service_controller_router.h"

#include <cinttypes>
#include <memory>
#include <string>

#include "core_v2/internal/client_proxy.h"
#include "core_v2/internal/mock_service_controller.h"
#include "core_v2/internal/service_controller.h"
#include "core_v2/listeners.h"
#include "core_v2/options.h"
#include "core_v2/params.h"
#include "platform_v2/base/byte_array.h"
#include "platform_v2/public/condition_variable.h"
#include "platform_v2/public/mutex.h"
#include "platform_v2/public/mutex_lock.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/container/flat_hash_set.h"
#include "absl/time/clock.h"
#include "absl/types/span.h"

namespace location {
namespace nearby {
namespace connections {

namespace {
using ::testing::Return;
}  // namespace

// This class must be in the same namespace as ServiceControllerRouter for
// friend class to work.
class ServiceControllerRouterTest : public testing::Test {
 public:
  ServiceControllerRouterTest() = default;
  ~ServiceControllerRouterTest() override {
    router_.service_controller_.release();
  }

  void StartAdvertising(ClientProxy* client, std::string service_id,
                        ConnectionOptions options, ConnectionRequestInfo info,
                        ResultCallback callback) {
    EXPECT_CALL(mock_, StartAdvertising)
        .WillOnce(Return(Status{Status::kSuccess}));
    {
      MutexLock lock(&mutex_);
      complete_ = false;
      router_.StartAdvertising(client, service_id, options, info, callback);
      while (!complete_) cond_.Wait();
      EXPECT_EQ(result_, Status{Status::kSuccess});
    }
    client->StartedAdvertising(kServiceId, options.strategy, info.listener,
                               absl::MakeSpan(mediums_));
    EXPECT_TRUE(client->IsAdvertising());
  }

  void StopAdvertising(ClientProxy* client, ResultCallback callback) {
    EXPECT_CALL(mock_, StopAdvertising).Times(1);
    {
      MutexLock lock(&mutex_);
      complete_ = false;
      router_.StopAdvertising(client, callback);
      while (!complete_) cond_.Wait();
    }
    client->StoppedAdvertising();
    EXPECT_FALSE(client->IsAdvertising());
  }

  void StartDiscovery(ClientProxy* client, std::string service_id,
                      ConnectionOptions options,
                      const DiscoveryListener& listener,
                      const ResultCallback& callback) {
    EXPECT_CALL(mock_, StartDiscovery)
        .WillOnce(Return(Status{Status::kSuccess}));
    {
      MutexLock lock(&mutex_);
      complete_ = false;
      router_.StartDiscovery(client, kServiceId, options, listener, callback);
      while (!complete_) cond_.Wait();
      EXPECT_EQ(result_, Status{Status::kSuccess});
    }
    client->StartedDiscovery(service_id, options.strategy, listener,
                             absl::MakeSpan(mediums_));
    EXPECT_TRUE(client->IsDiscovering());
  }

  void StopDiscovery(ClientProxy* client, ResultCallback callback) {
    EXPECT_CALL(mock_, StopDiscovery).Times(1);
    {
      MutexLock lock(&mutex_);
      complete_ = false;
      router_.StopDiscovery(client, callback);
      while (!complete_) cond_.Wait();
    }
    client->StoppedDiscovery();
    EXPECT_FALSE(client->IsDiscovering());
  }

  void RequestConnection(ClientProxy* client, const std::string& endpoint_id,
                         const ConnectionRequestInfo& request_info,
                         ResultCallback callback) {
    EXPECT_CALL(mock_, RequestConnection)
        .WillOnce(Return(Status{Status::kSuccess}));
    {
      MutexLock lock(&mutex_);
      complete_ = false;
      router_.RequestConnection(client, endpoint_id, request_info, callback);
      while (!complete_) cond_.Wait();
      EXPECT_EQ(result_, Status{Status::kSuccess});
    }
    ConnectionResponseInfo response_info{
        .remote_endpoint_name = "endpoint_name",
        .authentication_token = "auth_token",
        .raw_authentication_token = ByteArray("auth_token"),
        .is_incoming_connection = true,
    };
    client->OnConnectionInitiated(endpoint_id, response_info,
                                  request_info.listener);
    EXPECT_TRUE(client->HasPendingConnectionToEndpoint(endpoint_id));
  }

  void AcceptConnection(ClientProxy* client, const std::string endpoint_id,
                        const PayloadListener& listener,
                        const ResultCallback& callback) {
    EXPECT_CALL(mock_, AcceptConnection)
        .WillOnce(Return(Status{Status::kSuccess}));
    // Pre-condition for successful Accept is: connection must exist.
    EXPECT_TRUE(client->HasPendingConnectionToEndpoint(endpoint_id));
    {
      MutexLock lock(&mutex_);
      complete_ = false;
      router_.AcceptConnection(client, endpoint_id, listener, callback);
      while (!complete_) cond_.Wait();
      EXPECT_EQ(result_, Status{Status::kSuccess});
    }
    client->LocalEndpointAcceptedConnection(endpoint_id, listener);
    client->RemoteEndpointAcceptedConnection(endpoint_id);
    EXPECT_TRUE(client->IsConnectionAccepted(endpoint_id));
    client->OnConnectionAccepted(endpoint_id);
    EXPECT_TRUE(client->IsConnectedToEndpoint(endpoint_id));
  }

  void RejectConnection(ClientProxy* client, const std::string endpoint_id,
                        ResultCallback callback) {
    EXPECT_CALL(mock_, RejectConnection)
        .WillOnce(Return(Status{Status::kSuccess}));
    // Pre-condition for successful Accept is: connection must exist.
    EXPECT_TRUE(client->HasPendingConnectionToEndpoint(endpoint_id));
    {
      MutexLock lock(&mutex_);
      complete_ = false;
      router_.RejectConnection(client, endpoint_id, callback);
      while (!complete_) cond_.Wait();
      EXPECT_EQ(result_, Status{Status::kSuccess});
    }
    client->LocalEndpointRejectedConnection(endpoint_id);
    EXPECT_TRUE(client->IsConnectionRejected(endpoint_id));
  }

  void InitiateBandwidthUpgrade(ClientProxy* client,
                                const std::string endpoint_id,
                                ResultCallback callback) {
    EXPECT_CALL(mock_, InitiateBandwidthUpgrade).Times(1);
    EXPECT_TRUE(client->IsConnectedToEndpoint(endpoint_id));
    {
      MutexLock lock(&mutex_);
      complete_ = false;
      router_.InitiateBandwidthUpgrade(client, endpoint_id, callback);
      while (!complete_) cond_.Wait();
      EXPECT_EQ(result_, Status{Status::kSuccess});
    }
  }

  void SendPayload(ClientProxy* client,
                   const std::vector<std::string>& endpoint_ids,
                   Payload payload, ResultCallback callback) {
    EXPECT_CALL(mock_, SendPayload).Times(1);

    bool connected = false;
    for (const auto& endpoint_id : endpoint_ids) {
      connected = connected || client->IsConnectedToEndpoint(endpoint_id);
    }
    EXPECT_TRUE(connected);
    {
      MutexLock lock(&mutex_);
      complete_ = false;
      router_.SendPayload(client, absl::MakeSpan(endpoint_ids),
                          std::move(payload), callback);
      while (!complete_) cond_.Wait();
      EXPECT_EQ(result_, Status{Status::kSuccess});
    }
  }

  void CancelPayload(ClientProxy* client, std::int64_t payload_id,
                     ResultCallback callback) {
    EXPECT_CALL(mock_, CancelPayload)
        .WillOnce(Return(Status{Status::kSuccess}));
    {
      MutexLock lock(&mutex_);
      complete_ = false;
      router_.CancelPayload(client, payload_id, callback);
      while (!complete_) cond_.Wait();
      EXPECT_EQ(result_, Status{Status::kSuccess});
    }
  }

  void DisconnectFromEndpoint(ClientProxy* client,
                              const std::string endpoint_id,
                              ResultCallback callback) {
    EXPECT_CALL(mock_, DisconnectFromEndpoint).Times(1);
    EXPECT_TRUE(client->IsConnectedToEndpoint(endpoint_id));
    {
      MutexLock lock(&mutex_);
      complete_ = false;
      router_.DisconnectFromEndpoint(client, endpoint_id, callback);
      while (!complete_) cond_.Wait();
    }
    client->OnDisconnected(endpoint_id, false);
    EXPECT_FALSE(client->IsConnectedToEndpoint(endpoint_id));
  }

 protected:
  const ResultCallback kCallback{
      .result_cb =
          [this](Status status) {
            MutexLock lock(&mutex_);
            result_ = status;
            complete_ = true;
            cond_.Notify();
          },
  };
  const std::string kServiceId = "service id";
  const std::string kRequestorName = "requestor name";
  const std::string kRemoteEndpointId = "remote endpoint id";
  const std::int64_t kPayloadId = UINT64_C(0x123456789ABCDEF0);
  const ConnectionOptions kConnectionOptions{
      .strategy = Strategy::kP2pPointToPoint,
      .auto_upgrade_bandwidth = true,
      .enforce_topology_constraints = true,
  };

  std::vector<proto::connections::Medium> mediums_{
      proto::connections::Medium::BLUETOOTH};
  const ConnectionRequestInfo kConnectionRequestInfo{
      .name = kRequestorName,
      .listener = ConnectionListener(),
  };

  DiscoveryListener discovery_listener_;
  PayloadListener payload_listener_;

  Mutex mutex_;
  ConditionVariable cond_{&mutex_};
  Status result_ ABSL_GUARDED_BY(mutex_) = {Status::kError};
  bool complete_ ABSL_GUARDED_BY(mutex_) = false;
  MockServiceController mock_;
  ClientProxy client_;

  ServiceControllerRouter router_{
      [this]() -> ServiceController* { return &mock_; }};
};

namespace {
TEST_F(ServiceControllerRouterTest, CostructorDestructorWorks) { SUCCEED(); }

TEST_F(ServiceControllerRouterTest, StartAdvertisingCalled) {
  StartAdvertising(&client_, kServiceId, kConnectionOptions,
                   kConnectionRequestInfo, kCallback);
}

TEST_F(ServiceControllerRouterTest, StopAdvertisingCalled) {
  StartAdvertising(&client_, kServiceId, kConnectionOptions,
                   kConnectionRequestInfo, kCallback);
  StopAdvertising(&client_, kCallback);
}

TEST_F(ServiceControllerRouterTest, StartDiscoveryCalled) {
  StartDiscovery(&client_, kServiceId, kConnectionOptions, discovery_listener_,
                 kCallback);
}

TEST_F(ServiceControllerRouterTest, StopDiscoveryCalled) {
  StartDiscovery(&client_, kServiceId, kConnectionOptions, discovery_listener_,
                 kCallback);
  StopDiscovery(&client_, kCallback);
}

TEST_F(ServiceControllerRouterTest, RequestConnectionCalled) {
  // Either Advertising, or Discovery should be ongoing.
  StartDiscovery(&client_, kServiceId, kConnectionOptions, discovery_listener_,
                 kCallback);
  RequestConnection(&client_, kRemoteEndpointId, kConnectionRequestInfo,
                    kCallback);
}

TEST_F(ServiceControllerRouterTest, AcceptConnectionCalled) {
  // Either Adviertisng, or Discovery should be ongoing.
  StartDiscovery(&client_, kServiceId, kConnectionOptions, discovery_listener_,
                 kCallback);
  // Establish connection.
  RequestConnection(&client_, kRemoteEndpointId, kConnectionRequestInfo,
                    kCallback);
  // Now, we can accept connection.
  AcceptConnection(&client_, kRemoteEndpointId, payload_listener_, kCallback);
}

TEST_F(ServiceControllerRouterTest, RejectConnectionCalled) {
  // Either Adviertisng, or Discovery should be ongoing.
  StartDiscovery(&client_, kServiceId, kConnectionOptions, discovery_listener_,
                 kCallback);
  // Establish connection.
  RequestConnection(&client_, kRemoteEndpointId, kConnectionRequestInfo,
                    kCallback);
  // Now, we can reject connection.
  RejectConnection(&client_, kRemoteEndpointId, kCallback);
}

TEST_F(ServiceControllerRouterTest, InitiateBandwidthUpgradeCalled) {
  // Either Adviertisng, or Discovery should be ongoing.
  StartDiscovery(&client_, kServiceId, kConnectionOptions, discovery_listener_,
                 kCallback);
  // Establish connection.
  RequestConnection(&client_, kRemoteEndpointId, kConnectionRequestInfo,
                    kCallback);
  // Now, we can accept connection.
  AcceptConnection(&client_, kRemoteEndpointId, payload_listener_, kCallback);
  // Now we can change connection bandwidth.
  InitiateBandwidthUpgrade(&client_, kRemoteEndpointId, kCallback);
}

TEST_F(ServiceControllerRouterTest, SendPayloadCalled) {
  // Either Adviertisng, or Discovery should be ongoing.
  StartDiscovery(&client_, kServiceId, kConnectionOptions, discovery_listener_,
                 kCallback);
  // Establish connection.
  RequestConnection(&client_, kRemoteEndpointId, kConnectionRequestInfo,
                    kCallback);
  // Now, we can accept connection.
  AcceptConnection(&client_, kRemoteEndpointId, payload_listener_, kCallback);
  // Now we can send payload.
  SendPayload(&client_, std::vector<std::string>{kRemoteEndpointId},
              Payload{ByteArray("data")}, kCallback);
}

TEST_F(ServiceControllerRouterTest, CancelPayloadCalled) {
  // Either Adviertisng, or Discovery should be ongoing.
  StartDiscovery(&client_, kServiceId, kConnectionOptions, discovery_listener_,
                 kCallback);
  // Establish connection.
  RequestConnection(&client_, kRemoteEndpointId, kConnectionRequestInfo,
                    kCallback);
  // Now, we can accept connection.
  AcceptConnection(&client_, kRemoteEndpointId, payload_listener_, kCallback);
  // We have to know payload id, before we can cancel payload transfer.
  // It is either after a call to SendPayload, or after receiving
  // PayloadProgress callback. Let's assume we have it, and proceed.
  CancelPayload(&client_, kPayloadId, kCallback);
}

TEST_F(ServiceControllerRouterTest, DisconnectFromEndpointCalled) {
  // Either Adviertisng, or Discovery should be ongoing.
  StartDiscovery(&client_, kServiceId, kConnectionOptions, discovery_listener_,
                 kCallback);
  // Establish connection.
  RequestConnection(&client_, kRemoteEndpointId, kConnectionRequestInfo,
                    kCallback);
  // Now, we can accept connection.
  AcceptConnection(&client_, kRemoteEndpointId, payload_listener_, kCallback);
  // We can disconnect at any time after RequestConnection.
  DisconnectFromEndpoint(&client_, kRemoteEndpointId, kCallback);
}

}  // namespace
}  // namespace connections
}  // namespace nearby
}  // namespace location