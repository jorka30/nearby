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

#import "connections/clients/swift/NearbyCoreAdapter/Sources/Public/NearbyCoreAdapter/GNCPayload.h"

#import <Foundation/Foundation.h>

#include "connections/payload.h"

#import "connections/clients/swift/NearbyCoreAdapter/Sources/GNCPayload+Internal.h"

using ::location::nearby::ByteArray;
using ::location::nearby::connections::Payload;

@implementation GNCPayload

- (instancetype)initWithIdentifier:(int64_t)identifier {
  self = [super init];
  if (self) {
    _identifier = identifier;
  }
  return self;
}

+ (GNCPayload *)fromCpp:(Payload)payload {
  int64_t payloadId = payload.GetId();
  switch (payload.GetType()) {
    case location::nearby::connections::PayloadType::kBytes: {
      ByteArray bytes = payload.AsBytes();
      NSData *payloadData = [NSData dataWithBytes:bytes.data() length:bytes.size()];
      return [[GNCBytesPayload alloc] initWithData:payloadData identifier:payloadId];
    }
    case location::nearby::connections::PayloadType::kFile:
    case location::nearby::connections::PayloadType::kStream:
    case location::nearby::connections::PayloadType::kUnknown:
      // TODO(bourdakos@): Implement
      return nil;
  }
}

- (Payload)toCpp {
  return Payload{};
}

@end

@implementation GNCBytesPayload

- (instancetype)initWithData:(NSData *)data identifier:(int64_t)identifier {
  self = [super initWithIdentifier:identifier];
  if (self) {
    _data = [data copy];
  }
  return self;
}

- (Payload)toCpp {
  return Payload{ByteArray((const char *)self.data.bytes, self.data.length)};
}

@end

@implementation GNCStreamPayload

- (instancetype)initWithStream:(NSInputStream *)stream identifier:(int64_t)identifier {
  self = [super initWithIdentifier:identifier];
  if (self) {
    _stream = stream;
  }
  return self;
}

// TODO: implement
//- (Payload)toCpp {
//}

@end

@implementation GNCFilePayload

- (instancetype)initWithFileURL:(NSURL *)fileURL identifier:(int64_t)identifier {
  self = [super initWithIdentifier:identifier];
  if (self) {
    _fileURL = [fileURL copy];
  }
  return self;
}

// TODO: implement
//- (Payload)toCpp {
//}

@end
