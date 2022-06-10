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

@interface GNCPayload : NSObject

@property(nonatomic, readonly) int64_t identifier;

- (nonnull instancetype)init NS_UNAVAILABLE;

@end

@interface GNCBytesPayload : GNCPayload

@property(nonnull, nonatomic, readonly, copy) NSData *data;

- (nonnull instancetype)initWithData:(nonnull NSData *)data
                          identifier:(int64_t)identifier NS_DESIGNATED_INITIALIZER;

@end

@interface GNCStreamPayload : GNCPayload

@property(nonnull, nonatomic, readonly) NSInputStream *stream;

- (nonnull instancetype)initWithStream:(nonnull NSInputStream *)stream
                            identifier:(int64_t)identifier NS_DESIGNATED_INITIALIZER;

@end

@interface GNCFilePayload : GNCPayload

@property(nonnull, nonatomic, readonly, copy) NSURL *fileURL;

- (nonnull instancetype)initWithFileURL:(nonnull NSURL *)fileURL
                             identifier:(int64_t)identifier NS_DESIGNATED_INITIALIZER;

@end
