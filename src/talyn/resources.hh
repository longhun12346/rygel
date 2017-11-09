/* This Source Code Form is subject to the terms of the Mozilla Public
   License, v. 2.0. If a copy of the MPL was not distributed with this
   file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#pragma once

#include "../moya/libmoya.hh"

struct Resource {
    const char *url;
    ArrayRef<const uint8_t> data;
};
