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
#include <vector>

#include "gmock/gmock.h"
#include "protobuf-matchers/protocol-buffer-matchers.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/strings/escaping.h"
#include "third_party/nearby/presence/action_factory.h"
#include "third_party/nearby/presence/certificate_manager.h"
#include "third_party/nearby/presence/data_element.h"

namespace nearby {
namespace presence {

class MockCertificateManager : public CertificateManager {
 public:
  MOCK_METHOD(absl::StatusOr<std::string>, GetBaseEncryptedMetadataKey,
              (const PresenceIdentity& identity), (override));
  MOCK_METHOD(absl::StatusOr<std::string>, EncryptDataElements,
              (const PresenceIdentity& identity, absl::string_view salt,
               absl::string_view data_elements),
              (override));
};

namespace {

using ::testing::NiceMock;
using ::testing::Return;

TEST(AdvertisementFactory, CreateAdvertisementFromPrivateIdentity) {
  std::string salt = "AB";
  NiceMock<MockCertificateManager> certificate_manager;
  PresenceIdentity identity;
  std::vector<DataElement> data_elements;
  data_elements.emplace_back(DataElement::kActiveUnlock, "");
  Action action = ActionFactory::createAction(data_elements);
  BroadcastRequest request =
      BroadcastRequest(BasePresenceRequestBuilder(identity)
                           .SetSalt(salt)
                           .SetTxPower(5)
                           .SetAction(action));
  EXPECT_CALL(certificate_manager, GetBaseEncryptedMetadataKey(identity))
      .WillOnce(Return(absl::HexStringToBytes("1011121314151617181920212223")));
  EXPECT_CALL(
      certificate_manager,
      EncryptDataElements(identity, salt, absl::HexStringToBytes("1505260800")))
      .WillOnce(Return(absl::HexStringToBytes("5051525354")));

  AdvertisementFactory factory(certificate_manager);
  auto result = factory.CreateAdvertisement(request);

  EXPECT_TRUE(result.ok());
  auto service_data = result.value().service_data;
  EXPECT_EQ(service_data.size(), 1);
  for (auto i : service_data) {
    EXPECT_EQ(i.first.Get16BitAsString(), "FCF1");
    auto advertisement = absl::BytesToHexString(i.second.AsStringView());
    EXPECT_EQ(advertisement,
              "00204142e110111213141516171819202122235051525354");
  }
}

TEST(AdvertisementFactory,
     CreateAdvertisementFailsWhenCertificateManagerFails) {
  NiceMock<MockCertificateManager> certificate_manager;
  PresenceIdentity identity;
  std::vector<DataElement> data_elements;
  data_elements.emplace_back(DataElement::kActiveUnlock, "");
  Action action = ActionFactory::createAction(data_elements);
  BroadcastRequest request =
      BroadcastRequest(BasePresenceRequestBuilder(identity)
                           .SetSalt("AB")
                           .SetTxPower(5)
                           .SetAction(action));
  EXPECT_CALL(certificate_manager, GetBaseEncryptedMetadataKey(identity))
      .WillOnce(Return(absl::UnimplementedError(
          "GetBaseEncryptedMetadataKey not implemented")));

  AdvertisementFactory factory(certificate_manager);
  auto result = factory.CreateAdvertisement(request);

  EXPECT_FALSE(result.ok());
}
}  // namespace
}  // namespace presence
}  // namespace nearby
