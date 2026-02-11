// Copyright (c) 2025 HIGH CODE LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.


#include "tutti_frutti_spam.h"
#include "android_spam.h"
#include "apple_juice_spam.h"
#include "samsung_spam.h"
#include "sour_apple_spam.h"
#include "swift_pair_spam.h"

static int tutti_index = 0;

int generate_tutti_frutti_payload(uint8_t *buffer, size_t max_len) {
  int ret = 0;
  switch (tutti_index) {
    case 0: ret = generate_android_payload(buffer, max_len); break;
    case 1: ret = generate_samsung_payload(buffer, max_len); break;
    case 2: ret = generate_swift_pair_payload(buffer, max_len); break;
    case 3: ret = generate_sour_apple_payload(buffer, max_len); break;
    case 4: ret = generate_apple_juice_payload(buffer, max_len); break;
    default: ret = generate_apple_juice_payload(buffer, max_len); break;
  }

  tutti_index++;
  if (tutti_index > 4) tutti_index = 0;

  return ret;
}
