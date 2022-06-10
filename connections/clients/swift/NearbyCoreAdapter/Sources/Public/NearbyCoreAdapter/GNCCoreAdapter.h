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

@class GNCAdvertisingOptions;
@class GNCConnectionOptions;
@class GNCDiscoveryOptions;
@class GNCPayload;

@protocol GNCConnectionDelegate;
@protocol GNCDiscoveryDelegate;
@protocol GNCPayloadDelegate;

@interface GNCCoreAdapter : NSObject

- (void)startAdvertisingAsService:(nonnull NSString *)serviceID
                     endpointInfo:(nonnull NSData *)endpointInfo
                          options:(nullable GNCAdvertisingOptions *)
                                      advertisingOptions  // TODO: This shouldn't be nullable
                         delegate:(nullable id<GNCConnectionDelegate>)delegate
            withCompletionHandler:(nullable void (^)(NSError *_Nullable error))completionHandler;

- (void)stopAdvertisingWithCompletionHandler:
    (nullable void (^)(NSError *_Nonnull error))completionHandler;

- (void)startDiscoveryAsService:(nonnull NSString *)serviceID
                        options:(nullable GNCDiscoveryOptions *)
                                    discoveryOptions  // TODO: This shouldn't be nullable
                       delegate:(nullable id<GNCDiscoveryDelegate>)delegate
          withCompletionHandler:(nullable void (^)(NSError *_Nullable error))completionHandler;

- (void)stopDiscoveryWithCompletionHandler:
    (nullable void (^)(NSError *_Nonnull error))completionHandler;

- (void)requestConnectionToEndpoint:(nonnull NSString *)endpointID
                       endpointInfo:(nonnull NSData *)endpointInfo
                            options:(nullable GNCConnectionOptions *)
                                        connectionOptions  // TODO: This shouldn't be nullable
                           delegate:(nullable id<GNCConnectionDelegate>)delegate
              withCompletionHandler:(nullable void (^)(NSError *_Nullable error))completionHandler;

- (void)acceptConnectionRequestFromEndpoint:(nonnull NSString *)endpointID
                                   delegate:(nullable id<GNCPayloadDelegate>)delegate
                      withCompletionHandler:
                          (nullable void (^)(NSError *_Nullable error))completionHandler;

- (void)rejectConnectionRequestFromEndpoint:(nonnull NSString *)endpointID
                      withCompletionHandler:
                          (nullable void (^)(NSError *_Nullable error))completionHandler;

- (void)sendPayload:(nonnull GNCPayload *)payload
              toEndpoints:(nonnull NSArray<NSString *> *)endpointIDs
    withCompletionHandler:(nullable void (^)(NSError *_Nullable error))completionHandler;

- (void)cancelPayload:(int64_t)payloadID
    withCompletionHandler:(nullable void (^)(NSError *_Nullable error))completionHandler;

- (void)disconnectFromEndpoint:(nonnull NSString *)endpointID
         withCompletionHandler:(nullable void (^)(NSError *_Nullable error))completionHandler;

- (void)stopAllEndpointsWithCompletionHandler:
    (nullable void (^)(NSError *_Nullable error))completionHandler;

- (void)initiateBandwidthUpgrade:(nonnull NSString *)endpointID
           withCompletionHandler:(nullable void (^)(NSError *_Nullable error))completionHandler;

@property(nonnull, nonatomic, readonly, copy) NSString *localEndpointID;

@end
