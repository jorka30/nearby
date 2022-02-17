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

#include "connections/implementation/mediums/ble_v2/discovered_peripheral_tracker.h"

#include <algorithm>
#include <string>
#include <utility>

#include "absl/strings/escaping.h"
#include "connections/implementation/mediums/ble_v2/advertisement_read_result.h"
#include "connections/implementation/mediums/ble_v2/ble_advertisement.h"
#include "connections/implementation/mediums/ble_v2/ble_advertisement_header.h"
#include "connections/implementation/mediums/ble_v2/ble_utils.h"
#include "connections/implementation/mediums/ble_v2/bloom_filter.h"
#include "internal/platform/mutex_lock.h"

namespace location {
namespace nearby {
namespace connections {
namespace mediums {

void DiscoveredPeripheralTracker::StartTracking(
    const std::string& service_id,
    DiscoveredPeripheralCallback discovered_peripheral_callback,
    const std::string& fast_advertisement_service_uuid) {
  MutexLock lock(&mutex_);

  ServiceIdInfo service_id_info = {
      .discovered_peripheral_callback =
          std::move(discovered_peripheral_callback),
      .lost_entity_tracker =
          absl::make_unique<LostEntityTracker<BleAdvertisement>>(),
      .fast_advertisement_service_uuid = fast_advertisement_service_uuid};

  // Replace if key is existed.
  service_id_infos_.insert_or_assign(service_id, std::move(service_id_info));

  // Clear all of the GATT read results. With this cleared, we will now attempt
  // to reconnect to every peripheral we see, giving us a chance to search for
  // the new service we're now tracking.
  // See the documentation of advertisementReadResult for more information.
  advertisement_read_results_.clear();

  // Remove stale data from any previous sessions.
  ClearDataForServiceId(service_id);
}

void DiscoveredPeripheralTracker::StopTracking(const std::string& service_id) {
  MutexLock lock(&mutex_);

  service_id_infos_.erase(service_id);
}

void DiscoveredPeripheralTracker::ProcessFoundBleAdvertisement(
    BleV2Peripheral peripheral,
    const ::location::nearby::api::ble_v2::BleAdvertisementData&
        advertisement_data,
    AdvertisementFetcher advertisement_fetcher) {
  MutexLock lock(&mutex_);

  std::vector<std::string> tracked_service_ids = GetTrackedServiceIds();
  if (tracked_service_ids.empty()) {
    NEARBY_LOGS(INFO) << "Ignoring BLE advertisement header because we are not "
                         "tracking any service IDs.";
    return;
  }

  if (!peripheral.IsValid() || advertisement_data.service_data.empty()) {
    NEARBY_LOGS(INFO)
        << "Ignoring BLE advertisement header because the peripheral is "
           "invalid or the given service data is empty.";
    return;
  }

  HandleAdvertisement(peripheral, advertisement_data);
}

void DiscoveredPeripheralTracker::ProcessLostGattAdvertisements() {}

void DiscoveredPeripheralTracker::ClearDataForServiceId(
    const std::string& service_id) {
  for (const auto& it : gatt_advertisement_infos_) {
    const GattAdvertisementInfo gatt_advertisement_info = it.second;
    if (gatt_advertisement_info.service_id != service_id) {
      continue;
    }
    ClearGattAdvertisement(it.first);
  }
}

void DiscoveredPeripheralTracker::ClearGattAdvertisement(
    const BleAdvertisement& gatt_advertisement) {
  const auto& gai_it = gatt_advertisement_infos_.find(gatt_advertisement);
  if (gai_it == gatt_advertisement_infos_.end()) {
    return;
  }
  auto item = gatt_advertisement_infos_.extract(gai_it);
  GattAdvertisementInfo& gatt_advertisement_info = item.mapped();

  const auto ga_it =
      gatt_advertisements_.find(gatt_advertisement_info.advertisement_header);
  if (ga_it != gatt_advertisements_.end()) {
    // Remove the GATT advertisement from the advertisement header it's
    // associated with.
    BleAdvertisementSet& gatt_advertisement_set = ga_it->second;
    gatt_advertisement_set.erase(gatt_advertisement);

    // Unconditionally remove the header from advertisement_read_results_ so we
    // can attempt to reread the GATT advertisement if they return.
    advertisement_read_results_.erase(
        gatt_advertisement_info.advertisement_header);

    // If there are no more tracked GATT advertisements under this header, go
    // ahead and remove it from gatt_advertisements_.
    if (gatt_advertisement_set.empty()) {
      gatt_advertisements_.erase(ga_it);
    } else {  // or update the gatt_advertisement_set.
      gatt_advertisements_[gatt_advertisement_info.advertisement_header] =
          gatt_advertisement_set;
    }
  }
}

std::vector<std::string> DiscoveredPeripheralTracker::GetTrackedServiceIds() {
  std::vector<std::string> tracked_service_ids;
  std::transform(service_id_infos_.begin(), service_id_infos_.end(),
                 std::back_inserter(tracked_service_ids),
                 [](auto& kv) { return kv.first; });
  return tracked_service_ids;
}

void DiscoveredPeripheralTracker::HandleAdvertisement(
    BleV2Peripheral& peripheral,
    const ::location::nearby::api::ble_v2::BleAdvertisementData&
        advertisement_data) {
  ByteArray advertisement_bytes =
      ExtractInterestedAdvertisementBytes(advertisement_data);
  if (advertisement_bytes.Empty()) {
    return;
  }

  // The UUID that the Fast/Regular Advertisement was found on. For regular
  // advertisement, it's always kCopresenceServiceUuid. For fast
  // advertisement, we may have below 2 UUIDs: 1. kCopresenceServiceUuid 2.
  // Caller UUID.
  // First filter out kCopresenceServiceUuid and see if any Caller UUID
  // existed; if not then just take the kCopresenceServiceUuid as
  // |service_uuid|.
  std::vector<std::string> extracted_uuids;
  // Filter out kCoprsence service uuid.
  std::remove_copy_if(advertisement_data.service_uuids.begin(),
                      advertisement_data.service_uuids.end(),
                      std::back_inserter(extracted_uuids),
                      [](const std::string& advertisement_data_service_uuid) {
                        return advertisement_data_service_uuid ==
                               bleutils::kCopresenceServiceUuid;
                      });
  std::string service_uuid = std::string(bleutils::kCopresenceServiceUuid);
  if (!extracted_uuids.empty()) {
    service_uuid = extracted_uuids.front();
  }

  // Create a header tied to this fast advertisement. This helps us track the
  // advertisement when reporting it as lost or connecting.
  BleAdvertisementHeader advertisement_header =
      CreateAdvertisementHeader(advertisement_bytes);

  // Process the fast advertisement like we would a GATT advertisement and
  // insert a placeholder AdvertisementReadResult.
  advertisement_read_results_.insert(
      {advertisement_header, absl::make_unique<AdvertisementReadResult>()});

  BleAdvertisementHeader new_advertisement_header = HandleRawGattAdvertisements(
      advertisement_header, {&advertisement_bytes}, service_uuid);
  UpdateCommonStateForFoundBleAdvertisement(new_advertisement_header,
                                            /*mac_address=*/peripheral.GetId());
}

ByteArray DiscoveredPeripheralTracker::ExtractInterestedAdvertisementBytes(
    const ::location::nearby::api::ble_v2::BleAdvertisementData&
        advertisement_data) {
  ByteArray advertisement_bytes = {};
  // Iterate through all tracked service IDs to see if any of their fast
  // advertisements are contained within this BLE advertisement.
  for (const auto& service_id : GetTrackedServiceIds()) {
    const auto sdi_it = service_id_infos_.find(service_id);
    if (sdi_it != service_id_infos_.end()) {
      const ServiceIdInfo& service_id_info = sdi_it->second;
      // First, check if a service UUID is tied to this service ID.
      if (!service_id_info.fast_advertisement_service_uuid.empty()) {
        // Then, check if there's service data for this fast advertisement
        // service UUID. If so, we can short-circuit since all BLE
        // advertisements can contain at most ONE fast advertisement.
        const auto sd_it = advertisement_data.service_data.find(
            service_id_info.fast_advertisement_service_uuid);
        if (sd_it != advertisement_data.service_data.end()) {
          advertisement_bytes = sd_it->second;
          break;
        }
      }
    }
  }
  return advertisement_bytes;
}

BleAdvertisementHeader DiscoveredPeripheralTracker::CreateAdvertisementHeader(
    const ByteArray& advertisement_bytes) {
  // Our end goal is to have a fully zeroed-out byte array of the correct
  // length representing an empty bloom filter.
  BloomFilter bloom_filter(
      std::make_unique<BitSetImpl<
          BleAdvertisementHeader::kServiceIdBloomFilterByteLength>>());

  return BleAdvertisementHeader(
      BleAdvertisementHeader::Version::kV2, /*extended_advertisement=*/false,
      /*num_slots=*/1, ByteArray(bloom_filter),
      bleutils::GenerateAdvertisementHash(advertisement_bytes),
      /*psm=*/BleAdvertisementHeader::kDefaultPsmValue);
}

BleAdvertisementHeader DiscoveredPeripheralTracker::HandleRawGattAdvertisements(
    const BleAdvertisementHeader& advertisement_header,
    const std::vector<const ByteArray*>& gatt_advertisement_bytes_list,
    const std::string& service_uuid) {
  absl::flat_hash_map<std::string, BleAdvertisement>
      parsed_gatt_advertisements = ParseRawGattAdvertisements(
          gatt_advertisement_bytes_list, service_uuid);

  // Update state for each GATT advertisement.
  BleAdvertisementSet ble_advertisement_set;
  BleAdvertisementHeader new_advertisement_header = advertisement_header;
  for (const auto& item : parsed_gatt_advertisements) {
    const std::string& service_id = item.first;
    const BleAdvertisement& gatt_advertisement = item.second;
    const auto gai_it = gatt_advertisement_infos_.find(gatt_advertisement);
    BleAdvertisementHeader old_advertisement_header;
    if (gai_it != gatt_advertisement_infos_.end()) {
      old_advertisement_header = gai_it->second.advertisement_header;
    }

    ble_advertisement_set.insert(gatt_advertisement);

    int new_psm = advertisement_header.GetPsm();
    if (gatt_advertisement.GetPsm() !=
            BleAdvertisementHeader::kDefaultPsmValue &&
        gatt_advertisement.GetPsm() != new_psm) {
      new_psm = gatt_advertisement.GetPsm();
      // Also set header with new psm value to compare next time.
      new_advertisement_header.SetPsm(new_psm);
    }

    // If the device received first fast advertisement is legacy one after then
    // received extended one, should replace legacy with extended one which has
    // psm value.
    if (!old_advertisement_header.IsValid() ||
        ShouldNotifyForNewPsm(old_advertisement_header.GetPsm(), new_psm)) {
      // The GATT advertisement has never been seen before. Report it up to the
      // client.
      const auto sii_it = service_id_infos_.find(service_id);
      if (sii_it == service_id_infos_.end()) {
        NEARBY_LOGS(WARNING) << "HandleRawGattAdvertisements, failed to find "
                                "callback for service_id="
                             << service_id;
        continue;
      }
      DiscoveredPeripheralCallback discovered_peripheral_callback =
          sii_it->second.discovered_peripheral_callback;
      BlePeripheral discovered_ble_peripheral =
          GenerateBlePeripheral(gatt_advertisement, new_psm);
      if (discovered_ble_peripheral.IsValid()) {
        discovered_peripheral_callback.peripheral_discovered_cb(
            discovered_ble_peripheral, service_id, gatt_advertisement.GetData(),
            gatt_advertisement.IsFastAdvertisement());
      }
    } else if (old_advertisement_header.GetPsm() !=
                   BleAdvertisementHeader::kDefaultPsmValue &&
               new_advertisement_header.GetPsm() ==
                   BleAdvertisementHeader::kDefaultPsmValue) {
      // Don't replace it in advertisementHeaders if this one without PSM but
      // older has, it's the case that we received extended fast advertisement
      // after then received legacy one.
      continue;
    } else if (ShouldRemoveHeader(old_advertisement_header,
                                  new_advertisement_header)) {
      // The GATT advertisement has been seen on a different advertisement
      // header. Remove info about the old advertisement header since it's stale
      // now.
      advertisement_read_results_.erase(old_advertisement_header);
      gatt_advertisements_.erase(old_advertisement_header);
    }

    GattAdvertisementInfo gatt_advertisement_info = {
        .service_id = service_id,
        .advertisement_header = new_advertisement_header,
        .mac_address = {}};
    gatt_advertisement_infos_.insert_or_assign(gatt_advertisement,
                                               gatt_advertisement_info);
  }
  // Insert the list of read GATT advertisements for this advertisement
  // header.
  gatt_advertisements_.insert(
      {new_advertisement_header, ble_advertisement_set});
  return new_advertisement_header;
}

absl::flat_hash_map<std::string, BleAdvertisement>
DiscoveredPeripheralTracker::ParseRawGattAdvertisements(
    const std::vector<const ByteArray*>& gatt_advertisement_bytes_list,
    const std::string& service_uuid) {
  std::vector<std::string> tracked_service_ids = GetTrackedServiceIds();
  absl::flat_hash_map<std::string, BleAdvertisement>
      parsed_gatt_advertisements = {};

  for (const auto gatt_advertisement_bytes : gatt_advertisement_bytes_list) {
    // First, parse the raw bytes into a BleAdvertisement.
    BleAdvertisement gatt_advertisement(*gatt_advertisement_bytes);
    if (!gatt_advertisement.IsValid()) {
      NEARBY_LOGS(INFO) << "Unable to parse raw GATT advertisement:"
                        << absl::BytesToHexString(
                               gatt_advertisement_bytes->data());
      continue;
    }

    // Make sure the advertisement belongs to a service ID we're tracking.
    for (const auto& service_id : tracked_service_ids) {
      // If we already found a higher version advertisement for this service ID,
      // there's no point in comparing this advertisement against it.
      const auto pga_it = parsed_gatt_advertisements.find(service_id);
      if (pga_it != parsed_gatt_advertisements.end()) {
        if (pga_it->second.GetVersion() > gatt_advertisement.GetVersion()) {
          continue;
        }
      }

      // service_id_hash is null here (mediums advertisement) because we already
      // have a UUID in the fast advertisement.
      if (gatt_advertisement.IsFastAdvertisement() && !service_uuid.empty()) {
        const auto sii_it = service_id_infos_.find(service_id);
        if (sii_it != service_id_infos_.end()) {
          if (sii_it->second.fast_advertisement_service_uuid == service_uuid) {
            NEARBY_LOGS(INFO)
                << "This GATT advertisement:"
                << absl::BytesToHexString(gatt_advertisement_bytes->data())
                << " is a fast advertisement and matched UUID=" << service_uuid
                << " in a map with service_id=" << service_id;
            parsed_gatt_advertisements.insert({service_id, gatt_advertisement});
          }
        }
        continue;
      }

      // Map the service ID to the advertisement if theservice_id_hash match.
      if (bleutils::GenerateServiceIdHash(service_id) ==
          gatt_advertisement.GetServiceIdHash()) {
        NEARBY_LOGS(INFO) << "Matched service_id=" << service_id
                          << " to GATT advertisement="
                          << absl::BytesToHexString(
                                 gatt_advertisement_bytes->data());
        parsed_gatt_advertisements.insert({service_id, gatt_advertisement});
        break;
      }
    }
  }

  return parsed_gatt_advertisements;
}

bool DiscoveredPeripheralTracker::ShouldRemoveHeader(
    const BleAdvertisementHeader& old_advertisement_header,
    const BleAdvertisementHeader& new_advertisement_header) {
  if (old_advertisement_header == new_advertisement_header) {
    return false;
  }

  // We received the physical from legacy advertisement and create a mock one
  // when receive a regular advertisement from extended advertisements. Avoid to
  // remove the physical header for the new incoming regular extended
  // advertisement. Otherwise, it make the device to fetch advertisement when
  // received a physical header again.
  // TODO(b/213835576) : Implement API to fetch the support for extended
  // advertisement from platform impl.
  bool is_extended_advertisement_available = false;
  if (is_extended_advertisement_available) {
    if (!IsMockedAdvertisementHeader(old_advertisement_header) &&
        IsMockedAdvertisementHeader(new_advertisement_header)) {
      return false;
    }
  }

  return true;
}

bool DiscoveredPeripheralTracker::IsMockedAdvertisementHeader(
    const BleAdvertisementHeader& advertisement_header) {
  // Do not count advertisementHash and psm value here, for L2CAP feature, the
  // regular advertisement has different value, it will include PSM value if
  // received it from extended advertisement protocol and it will not has PSM
  // value if it fetcted from GATT connection.
  BloomFilter bloom_filter(
      std::make_unique<BitSetImpl<
          BleAdvertisementHeader::kServiceIdBloomFilterByteLength>>());
  return advertisement_header.GetVersion() ==
             BleAdvertisementHeader::Version::kV2 &&
         advertisement_header.GetNumSlots() == 1 &&
         advertisement_header.GetServiceIdBloomFilter() ==
             ByteArray(bloom_filter);
}

void DiscoveredPeripheralTracker::UpdateCommonStateForFoundBleAdvertisement(
    const BleAdvertisementHeader& advertisement_header,
    const std::string& mac_address) {
  const auto ga_it = gatt_advertisements_.find(advertisement_header);
  if (ga_it == gatt_advertisements_.end()) {
    NEARBY_LOGS(INFO)
        << "No GATT advertisements found for advertisement header="
        << absl::BytesToHexString(ByteArray(advertisement_header).data());
    return;
  }

  BleAdvertisementSet saved_gatt_advertisements = ga_it->second;
  for (const auto& gatt_advertisement : saved_gatt_advertisements) {
    const auto gai_it = gatt_advertisement_infos_.find(gatt_advertisement);
    if (gai_it == gatt_advertisement_infos_.end()) {
      continue;
    }
    GattAdvertisementInfo& gatt_advertisement_info = gai_it->second;

    // Make sure the stored GATT advertisement is still being tracked.
    std::vector<std::string> tracked_service_ids = GetTrackedServiceIds();
    if (std::find(tracked_service_ids.begin(), tracked_service_ids.end(),
                  gatt_advertisement_info.service_id) ==
        tracked_service_ids.end()) {
      continue;
    }
    const auto sii_it =
        service_id_infos_.find(gatt_advertisement_info.service_id);
    if (sii_it != service_id_infos_.end()) {
      auto* lost_entity_tracker = sii_it->second.lost_entity_tracker.get();
      if (lost_entity_tracker == nullptr) {
        NEARBY_LOGS(WARNING) << "UpdateCommonStateForFoundBleAdvertisement, "
                                "failed to find entity "
                                "tracker for service_id="
                             << gatt_advertisement_info.service_id;
        continue;
      }
      lost_entity_tracker->RecordFoundEntity(gatt_advertisement);

      gatt_advertisement_info.mac_address = mac_address;
      gatt_advertisement_infos_[gatt_advertisement] = gatt_advertisement_info;
    }
  }
}

BlePeripheral DiscoveredPeripheralTracker::GenerateBlePeripheral(
    const BleAdvertisement& gatt_advertisement, int psm) {
  return BlePeripheral(ByteArray(gatt_advertisement), psm);
}

}  // namespace mediums
}  // namespace connections
}  // namespace nearby
}  // namespace location
