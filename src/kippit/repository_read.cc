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
// along with this program. If not, see <https://www.gnu.org/licenses/>.

#include "src/core/libcc/libcc.hh"
#include "disk.hh"
#include "repository.hh"

#ifdef _WIN32
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
    #include <io.h>
#else
    #include <unistd.h>
#endif

namespace RG {

enum class ExtractFlag {
    AllowSeparators = 1 << 0,
    FlattenName = 1 << 1
};

class GetContext {
    kt_Disk *disk;
    unsigned int flags;

    Async tasks;

    std::atomic<int64_t> stat_len {0};

public:
    GetContext(kt_Disk *disk, int threads);

    bool ExtractEntries(kt_ObjectType type, Span<const uint8_t> entries, unsigned int flags, const char *dest_dirname);
    bool GetFile(const kt_ID &id, kt_ObjectType type, Span<const uint8_t> file_obj, const char *dest_filename);

    bool Sync() { return tasks.Sync(); }

    int64_t GetLen() const { return stat_len; }
};

GetContext::GetContext(kt_Disk *disk, int threads)
    : disk(disk), tasks(threads)
{
}

#ifdef _WIN32

static bool ReserveFile(int fd, const char *filename, int64_t len)
{
    HANDLE h = (HANDLE)_get_osfhandle(fd);

    LARGE_INTEGER prev_pos = {};
    if (!SetFilePointerEx(h, prev_pos, &prev_pos, FILE_CURRENT)) {
        LogError("Failed to resize file '%1': %2", filename, GetWin32ErrorString());
        return false;
    }
    RG_DEFER { SetFilePointerEx(h, prev_pos, nullptr, FILE_BEGIN); };

    if (!SetFilePointerEx(h, {.QuadPart = len}, nullptr, FILE_BEGIN)) {
        LogError("Failed to resize file '%1': %2", filename, GetWin32ErrorString());
        return false;
    }
    if (!SetEndOfFile(h)) {
        LogError("Failed to resize file '%1': %2", filename, GetWin32ErrorString());
        return false;
    }

    return true;
}

static bool WriteAt(int fd, const char *filename, int64_t offset, Span<const uint8_t> buf)
{
    RG_ASSERT(buf.len < UINT32_MAX);

    HANDLE h = (HANDLE)_get_osfhandle(fd);

    while (buf.len) {
        OVERLAPPED ov = {};
        DWORD written = 0;

        ov.OffsetHigh = (uint32_t)((offset & 0xFFFFFFFF00000000ll) >> 32);
        ov.Offset = (uint32_t)(offset & 0xFFFFFFFFll);

        if (!WriteFile(h, buf.ptr, (DWORD)buf.len, &written, &ov)) {
            LogError("Failed to write to '%1': %2", filename, GetWin32ErrorString());
            return false;
        }

        offset += (Size)written;
        buf.ptr += (Size)written;
        buf.len -= (Size)written;
    }

    return true;
}

static bool CreateSymbolicLink(const char *filename, const char *target)
{
    LogWarning("Ignoring symbolic link '%1' to '%2'", filename, target);
    return true;
}

#else

static bool ReserveFile(int fd, const char *filename, int64_t len)
{
    if (ftruncate(fd, len) < 0) {
        LogError("Failed to reserve file '%1': %2", filename, strerror(errno));
        return false;
    }

    return true;
}

static bool WriteAt(int fd, const char *filename, int64_t offset, Span<const uint8_t> buf)
{
    while (buf.len) {
        Size written = RG_POSIX_RESTART_EINTR(pwrite(fd, buf.ptr, buf.len, (off_t)offset), < 0);

        if (written < 0) {
            LogError("Failed to write to '%1': %2", filename, strerror(errno));
            return false;
        }

        offset += written;
        buf.ptr += written;
        buf.len -= written;
    }

    return true;
}

static bool CreateSymbolicLink(const char *filename, const char *target)
{
    if (symlink(target, filename) < 0) {
        LogError("Failed to create symbolic link '%1': %2", filename, strerror(errno));
        return false;
    }

    return true;
}

#endif

bool GetContext::ExtractEntries(kt_ObjectType type, Span<const uint8_t> entries,
                                unsigned int flags, const char *dest_dirname)
{
    // XXX: Make sure each path does not clobber a previous one

    std::shared_ptr<BlockAllocator> temp_alloc = std::make_shared<BlockAllocator>();

    for (Size offset = 0; offset < entries.len;) {
        const kt_FileEntry *entry = (const kt_FileEntry *)(entries.ptr + offset);
        const char *name = entry->name;

        if (type == kt_ObjectType::Directory1 || type == kt_ObjectType::Snapshot1) {
            name = (const char *)entry + 45;

            Size name_len = (Size)strnlen(name, entries.end() - (const uint8_t *)name);
            Size entry_len = 45 + name_len + 1;

            offset += entry_len;
        } else if (type == kt_ObjectType::Directory2 || type == kt_ObjectType::Snapshot2) {
            Size name_len = (Size)strnlen(name, entries.end() - (const uint8_t *)name);
            Size entry_len = RG_SIZE(kt_FileEntry) + name_len + 1;

            offset += entry_len;
        } else {
            RG_UNREACHABLE();
        }

        // Sanity checks
        if (offset > entries.len) {
            LogError("Malformed entry in directory object");
            return false;
        }
        if (entry->kind != (int8_t)kt_FileEntry::Kind::Directory &&
                entry->kind != (int8_t)kt_FileEntry::Kind::File &&
                entry->kind != (int8_t)kt_FileEntry::Kind::Link) {
            LogError("Unknown file kind 0x%1", FmtHex((unsigned int)entry->kind));
            return false;
        }
        if (!name[0] || PathContainsDotDot(name)) {
            LogError("Unsafe file name '%1'", name);
            return false;
        }
        if (PathIsAbsolute(name)) {
            LogError("Unsafe file name '%1'", name);
            return false;
        }
        if (!(flags & (int)ExtractFlag::AllowSeparators) && strpbrk(name, RG_PATH_SEPARATORS)) {
            LogError("Unsafe file name '%1'", name);
            return false;
        }

        kt_ID entry_id = entry->id;
        int8_t entry_kind = entry->kind;

        const char *entry_filename;
        if (flags & (int)ExtractFlag::FlattenName) {
            entry_filename = Fmt(temp_alloc.get(), "%1%/%2", dest_dirname, SplitStrReverse(name, '/')).ptr;
        } else {
            entry_filename = Fmt(temp_alloc.get(), "%1%/%2", dest_dirname, name).ptr;

            if ((flags & (int)ExtractFlag::AllowSeparators) && !EnsureDirectoryExists(entry_filename))
                return false;
        }

        tasks.Run([temp_alloc, entry_id, entry_kind, entry_filename, this]() {
            kt_ObjectType entry_type;
            HeapArray<uint8_t> entry_obj;
            if (!disk->ReadObject(entry_id, &entry_type, &entry_obj))
                return false;

            switch (entry_kind) {
                case (int8_t)kt_FileEntry::Kind::Directory: {
                    if (entry_type != kt_ObjectType::Directory1 &&
                            entry_type != kt_ObjectType::Directory2) {
                        LogError("Object '%1' is not a directory", entry_id);
                        return false;
                    }

                    if (!MakeDirectory(entry_filename, false))
                        return false;

                    if (!ExtractEntries(entry_type, entry_obj, 0, entry_filename))
                        return false;
                } break;
                case (int8_t)kt_FileEntry::Kind::File: {
                    if (entry_type != kt_ObjectType::File && entry_type != kt_ObjectType::Chunk) {
                        LogError("Object '%1' is not a file", entry_id);
                        return false;
                    }

                    if (!GetFile(entry_id, entry_type, entry_obj, entry_filename))
                        return false;
                } break;
                case (int8_t)kt_FileEntry::Kind::Link: {
                    if (entry_type != kt_ObjectType::Link) {
                        LogError("Object '%1' is not a link", entry_id);
                        return false;
                    }

                    // NUL terminate the path
                    entry_obj.Append(0);

                    if (!CreateSymbolicLink(entry_filename, (const char *)entry_obj.ptr))
                        return false;
                } break;

                default: {
                    LogError("Unknown file kind 0x%1", FmtHex((unsigned int)entry_kind));
                    return false;
                } break;
            }

            return true;
        });
    }

    return true;
}

bool GetContext::GetFile(const kt_ID &id, kt_ObjectType type, Span<const uint8_t> file_obj, const char *dest_filename)
{
    RG_ASSERT(type == kt_ObjectType::File || type == kt_ObjectType::Chunk);

    // Open destination file
    int fd = OpenDescriptor(dest_filename, (int)OpenFlag::Write);
    if (fd < 0)
        return false;
    RG_DEFER { close(fd); };

    int64_t file_len = -1;
    switch (type) {
        case kt_ObjectType::File: {
            if (file_obj.len % RG_SIZE(kt_ChunkEntry) != RG_SIZE(int64_t)) {
                LogError("Malformed file object '%1'", id);
                return false;
            }

            file_obj.len -= RG_SIZE(int64_t);

            // Prepare destination file
            file_len = LittleEndian(*(const int64_t *)file_obj.end());
            if (file_len < 0) {
                LogError("Malformed file object '%1'", id);
                return false;
            }
            if (!ReserveFile(fd, dest_filename, file_len))
                return false;

            Async async(&tasks);

            // Write unencrypted file
            for (Size idx = 0, offset = 0; offset < file_obj.len; idx++, offset += RG_SIZE(kt_ChunkEntry)) {
                async.Run([=, this]() {
                    kt_ChunkEntry entry = {};

                    memcpy(&entry, file_obj.ptr + offset, RG_SIZE(entry));
                    entry.offset = LittleEndian(entry.offset);
                    entry.len = LittleEndian(entry.len);

                    kt_ObjectType type;
                    HeapArray<uint8_t> buf;
                    if (!disk->ReadObject(entry.id, &type, &buf))
                        return false;

                    if (RG_UNLIKELY(type != kt_ObjectType::Chunk)) {
                        LogError("Object '%1' is not a chunk", entry.id);
                        return false;
                    }
                    if (RG_UNLIKELY(buf.len != entry.len)) {
                        LogError("Chunk size mismatch for '%1'", entry.id);
                        return false;
                    }
                    if (!WriteAt(fd, dest_filename, entry.offset, buf)) {
                        LogError("Failed to write to '%1': %2", dest_filename, strerror(errno));
                        return false;
                    }

                    return true;
                });
            }

            if (!async.Sync())
                return false;

            // Check actual file size
            if (file_obj.len) {
                const kt_ChunkEntry *entry = (const kt_ChunkEntry *)(file_obj.end() - RG_SIZE(kt_ChunkEntry));
                int64_t len = LittleEndian(entry->offset) + LittleEndian(entry->len);

                if (RG_UNLIKELY(len != file_len)) {
                    LogError("File size mismatch for '%1'", entry->id);
                    return false;
                }
            }
        } break;

        case kt_ObjectType::Chunk: {
            file_len = file_obj.len;

            if (!WriteAt(fd, dest_filename, 0, file_obj)) {
                LogError("Failed to write to '%1': %2", dest_filename, strerror(errno));
                return false;
            }
        } break;

        case kt_ObjectType::Directory1:
        case kt_ObjectType::Directory2:
        case kt_ObjectType::Snapshot1:
        case kt_ObjectType::Snapshot2:
        case kt_ObjectType::Link: { RG_UNREACHABLE(); } break;
    }

    if (!FlushFile(fd, dest_filename))
        return false;

    stat_len += file_len;
    return true;
}

bool kt_Get(kt_Disk *disk, const kt_ID &id, const kt_GetSettings &settings, const char *dest_path, int64_t *out_len)
{
    kt_ObjectType type;
    HeapArray<uint8_t> obj;
    if (!disk->ReadObject(id, &type, &obj))
        return false;

    GetContext get(disk, settings.threads);

    switch (type) {
        case kt_ObjectType::Chunk:
        case kt_ObjectType::File: {
            if (TestFile(dest_path) && !IsDirectoryEmpty(dest_path)) {
                LogError("File '%1' already exists", dest_path);
                return false;
            }

            if (!get.GetFile(id, type, obj, dest_path))
                return false;
        } break;

        case kt_ObjectType::Directory1:
        case kt_ObjectType::Directory2: {
            if (TestFile(dest_path, FileType::Directory)) {
                if (!IsDirectoryEmpty(dest_path)) {
                    LogError("Directory '%1' exists and is not empty", dest_path);
                    return false;
                }
            } else {
                if (!MakeDirectory(dest_path))
                    return false;
            }

            if (!get.ExtractEntries(type, obj, 0, dest_path))
                return false;
        } break;

        case kt_ObjectType::Snapshot1:
        case kt_ObjectType::Snapshot2: {
            if (TestFile(dest_path, FileType::Directory)) {
                if (!IsDirectoryEmpty(dest_path)) {
                    LogError("Directory '%1' exists and is not empty", dest_path);
                    return false;
                }
            } else {
                if (!MakeDirectory(dest_path))
                    return false;
            }

            // There must be at least one entry
            if (obj.len <= RG_SIZE(kt_SnapshotHeader)) {
                LogError("Malformed snapshot object '%1'", id);
                return false;
            }

            Span<uint8_t> entries = obj.Take(RG_SIZE(kt_SnapshotHeader), obj.len - RG_SIZE(kt_SnapshotHeader));
            unsigned int flags = (int)ExtractFlag::AllowSeparators | (settings.flat ? (int)ExtractFlag::FlattenName : 0);

            if (!get.ExtractEntries(type, entries, flags, dest_path))
                return false;
        } break;

        case kt_ObjectType::Link: {
            obj.Append(0);

            if (!CreateSymbolicLink(dest_path, (const char *)obj.ptr))
                return false;
        } break;
    }

    if (!get.Sync())
        return false;

    if (out_len) {
        *out_len += get.GetLen();
    }
    return true;
}

bool kt_List(kt_Disk *disk, const kt_ListSettings &settings, Allocator *str_alloc, HeapArray<kt_SnapshotInfo> *out_snapshots)
{
    Size prev_len = out_snapshots->len;
    RG_DEFER_N(out_guard) { out_snapshots->RemoveFrom(prev_len); };

    HeapArray<kt_ID> ids;
    if (!disk->ListTags(&ids))
        return false;

    Async async(settings.threads);

    // Gather snapshot information
    {
        std::mutex mutex;

        for (const kt_ID &id: ids) {
            async.Run([=, &mutex]() {
                kt_SnapshotInfo snapshot = {};

                kt_ObjectType type;
                HeapArray<uint8_t> obj;
                if (!disk->ReadObject(id, &type, &obj))
                    return false;

                if (type != kt_ObjectType::Snapshot1 && type != kt_ObjectType::Snapshot2) {
                    LogError("Object '%1' is not a snapshot (ignoring)", id);
                    return true;
                }
                if (obj.len <= RG_SIZE(kt_SnapshotHeader)) {
                    LogError("Malformed snapshot object '%1' (ignoring)", id);
                    return true;
                }

                std::lock_guard lock(mutex);
                const kt_SnapshotHeader *header = (const kt_SnapshotHeader *)obj.ptr;

                snapshot.id = id;
                snapshot.name = header->name[0] ? DuplicateString(header->name, str_alloc).ptr : nullptr;
                snapshot.time = LittleEndian(header->time);
                snapshot.len = LittleEndian(header->len);
                snapshot.stored = LittleEndian(header->stored) + obj.len;

                out_snapshots->Append(snapshot);

                return true;
            });
        }
    }

    if (!async.Sync())
        return false;

    std::sort(out_snapshots->ptr + prev_len, out_snapshots->end(),
              [](const kt_SnapshotInfo &snapshot1, const kt_SnapshotInfo &snapshot2) { return snapshot1.time < snapshot2.time; });

    out_guard.Disable();
    return true;
}

}
