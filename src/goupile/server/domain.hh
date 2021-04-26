// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License
// along with this program. If not, see https://www.gnu.org/licenses/.

#pragma once

#include "../../core/libcc/libcc.hh"
#include "instance.hh"
#include "../../core/libwrap/sqlite.hh"
#include "../../web/libhttp/libhttp.hh"

namespace RG {

extern const int DomainVersion;
extern const int MaxInstancesPerDomain;

struct DomainConfig {
    const char *database_filename = nullptr;
    const char *instances_directory = nullptr;
    const char *temp_directory = nullptr;
    const char *backup_directory = nullptr;

    uint8_t backup_key[32] = {}; // crypto_box_curve25519xsalsa20poly1305_PUBLICKEYBYTES
    bool enable_backups = false;
    bool sync_full = false;

    const char *demo_user = nullptr;

    const char *sms_sid = nullptr;
    const char *sms_token = nullptr;
    const char *sms_from = nullptr;

    const char *smtp_url = nullptr;
    const char *smtp_username = nullptr;
    const char *smtp_password = nullptr;

    // XXX: Restore http_Config designated initializers when MSVC ICE is fixed
    // https://developercommunity.visualstudio.com/content/problem/1238876/fatal-error-c1001-ice-with-ehsc.html
    http_Config http;
    int max_age = 900;

    bool Validate() const;

    const char *GetInstanceFileName(const char *key, Allocator *alloc) const;

    BlockAllocator str_alloc;
};

bool LoadConfig(StreamReader *st, DomainConfig *out_config);
bool LoadConfig(const char *filename, DomainConfig *out_config);

class DomainHolder {
    mutable std::shared_mutex mutex;
    HeapArray<InstanceHolder *> instances;
    HashTable<Span<const char>, InstanceHolder *> instances_map;

public:
    DomainConfig config = {};
    sq_Database db;

    ~DomainHolder() { Close(); }

    bool Open(const char *filename);
    void Close();

    bool Sync();
    bool Checkpoint();

    Span<InstanceHolder *> LockInstances();
    void UnlockInstances();
    Size CountInstances() const;

    InstanceHolder *Ref(Span<const char> key);
};

bool MigrateDomain(sq_Database *db, const char *instances_directory);
bool MigrateDomain(const DomainConfig &config);

}
