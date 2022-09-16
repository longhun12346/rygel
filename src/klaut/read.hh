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

#pragma once

#include "src/core/libcc/libcc.hh"
#include "types.hh"

namespace RG {

bool kt_ListSnapshots(kt_Disk *disk, Allocator *alloc, HeapArray<kt_SnapshotInfo> *out_snapshots);
bool kt_ReadSnapshot(kt_Disk *disk, const kt_ObjectID &id, Allocator *alloc, kt_SnapshotInfo *out_snapshot);

bool kt_ListDirectory(kt_Disk *disk, const kt_ObjectID &id, Allocator *alloc, HeapArray<kt_EntryInfo> *out_entries);
bool kt_ExtractFile(kt_Disk *disk, const kt_ObjectID &id, const char *dest_filename);

}
