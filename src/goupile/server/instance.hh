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
#include "../../core/libwrap/sqlite.hh"
#include "../../web/libhttp/libhttp.hh"

namespace RG {

extern const int InstanceVersion;

enum SyncMode {
    Offline,
    Online,
    Mirror
};
static const char *const SyncModeNames[] = {
    "Offline",
    "Online",
    "Mirror"
};

class InstanceHolder {
    // Managed by DomainHolder

    mutable std::atomic_int refcount {0};
    std::atomic_bool reload {false};
    bool unload = false;

public:
    struct SlaveInfo {
        const char *key;
        const char *title;
    };

    int64_t unique = -1;
    Span<const char> key = {};
    const char *filename = nullptr;
    sq_Database db;

    InstanceHolder *master = nullptr;
    HeapArray<SlaveInfo> slaves;

    struct {
        const char *title = nullptr;
        bool use_offline = false;
        SyncMode sync_mode = SyncMode::Offline;
        int max_file_size = (int)Megabytes(8);
        const char *shared_key = nullptr;
        const char *backup_key = nullptr;
    } config;

    BlockAllocator str_alloc;

    ~InstanceHolder() { Close(); }

    bool Open(int64_t uniquen, const char *key, const char *filename,
              InstanceHolder *master, bool sync_full);
    bool Validate();
    void Close();

    bool Checkpoint();

    void Reload() { master->reload = true; }
    void Ref() const { master->refcount++; }
    void Unref() const { master->refcount--; }

    RG_HASHTABLE_HANDLER(InstanceHolder, key);

    friend class DomainHolder;
};

bool MigrateInstance(sq_Database *db);
bool MigrateInstance(const char *filename);

}
