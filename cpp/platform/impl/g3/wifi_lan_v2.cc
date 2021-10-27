// Copyright 2020 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "platform/impl/g3/wifi_lan_v2.h"

#include <iostream>
#include <memory>
#include <string>
#include <utility>

#include "absl/strings/escaping.h"
#include "absl/strings/str_format.h"
#include "absl/synchronization/mutex.h"
#include "platform/api/wifi_lan_v2.h"
#include "platform/base/cancellation_flag_listener.h"
#include "platform/base/logging.h"
#include "platform/base/medium_environment.h"
#include "platform/base/nsd_service_info.h"

namespace location {
namespace nearby {
namespace g3 {

WifiLanSocketV2::~WifiLanSocketV2() {
  absl::MutexLock lock(&mutex_);
  DoClose();
}

void WifiLanSocketV2::Connect(WifiLanSocketV2& other) {
  absl::MutexLock lock(&mutex_);
  remote_socket_ = &other;
  input_ = other.output_;
}

InputStream& WifiLanSocketV2::GetInputStream() {
  auto* remote_socket = GetRemoteSocket();
  CHECK(remote_socket != nullptr);
  return remote_socket->GetLocalInputStream();
}

OutputStream& WifiLanSocketV2::GetOutputStream() {
  return GetLocalOutputStream();
}

WifiLanSocketV2* WifiLanSocketV2::GetRemoteSocket() {
  absl::MutexLock lock(&mutex_);
  return remote_socket_;
}

bool WifiLanSocketV2::IsConnected() const {
  absl::MutexLock lock(&mutex_);
  return IsConnectedLocked();
}

bool WifiLanSocketV2::IsClosed() const {
  absl::MutexLock lock(&mutex_);
  return closed_;
}

Exception WifiLanSocketV2::Close() {
  absl::MutexLock lock(&mutex_);
  DoClose();
  return {Exception::kSuccess};
}

void WifiLanSocketV2::DoClose() {
  if (!closed_) {
    remote_socket_ = nullptr;
    output_->GetOutputStream().Close();
    output_->GetInputStream().Close();
    input_->GetOutputStream().Close();
    input_->GetInputStream().Close();
    closed_ = true;
  }
}

bool WifiLanSocketV2::IsConnectedLocked() const { return input_ != nullptr; }

InputStream& WifiLanSocketV2::GetLocalInputStream() {
  absl::MutexLock lock(&mutex_);
  return output_->GetInputStream();
}

OutputStream& WifiLanSocketV2::GetLocalOutputStream() {
  absl::MutexLock lock(&mutex_);
  return output_->GetOutputStream();
}

std::string WifiLanServerSocketV2::GetName(const std::string& ip_address,
                                           int port) {
  std::string dot_delimited_string;
  if (!ip_address.empty()) {
    for (auto byte : ip_address) {
      if (!dot_delimited_string.empty())
        absl::StrAppend(&dot_delimited_string, ".");
      absl::StrAppend(&dot_delimited_string, absl::StrFormat("%d", byte));
    }
  }
  std::string out = absl::StrCat(dot_delimited_string, ":", port);
  return out;
}

std::unique_ptr<api::WifiLanSocketV2> WifiLanServerSocketV2::Accept() {
  absl::MutexLock lock(&mutex_);
  while (!closed_ && pending_sockets_.empty()) {
    cond_.Wait(&mutex_);
  }
  // whether or not we were running in the wait loop, return early if closed.
  if (closed_) return {};
  auto* remote_socket =
      pending_sockets_.extract(pending_sockets_.begin()).value();
  CHECK(remote_socket);

  auto local_socket = std::make_unique<WifiLanSocketV2>();
  local_socket->Connect(*remote_socket);
  remote_socket->Connect(*local_socket);
  cond_.SignalAll();
  return local_socket;
}

bool WifiLanServerSocketV2::Connect(WifiLanSocketV2& socket) {
  absl::MutexLock lock(&mutex_);
  if (closed_) return false;
  if (socket.IsConnected()) {
    NEARBY_LOGS(ERROR)
        << "Failed to connect to WifiLan server socket: already connected";
    return true;  // already connected.
  }
  // add client socket to the pending list
  pending_sockets_.insert(&socket);
  cond_.SignalAll();
  while (!socket.IsConnected()) {
    cond_.Wait(&mutex_);
    if (closed_) return false;
  }
  return true;
}

void WifiLanServerSocketV2::SetCloseNotifier(std::function<void()> notifier) {
  absl::MutexLock lock(&mutex_);
  close_notifier_ = std::move(notifier);
}

WifiLanServerSocketV2::~WifiLanServerSocketV2() {
  absl::MutexLock lock(&mutex_);
  DoClose();
}

Exception WifiLanServerSocketV2::Close() {
  absl::MutexLock lock(&mutex_);
  return DoClose();
}

Exception WifiLanServerSocketV2::DoClose() {
  bool should_notify = !closed_;
  closed_ = true;
  if (should_notify) {
    cond_.SignalAll();
    if (close_notifier_) {
      auto notifier = std::move(close_notifier_);
      mutex_.Unlock();
      // Notifier may contain calls to public API, and may cause deadlock, if
      // mutex_ is held during the call.
      notifier();
      mutex_.Lock();
    }
  }
  return {Exception::kSuccess};
}

WifiLanMediumV2::WifiLanMediumV2() {
  auto& env = MediumEnvironment::Instance();
  env.RegisterWifiLanMediumV2(*this);
}

WifiLanMediumV2::~WifiLanMediumV2() {
  auto& env = MediumEnvironment::Instance();
  env.UnregisterWifiLanMediumV2(*this);
}

bool WifiLanMediumV2::StartAdvertising(const NsdServiceInfo& nsd_service_info) {
  std::string service_type = nsd_service_info.GetServiceType();
  NEARBY_LOGS(INFO) << "G3 WifiLan StartAdvertising: nsd_service_info="
                    << &nsd_service_info
                    << ", service_name=" << nsd_service_info.GetServiceName()
                    << ", service_type=" << service_type;
  {
    absl::MutexLock lock(&mutex_);
    if (advertising_info_.Existed(service_type)) {
      NEARBY_LOGS(INFO)
          << "G3 WifiLan StartAdvertising: Can't start advertising because "
             "service_type="
          << service_type << ", has started already.";
      return false;
    }
  }
  auto& env = MediumEnvironment::Instance();
  env.UpdateWifiLanMediumV2ForAdvertising(*this, nsd_service_info,
                                          /*enabled=*/true);
  {
    absl::MutexLock lock(&mutex_);
    advertising_info_.Add(service_type);
  }
  return true;
}

bool WifiLanMediumV2::StopAdvertising(const NsdServiceInfo& nsd_service_info) {
  std::string service_type = nsd_service_info.GetServiceType();
  NEARBY_LOGS(INFO) << "G3 WifiLan StopAdvertising: nsd_service_info="
                    << &nsd_service_info
                    << ", service_name=" << nsd_service_info.GetServiceName()
                    << ", service_type=" << service_type;
  {
    absl::MutexLock lock(&mutex_);
    if (!advertising_info_.Existed(service_type)) {
      NEARBY_LOGS(INFO)
          << "G3 WifiLan StopAdvertising: Can't stop advertising because "
             "we never started advertising for service_type="
          << service_type;
      return false;
    }
    advertising_info_.Remove(service_type);
  }
  auto& env = MediumEnvironment::Instance();
  env.UpdateWifiLanMediumV2ForAdvertising(*this, nsd_service_info,
                                          /*enabled=*/false);
  return true;
}

bool WifiLanMediumV2::StartDiscovery(const std::string& service_type,
                                     DiscoveredServiceCallback callback) {
  NEARBY_LOGS(INFO) << "G3 WifiLan StartDiscovery: service_type="
                    << service_type;
  {
    absl::MutexLock lock(&mutex_);
    if (discovering_info_.Existed(service_type)) {
      NEARBY_LOGS(INFO)
          << "G3 WifiLan StartDiscovery: Can't start discovery because "
             "service_type="
          << service_type << " has started already.";
      return false;
    }
  }
  auto& env = MediumEnvironment::Instance();
  env.UpdateWifiLanMediumV2ForDiscovery(*this, std::move(callback),
                                        service_type, true);
  {
    absl::MutexLock lock(&mutex_);
    discovering_info_.Add(service_type);
  }
  return true;
}

bool WifiLanMediumV2::StopDiscovery(const std::string& service_type) {
  NEARBY_LOGS(INFO) << "G3 WifiLan StopDiscovery: service_type="
                    << service_type;
  {
    absl::MutexLock lock(&mutex_);
    if (!discovering_info_.Existed(service_type)) {
      NEARBY_LOGS(INFO)
          << "G3 WifiLan StopDiscovery: Can't stop discovering because we "
             "never started discovering.";
      return false;
    }
    discovering_info_.Remove(service_type);
  }
  auto& env = MediumEnvironment::Instance();
  env.UpdateWifiLanMediumV2ForDiscovery(*this, {}, service_type, false);
  return true;
}

std::unique_ptr<api::WifiLanSocketV2> WifiLanMediumV2::ConnectToService(
    const NsdServiceInfo& remote_service_info,
    CancellationFlag* cancellation_flag) {
  std::string service_type = remote_service_info.GetServiceType();
  NEARBY_LOGS(INFO) << "G3 WifiLan ConnectToService [self]: medium=" << this
                    << ", service_type=" << service_type;
  return ConnectToService(remote_service_info.GetIPAddress(),
                          remote_service_info.GetPort(), cancellation_flag);
}

std::unique_ptr<api::WifiLanSocketV2> WifiLanMediumV2::ConnectToService(
    const std::string& ip_address, int port,
    CancellationFlag* cancellation_flag) {
  NEARBY_LOGS(INFO) << "G3 WifiLan ConnectToService [self]: medium=" << this
                    << ", ip address + port="
                    << WifiLanServerSocketV2::GetName(ip_address, port);
  // First, find an instance of remote medium, that exposed this service.
  auto& env = MediumEnvironment::Instance();
  auto* remote_medium =
      static_cast<WifiLanMediumV2*>(env.GetWifiLanV2Medium(ip_address, port));
  if (!remote_medium) {
    return {};
  }

  WifiLanServerSocketV2* server_socket = nullptr;
  NEARBY_LOGS(INFO) << "G3 WifiLan ConnectToService [peer]: medium="
                    << remote_medium << ", remote ip address + port="
                    << WifiLanServerSocketV2::GetName(ip_address, port);
  // Then, find our server socket context in this medium.
  std::string socket_name = WifiLanServerSocketV2::GetName(ip_address, port);
  {
    absl::MutexLock medium_lock(&remote_medium->mutex_);
    auto item = remote_medium->server_sockets_.find(socket_name);
    server_socket = item != server_sockets_.end() ? item->second : nullptr;
    if (server_socket == nullptr) {
      NEARBY_LOGS(ERROR)
          << "G3 WifiLan Failed to find WifiLan Server socket: socket_name="
          << socket_name;
      return {};
    }
  }

  if (cancellation_flag->Cancelled()) {
    NEARBY_LOGS(ERROR) << "G3 WifiLan Connect: Has been cancelled: socket_name="
                       << socket_name;
    return {};
  }

  CancellationFlagListener listener(cancellation_flag, [&server_socket]() {
    NEARBY_LOGS(INFO) << "G3 WifiLan Cancel Connect.";
    if (server_socket != nullptr) {
      server_socket->Close();
    }
  });

  auto socket = std::make_unique<WifiLanSocketV2>();
  // Finally, Request to connect to this socket.
  if (!server_socket->Connect(*socket)) {
    NEARBY_LOGS(ERROR) << "G3 WifiLan Failed to connect to existing WifiLan "
                          "Server socket: name="
                       << socket_name;
    return {};
  }
  NEARBY_LOGS(INFO) << "G3 WifiLan ConnectToService: connected: socket="
                    << socket.get();
  return socket;
}

std::unique_ptr<api::WifiLanServerSocketV2> WifiLanMediumV2::ListenForService(
    int port) {
  auto& env = MediumEnvironment::Instance();
  auto server_socket = std::make_unique<WifiLanServerSocketV2>();
  server_socket->SetIPAddress(env.GetFakeIPAddress());
  server_socket->SetPort(port == 0 ? env.GetFakePort() : port);
  std::string socket_name = WifiLanServerSocketV2::GetName(
      server_socket->GetIPAddress(), server_socket->GetPort());
  server_socket->SetCloseNotifier([this, socket_name]() {
    absl::MutexLock lock(&mutex_);
    server_sockets_.erase(socket_name);
  });
  NEARBY_LOGS(INFO) << "G3 WifiLan Adding server socket: medium=" << this
                    << ", socket_name=" << socket_name;
  absl::MutexLock lock(&mutex_);
  server_sockets_.insert({socket_name, server_socket.get()});
  return server_socket;
}

}  // namespace g3
}  // namespace nearby
}  // namespace location
