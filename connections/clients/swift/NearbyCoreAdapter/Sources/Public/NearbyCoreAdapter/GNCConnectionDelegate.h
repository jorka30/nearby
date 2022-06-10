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

#import <Foundation/Foundation.h>

typedef NS_ENUM(NSInteger, GNCStatus);

@protocol GNCConnectionDelegate

- (void)connectedToEndpoint:(nonnull NSString *)endpointID
           withEndpointInfo:(nonnull NSData *)info
        authenticationToken:(nonnull NSString *)authenticationToken;

- (void)acceptedConnectionToEndpoint:(nonnull NSString *)endpointID;

- (void)rejectedConnectionToEndpoint:(nonnull NSString *)endpointID withStatus:(GNCStatus)status;

- (void)disconnectedFromEndpoint:(nonnull NSString *)endpointID;

// TODO
// OnBandwidthChanged(const std::string &endpoint_id, Medium medium)

@end
