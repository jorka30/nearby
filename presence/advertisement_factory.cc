// Copyright 2022 Google LLC
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

#include "third_party/nearby/presence/advertisement_factory.h"

#include <string>

#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "internal/platform/logging.h"
#include "internal/platform/uuid.h"

namespace nearby {
namespace presence {

constexpr uint8_t kBaseVersion = 0;
constexpr uint8_t kSaltFieldType = 0;
constexpr uint8_t kPrivateIdentityFieldType = 1;
constexpr uint8_t kTrustedIdentityFieldType = 2;
constexpr uint8_t kPublicIdentityFieldType = 3;
constexpr uint8_t kProvisionedIdentityFieldType = 4;
constexpr uint8_t kTxPowerFieldType = 5;
constexpr uint8_t kActionFieldType = 6;
constexpr unsigned kMaxDataElementLength = 15;
constexpr unsigned kMaxDataElementType = 15;
constexpr int kDataElementLengthShift = 4;
constexpr ::location::nearby::Uuid kServiceData(0xFCF1ULL << 32, 0);

absl::StatusOr<BleAdvertisementData> AdvertisementFactory::CreateAdvertisement(
    const BroadcastRequest& request) {
  BleAdvertisementData advert{};
  if (std::holds_alternative<BroadcastRequest::BasePresence>(request.variant)) {
    return CreateBaseNpAdvertisement(request);
  }
  return advert;
}

absl::StatusOr<BleAdvertisementData>
AdvertisementFactory::CreateBaseNpAdvertisement(
    const BroadcastRequest& request) {
  absl::Status result;
  auto presence = std::get<BroadcastRequest::BasePresence>(request.variant);
  BleAdvertisementData advert{};
  std::string payload;
  payload.push_back(kBaseVersion);
  if (!request.salt.empty()) {
    result = AddDataElement(payload, kSaltFieldType, request.salt);
    if (!result.ok()) {
      return result;
    }
  }
  auto identity =
      certificate_manager_.GetBaseEncryptedMetadataKey(presence.identity);
  if (!identity.ok()) {
    return identity.status();
  }
  uint8_t identity_type =
      GetIdentityFieldType(presence.identity.GetIdentityType());
  result = AddDataElement(payload, identity_type, identity.value());
  if (!result.ok()) {
    return result;
  }

  std::string data_elements;
  std::string tx_power = {static_cast<char>(request.tx_power)};
  result = AddDataElement(data_elements, kTxPowerFieldType, tx_power);
  if (!result.ok()) {
    return result;
  }
  std::string action = {static_cast<char>(presence.action.action >> 8),
                        static_cast<char>(presence.action.action)};
  result = AddDataElement(data_elements, kActionFieldType, action);
  if (!result.ok()) {
    return result;
  }
  if (!identity.value().empty()) {
    auto encrypted = certificate_manager_.EncryptDataElements(
        presence.identity, request.salt, data_elements);
    if (!encrypted.ok()) {
      return encrypted.status();
    }
    payload += encrypted.value();
  } else {
    payload += data_elements;
  }
  advert.service_data.insert(
      {kServiceData, location::nearby::ByteArray(payload)});
  return advert;
}

absl::Status AdvertisementFactory::AddDataElement(
    std::string& output, unsigned data_type, absl::string_view data_element) {
  auto header = CreateDataElementHeader(data_element.size(), data_type);
  if (!header.ok()) {
    NEARBY_LOG(WARNING, "Can't add Data element type: %d, length: %d",
               data_type, data_element.size());
    return header.status();
  }
  output.push_back(header.value());
  output.insert(output.end(), data_element.begin(), data_element.end());
  return absl::OkStatus();
}

absl::StatusOr<uint8_t> AdvertisementFactory::CreateDataElementHeader(
    size_t length, unsigned data_type) {
  if (length > kMaxDataElementLength) {
    return absl::InvalidArgumentError(
        absl::StrFormat("Unsupported Data Element length: %d", length));
  }
  if (data_type > kMaxDataElementType) {
    return absl::InvalidArgumentError(
        absl::StrFormat("Unsupported Data Element type: %d", data_type));
  }
  return (length << kDataElementLengthShift) | data_type;
}

uint8_t AdvertisementFactory::GetIdentityFieldType(
    PresenceIdentity::IdentityType type) {
  if (type == PresenceIdentity::IdentityType::kPrivate)
    return kPrivateIdentityFieldType;
  if (type == PresenceIdentity::IdentityType::kTrusted)
    return kTrustedIdentityFieldType;
  if (type == PresenceIdentity::IdentityType::kPublic)
    return kPublicIdentityFieldType;
  return kProvisionedIdentityFieldType;
}

}  // namespace presence
}  // namespace nearby
