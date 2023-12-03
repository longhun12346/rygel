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
#include "src/core/libnet/curl.hh"
#include "src/core/libpasswd/libpasswd.hh"
#include "src/core/libwrap/json.hh"
#include "src/rekord/librekord/librekord.hh"
#include "vendor/libsodium/src/libsodium/include/sodium.h"
#include "vendor/pugixml/src/pugixml.hpp"
#include <iostream> // for pugixml
#ifndef _WIN32
    #include <sys/time.h>
    #include <sys/resource.h>
#endif

namespace RG {

enum class OutputFormat {
    Human,
    JSON,
    XML
};

static const char *const OutputFormatNames[] = {
    "Human",
    "JSON",
    "XML"
};

static bool FindAndLoadConfig(Span<const char *> arguments, rk_Config *out_config)
{
    OptionParser opt(arguments, OptionMode::Skip);

    const char *config_filename = nullptr;

    while (opt.Next()) {
        if (opt.Test("-C", "--config_file", OptionType::Value)) {
            config_filename = opt.current_value;
        }
    }

    if (config_filename && !rk_LoadConfig(config_filename, out_config))
        return false;

    return true;
}

static int RunInit(Span<const char *> arguments)
{
    // Options
    rk_Config config;
    char full_pwd[129] = {};
    char write_pwd[129] = {};

    const auto print_usage = [=](FILE *fp) {
        PrintLn(fp,
R"(Usage: %!..+%1 init [-C <config>] [dir]

Options:
    %!..+-C, --config_file <file>%!0     Set configuration file

        %!..+--full_password <pwd>%!0    Set full password manually
        %!..+--write_password <pwd>%!0   Set write-only password manually)", FelixTarget);
    };

    if (!FindAndLoadConfig(arguments, &config))
        return 1;

    // Parse arguments
    {
        OptionParser opt(arguments);

        while (opt.Next()) {
            if (opt.Test("--help")) {
                print_usage(stdout);
                return 0;
            } else if (opt.Test("-C", "--config_file", OptionType::Value)) {
                // Already handled
            } else if (opt.Test("--full_password", OptionType::Value)) {
                if (!CopyString(opt.current_value, full_pwd)) {
                    LogError("Password is too long");
                    return 1;
                }
            } else if (opt.Test("--write_password", OptionType::Value)) {
                if (!CopyString(opt.current_value, write_pwd)) {
                    LogError("Password is too long");
                    return 1;
                }
            } else {
                opt.LogUnknownError();
                return 1;
            }
        }

        const char *repo = opt.ConsumeNonOption();

        if (repo && !rk_DecodeURL(repo, &config))
            return 1;
    }

    // Generate repository passwords
    {
        // Avoid characters that are annoying in consoles
        unsigned int flags = (int)pwd_GenerateFlag::LowersNoAmbi |
                             (int)pwd_GenerateFlag::UppersNoAmbi |
                             (int)pwd_GenerateFlag::DigitsNoAmbi |
                             (int)pwd_GenerateFlag::Specials;

        if (!full_pwd[0] && !pwd_GeneratePassword(flags, MakeSpan(full_pwd, 33)))
            return 1;
        if (!write_pwd[0] && !pwd_GeneratePassword(flags, MakeSpan(write_pwd, 33)))
            return 1;
    }

    if (!config.Complete(false))
        return 1;

    std::unique_ptr<rk_Disk> disk = rk_Open(config, false);
    if (!disk)
        return 1;
    if (!disk->Init(full_pwd, write_pwd))
        return 1;

    LogInfo("Repository: %!..+%1%!0", disk->GetURL());
    LogInfo();
    LogInfo("Default account name: %!..+default%!0");
    LogInfo();
    LogInfo("Default full password: %!..+%1%!0", full_pwd);
    LogInfo("  write-only password: %!..+%1%!0", write_pwd);
    LogInfo();
    LogInfo("Please write them down, they cannot be recovered and the backup will be lost if you lose them.");

    return 0;
}

static int RunExportMasterKey(Span<const char *> arguments)
{
    // Options
    rk_Config config;
    const char *output_filename = "full.key";

    const auto print_usage = [=](FILE *fp) {
        PrintLn(fp,
R"(Usage: %!..+%1 export_key [-C <config>]

Options:
    %!..+-C, --config_file <file>%!0     Set configuration file

    %!..+-R, --repository <dir>%!0       Set repository directory
    %!..+-u, --user <user>%!0            Set repository username
        %!..+--password <pwd>%!0         Set repository password

    %!..+-O, --output_file <file>%!0     Output key to specific file
                                 %!D..(default: %2)%!0)", FelixTarget, output_filename);
    };

    if (!FindAndLoadConfig(arguments, &config))
        return 1;

    // Parse arguments
    {
        OptionParser opt(arguments);

        while (opt.Next()) {
            if (opt.Test("--help")) {
                print_usage(stdout);
                return 0;
            } else if (opt.Test("-C", "--config_file", OptionType::Value)) {
                // Already handled
            } else if (opt.Test("-R", "--repository", OptionType::Value)) {
                if (!rk_DecodeURL(opt.current_value, &config))
                    return 1;
            } else if (opt.Test("-u", "--username", OptionType::Value)) {
                config.username = opt.current_value;
            } else if (opt.Test("--password", OptionType::Value)) {
                config.password = opt.current_value;
            } else {
                opt.LogUnknownError();
                return 1;
            }
        }
    }

    if (!config.Complete(true))
        return 1;

    std::unique_ptr<rk_Disk> disk = rk_Open(config, true);
    if (!disk)
        return 1;

    LogInfo("Repository: %!..+%1%!0 (%2)", disk->GetURL(), rk_DiskModeNames[(int)disk->GetMode()]);
    if (disk->GetMode() != rk_DiskMode::ReadWrite) {
        LogError("You must use the read-write password with this command");
        return 1;
    }

    if (!WriteFile(disk->GetFullKey(), output_filename))
        return 1;

    LogInfo("Unprotected full-access key written to: %!..+%1%!0", output_filename);
    return 0;
}

static int RunPut(Span<const char *> arguments)
{
    BlockAllocator temp_alloc;

    // Options
    rk_Config config;
    rk_PutSettings settings;
    bool allow_anonymous = false;
    HeapArray<const char *> filenames;

    const auto print_usage = [=](FILE *fp) {
        PrintLn(fp,
R"(Usage: %!..+%1 put [-R <repo>] <filename> ...%!0

Options:
    %!..+-C, --config_file <file>%!0     Set configuration file

    %!..+-R, --repository <dir>%!0       Set repository directory
    %!..+-u, --user <user>%!0            Set repository username
        %!..+--password <pwd>%!0         Set repository password

    %!..+-n, --name <name>%!0            Set user friendly name
        %!..+--anonymous%!0              Allow snapshot without name
        %!..+--raw%!0                    Skip snapshot object and report data ID

        %!..+--follow_symlinks%!0        Follow symbolic links (instead of storing them as-is

    %!..+-j, --threads <threads>%!0      Change number of threads
                                 %!D..(default: automatic)%!0)", FelixTarget);
    };

    if (!FindAndLoadConfig(arguments, &config))
        return 1;

    // Parse arguments
    {
        OptionParser opt(arguments);

        while (opt.Next()) {
            if (opt.Test("--help")) {
                print_usage(stdout);
                return 0;
            } else if (opt.Test("-C", "--config_file", OptionType::Value)) {
                // Already handled
            } else if (opt.Test("-R", "--repository", OptionType::Value)) {
                if (!rk_DecodeURL(opt.current_value, &config))
                    return 1;
            } else if (opt.Test("-u", "--username", OptionType::Value)) {
                config.username = opt.current_value;
            } else if (opt.Test("--password", OptionType::Value)) {
                config.password = opt.current_value;
            } else if (opt.Test("-n", "--name", OptionType::Value)) {
                settings.name = opt.current_value;
            } else if (opt.Test("--follow_symlinks")) {
                settings.follow_symlinks = true;
            } else if (opt.Test("--anonymous")) {
                allow_anonymous = true;
            } else if (opt.Test("--raw")) {
                settings.raw = true;
            } else if (opt.Test("-j", "--threads", OptionType::Value)) {
                if (!ParseInt(opt.current_value, &config.threads))
                    return 1;
                if (config.threads < 1) {
                    LogError("Threads count cannot be < 1");
                    return 1;
                }
            } else {
                opt.LogUnknownError();
                return 1;
            }
        }

        opt.ConsumeNonOptions(&filenames);
    }

    if (!filenames.len) {
        LogError("No filename provided");
        return 1;
    }

    if (!settings.name && !allow_anonymous && !settings.raw) {
        LogError("Use --anonymous to create unnamed snapshot object");
        return 1;
    }

    if (!config.Complete(true))
        return 1;

    std::unique_ptr<rk_Disk> disk = rk_Open(config, true);
    if (!disk)
        return 1;

    LogInfo("Repository: %!..+%1%!0 (%2)", disk->GetURL(), rk_DiskModeNames[(int)disk->GetMode()]);
    if (disk->GetMode() != rk_DiskMode::WriteOnly) {
        LogWarning("You should use the write-only key with this command");
    }

    LogInfo();
    LogInfo("Backing up...");

    int64_t now = GetMonotonicTime();

    rk_ID id = {};
    int64_t total_len = 0;
    int64_t total_written = 0;
    if (!rk_Put(disk.get(), settings, filenames, &id, &total_len, &total_written))
        return 1;

    double time = (double)(GetMonotonicTime() - now) / 1000.0;

    LogInfo();
    LogInfo("%1 ID: %!..+%2%!0", settings.raw ? "Data" : "Snapshot", id);
    LogInfo("Stored size: %!..+%1%!0", FmtDiskSize(total_len));
    LogInfo("Total written: %!..+%1%!0", FmtDiskSize(total_written));
    LogInfo("Execution time: %!..+%1s%!0", FmtDouble(time, 1));

    return 0;
}

static int RunGet(Span<const char *> arguments)
{
    BlockAllocator temp_alloc;

    // Options
    rk_Config config;
    rk_GetSettings settings;
    const char *dest_filename = nullptr;
    const char *name = nullptr;

    const auto print_usage = [=](FILE *fp) {
        PrintLn(fp,
R"(Usage: %!..+%1 get [-R <repo>] <ID> -O <path>%!0

Options:
    %!..+-C, --config_file <file>%!0     Set configuration file

    %!..+-R, --repository <dir>%!0       Set repository directory
    %!..+-u, --user <user>%!0            Set repository username
        %!..+--password <pwd>%!0         Set repository password

    %!..+-O, --output <path>%!0          Restore file or directory to path

    %!..+-f, --force%!0                  Overwrite destination if not empty

        %!..+--flat%!0                   Use flat names for snapshot files
        %!..+--chown%!0                  Restore original file UID and GID

    %!..+-j, --threads <threads>%!0      Change number of threads
                                 %!D..(default: automatic)%!0)", FelixTarget);
    };

    if (!FindAndLoadConfig(arguments, &config))
        return 1;

    // Parse arguments
    {
        OptionParser opt(arguments);

        while (opt.Next()) {
            if (opt.Test("--help")) {
                print_usage(stdout);
                return 0;
            } else if (opt.Test("-C", "--config_file", OptionType::Value)) {
                // Already handled
            } else if (opt.Test("-R", "--repository", OptionType::Value)) {
                if (!rk_DecodeURL(opt.current_value, &config))
                    return 1;
            } else if (opt.Test("-u", "--username", OptionType::Value)) {
                config.username = opt.current_value;
            } else if (opt.Test("--password", OptionType::Value)) {
                config.password = opt.current_value;
            } else if (opt.Test("-O", "--output", OptionType::Value)) {
                dest_filename = opt.current_value;
            } else if (opt.Test("-f", "--force")) {
                settings.force = true;
            } else if (opt.Test("--flat")) {
                settings.flat = true;
            } else if (opt.Test("--chown")) {
                settings.chown = true;
            } else if (opt.Test("-j", "--threads", OptionType::Value)) {
                if (!ParseInt(opt.current_value, &config.threads))
                    return 1;
                if (config.threads < 1) {
                    LogError("Threads count cannot be < 1");
                    return 1;
                }
            } else {
                opt.LogUnknownError();
                return 1;
            }
        }

        name = opt.ConsumeNonOption();
    }

    if (!name) {
        LogError("No ID provided");
        return 1;
    }
    if (!dest_filename) {
        LogError("Missing destination filename");
        return 1;
    }

    if (!config.Complete(true))
        return 1;

    std::unique_ptr<rk_Disk> disk = rk_Open(config, true);
    if (!disk)
        return 1;

    LogInfo("Repository: %!..+%1%!0 (%2)", disk->GetURL(), rk_DiskModeNames[(int)disk->GetMode()]);
    if (disk->GetMode() != rk_DiskMode::ReadWrite) {
        LogError("Cannot decrypt with write-only key");
        return 1;
    }

    LogInfo();
    LogInfo("Extracting...");

    int64_t now = GetMonotonicTime();

    int64_t file_len = 0;
    {
        rk_ID id = {};
        if (!rk_ParseID(name, &id))
            return 1;
        if (!rk_Get(disk.get(), id, settings, dest_filename, &file_len))
            return 1;
    }

    double time = (double)(GetMonotonicTime() - now) / 1000.0;

    LogInfo();
    LogInfo("Restored: %!..+%1%!0 (%2)", dest_filename, FmtDiskSize(file_len));
    LogInfo("Execution time: %!..+%1s%!0", FmtDouble(time, 1));

    return 0;
}

static int RunList(Span<const char *> arguments)
{
    BlockAllocator temp_alloc;

    // Options
    rk_Config config;
    OutputFormat format = OutputFormat::Human;

    const auto print_usage = [=](FILE *fp) {
        PrintLn(fp,
R"(Usage: %!..+%1 list [-R <repo>]%!0

Options:
    %!..+-C, --config_file <file>%!0     Set configuration file

    %!..+-R, --repository <dir>%!0       Set repository directory
    %!..+-u, --user <user>%!0            Set repository username
        %!..+--password <pwd>%!0         Set repository password

    %!..+-j, --threads <threads>%!0      Change number of threads
                                 %!D..(default: automatic)%!0

    %!..+-f, --format <format>%!0        Change output format
                                 %!D..(default: %2)%!0

Available output formats: %!..+%3%!0)", FelixTarget, OutputFormatNames[(int)format], FmtSpan(OutputFormatNames));
    };

    if (!FindAndLoadConfig(arguments, &config))
        return 1;

    // Parse arguments
    {
        OptionParser opt(arguments);

        while (opt.Next()) {
            if (opt.Test("--help")) {
                print_usage(stdout);
                return 0;
            } else if (opt.Test("-C", "--config_file", OptionType::Value)) {
                // Already handled
            } else if (opt.Test("-R", "--repository", OptionType::Value)) {
                if (!rk_DecodeURL(opt.current_value, &config))
                    return 1;
            } else if (opt.Test("-u", "--username", OptionType::Value)) {
                config.username = opt.current_value;
            } else if (opt.Test("--password", OptionType::Value)) {
                config.password = opt.current_value;
            } else if (opt.Test("-f", "--format", OptionType::Value)) {
                if (!OptionToEnum(OutputFormatNames, opt.current_value, &format)) {
                    LogError("Unknown output format '%1'", opt.current_value);
                    return 1;
                }
            } else if (opt.Test("-j", "--threads", OptionType::Value)) {
                if (!ParseInt(opt.current_value, &config.threads))
                    return 1;
                if (config.threads < 1) {
                    LogError("Threads count cannot be < 1");
                    return 1;
                }
            } else {
                opt.LogUnknownError();
                return 1;
            }
        }
    }

    if (!config.Complete(true))
        return 1;

    std::unique_ptr<rk_Disk> disk = rk_Open(config, true);
    if (!disk)
        return 1;

    LogInfo("Repository: %!..+%1%!0 (%2)", disk->GetURL(), rk_DiskModeNames[(int)disk->GetMode()]);
    if (disk->GetMode() != rk_DiskMode::ReadWrite) {
        LogError("Cannot list with write-only key");
        return 1;
    }
    LogInfo();

    HeapArray<rk_SnapshotInfo> snapshots;
    if (!rk_List(disk.get(), &temp_alloc, &snapshots))
        return 1;

    switch (format) {
        case OutputFormat::Human: {
            if (snapshots.len) {
                for (const rk_SnapshotInfo &snapshot: snapshots) {
                    TimeSpec spec = DecomposeTime(snapshot.time);

                    PrintLn("%!..+%1%!0", snapshot.id);
                    if (snapshot.name) {
                        PrintLn("+ Name: %!..+%1%!0", snapshot.name);
                    }
                    PrintLn("+ Time: %!..+%1%!0", FmtTimeNice(spec));
                    PrintLn("+ Size: %!..+%1%!0", FmtDiskSize(snapshot.len));
                    PrintLn("+ Storage: %!..+%1%!0", FmtDiskSize(snapshot.stored));
                    PrintLn();
                }
            } else {
                LogInfo("There does not seem to be any snapshot");
            }
        } break;

        case OutputFormat::JSON: {
            json_PrettyWriter json(&stdout_st);

            json.StartArray();
            for (const rk_SnapshotInfo &snapshot: snapshots) {
                json.StartObject();

                char id[128];
                Fmt(id, "%1", snapshot.id);

                json.Key("id"); json.String(id);
                if (snapshot.name) {
                    json.Key("name"); json.String(snapshot.name);
                } else {
                    json.Key("name"); json.Null();
                }
                json.Key("time"); json.Int64(snapshot.time);
                json.Key("size"); json.Int64(snapshot.len);
                json.Key("storage"); json.Int64(snapshot.stored);

                json.EndObject();
            }
            json.EndArray();

            json.Flush();
            PrintLn();
        } break;

        case OutputFormat::XML: {
            pugi::xml_document doc;
            pugi::xml_node root = doc.append_child("Snapshots");

            for (const rk_SnapshotInfo &snapshot: snapshots) {
                pugi::xml_node element = root.append_child("Snapshot");

                char id[128];
                Fmt(id, "%1", snapshot.id);

                element.append_attribute("id") = id;
                element.append_attribute("name") = snapshot.name ? snapshot.name : "";
                element.append_attribute("time") = snapshot.time;
                element.append_attribute("size") = snapshot.len;
                element.append_attribute("storage") = snapshot.stored;
            }

            doc.save(std::cout, "    ");
        } break;
    }

    return 0;
}

static void ListObjectPlain(const rk_ObjectInfo &obj, int start_depth)
{
    TimeSpec mspec = DecomposeTime(obj.mtime);
    int indent = (start_depth + obj.depth) * 2;

    char suffix = (obj.type == rk_ObjectType::Directory) ? '/' : ' ';
    int align = std::max(60 - indent - strlen(obj.basename), (size_t)0);
    bool size = (obj.readable && obj.type == rk_ObjectType::File);

    PrintLn("%1%!D..[%2] %!0%!..+%3%4%!0%5 (0%6) %!D..[%7]%!0 %8",
            FmtArg(" ").Repeat(indent), rk_ObjectTypeNames[(int)obj.type][0],
            obj.basename, suffix, FmtArg(" ").Repeat(align), FmtOctal(obj.mode),
            FmtTimeNice(mspec), size ? FmtDiskSize(obj.size) : FmtArg(""));
}

static void ListObjectJson(json_PrettyWriter *json, const rk_ObjectInfo &obj)
{
    char buf[128];

    json->Key("type"); json->String(rk_ObjectTypeNames[(int)obj.type]);
    if (obj.readable) {
        json->Key("id"); json->String(Fmt(buf, "%1", obj.id).ptr);
    } else {
        json->Key("id"); json->Null();
    }
    json->Key("name"); json->String(obj.basename);
    json->Key("mtime"); json->Int64(obj.mtime);
    json->Key("btime"); json->Int64(obj.btime);
    json->Key("mode"); json->String(Fmt(buf, "0o%1", FmtOctal(obj.mode)).ptr);
    json->Key("uid"); json->Uint(obj.uid);
    json->Key("gid"); json->Uint(obj.gid);

    if (obj.readable) {
        switch (obj.type) {
            case rk_ObjectType::Directory: { json->Key("children"); json->StartArray(); } break;
            case rk_ObjectType::File: { json->Key("size"); json->Int64(obj.size); } break;
            case rk_ObjectType::Link: { json->Key("target"); json->String(obj.u.target); } break;
            case rk_ObjectType::Unknown: {} break;
        }
    }
}

static pugi::xml_node ListObjectXml(pugi::xml_node *ptr, const rk_ObjectInfo &obj)
{
    char buf[128];

    pugi::xml_node element = ptr->append_child(rk_ObjectTypeNames[(int)obj.type]);

    if (obj.readable) {
        element.append_attribute("id") = Fmt(buf, "%1", obj.id).ptr;
    } else {
        element.append_attribute("id") = "";
    }
    element.append_attribute("name") = obj.basename;
    element.append_attribute("mtime") = obj.mtime;
    element.append_attribute("btime") = obj.btime;
    element.append_attribute("mode") = Fmt(buf, "0o%1", FmtOctal(obj.mode)).ptr;
    element.append_attribute("uid") = obj.uid;
    element.append_attribute("gid") = obj.gid;

    if (obj.readable) {
        switch (obj.type) {
            case rk_ObjectType::Directory: {} break;
            case rk_ObjectType::File: { element.append_attribute("size") = obj.size; } break;
            case rk_ObjectType::Link: { element.append_attribute("target") = obj.u.target; } break;
            case rk_ObjectType::Unknown: {} break;
        }
    }

    return element;
}

static int RunTree(Span<const char *> arguments)
{
    BlockAllocator temp_alloc;

    // Options
    rk_Config config;
    rk_TreeSettings settings;
    OutputFormat format = OutputFormat::Human;
    const char *name = nullptr;

    const auto print_usage = [=](FILE *fp) {
        PrintLn(fp,
R"(Usage: %!..+%1 tree [-R <repo>] <ID>%!0

Options:
    %!..+-C, --config_file <file>%!0     Set configuration file

    %!..+-R, --repository <dir>%!0       Set repository directory
    %!..+-u, --user <user>%!0            Set repository username
        %!..+--password <pwd>%!0         Set repository password

    %!..+-j, --threads <threads>%!0      Change number of threads
                                 %!D..(default: automatic)%!0

        %!..+--depth <depth>%!0          Set maximum recursion depth
                                 %!D..(default: All)%!0

    %!..+-f, --format <format>%!0        Change output format
                                 %!D..(default: %2)%!0

Available output formats: %!..+%3%!0)",
                FelixTarget, OutputFormatNames[(int)format], FmtSpan(OutputFormatNames));
    };

    if (!FindAndLoadConfig(arguments, &config))
        return 1;

    // Parse arguments
    {
        OptionParser opt(arguments);

        while (opt.Next()) {
            if (opt.Test("--help")) {
                print_usage(stdout);
                return 0;
            } else if (opt.Test("-C", "--config_file", OptionType::Value)) {
                // Already handled
            } else if (opt.Test("-R", "--repository", OptionType::Value)) {
                if (!rk_DecodeURL(opt.current_value, &config))
                    return 1;
            } else if (opt.Test("-u", "--username", OptionType::Value)) {
                config.username = opt.current_value;
            } else if (opt.Test("--password", OptionType::Value)) {
                config.password = opt.current_value;
            } else if (opt.Test("-j", "--threads", OptionType::Value)) {
                if (!ParseInt(opt.current_value, &config.threads))
                    return 1;
                if (config.threads < 1) {
                    LogError("Threads count cannot be < 1");
                    return 1;
                }
            } else if (opt.Test("--depth", OptionType::Value)) {
                if (TestStr(opt.current_value, "All")) {
                    settings.max_depth = -1;
                } else if (!ParseInt(opt.current_value, &settings.max_depth)) {
                    return 1;
                } else if (settings.max_depth < 0) {
                    LogInfo("Option --depth must be 0 or more (or 'All')");
                    return 1;
                }
            } else if (opt.Test("-f", "--format", OptionType::Value)) {
                if (!OptionToEnum(OutputFormatNames, opt.current_value, &format)) {
                    LogError("Unknown output format '%1'", opt.current_value);
                    return 1;
                }
            } else {
                opt.LogUnknownError();
                return 1;
            }
        }

        name = opt.ConsumeNonOption();
    }

    if (!name) {
        LogError("No ID provided");
        return 1;
    }

    if (!config.Complete(true))
        return 1;

    std::unique_ptr<rk_Disk> disk = rk_Open(config, true);
    if (!disk)
        return 1;

    LogInfo("Repository: %!..+%1%!0 (%2)", disk->GetURL(), rk_DiskModeNames[(int)disk->GetMode()]);
    if (disk->GetMode() != rk_DiskMode::ReadWrite) {
        LogError("Cannot list with write-only key");
        return 1;
    }
    LogInfo();

    HeapArray<rk_ObjectInfo> objects;
    {
        rk_ID id = {};
        if (!rk_ParseID(name, &id))
            return 1;
        if (!rk_Tree(disk.get(), id, settings, &temp_alloc, &objects))
            return 1;
    }

    switch (format) {
        case OutputFormat::Human: {
            if (objects.len) {
                for (const rk_ObjectInfo &obj: objects) {
                    ListObjectPlain(obj, 0);
                }
            } else {
                LogInfo("There does not seem to be any object");
            }
        } break;

        case OutputFormat::JSON: {
            json_PrettyWriter json(&stdout_st);
            int depth = 0;

            json.StartArray();
            for (const rk_ObjectInfo &obj: objects) {
                while (obj.depth < depth) {
                    json.EndArray();
                    json.EndObject();

                    depth--;
                }

                json.StartObject();

                ListObjectJson(&json, obj);

                if (obj.type == rk_ObjectType::Directory) {
                    if (obj.u.children) {
                        depth++;
                        continue;
                    } else {
                        json.EndArray();
                    }
                }

                json.EndObject();
            }
            while (depth--) {
                json.EndArray();
                json.EndObject();
            }
            json.EndArray();

            json.Flush();
            PrintLn();
        } break;

        case OutputFormat::XML: {
            pugi::xml_document doc;
            pugi::xml_node root = doc.append_child("Tree");

            pugi::xml_node ptr = root;
            int depth = 0;

            for (const rk_ObjectInfo &obj: objects) {
                while (obj.depth < depth) {
                    ptr = ptr.parent();
                    depth--;
                }

                pugi::xml_node element = ListObjectXml(&ptr, obj);

                if (obj.type == rk_ObjectType::Directory && obj.u.children) {
                    depth++;
                    ptr = element;
                }
            }

            doc.save(std::cout, "    ");
        } break;
    }

    return 0;
}

int Main(int argc, char **argv)
{
    RG_CRITICAL(argc >= 1, "First argument is missing");

    const auto print_usage = [](FILE *fp) {
        PrintLn(fp, R"(Usage: %!..+%1 <command> [args]%!0

Management commands:
    %!..+init%!0                         Init new backup repository
    %!..+export_key%!0                   Export master repository key

Snapshot commands:
    %!..+put%!0                          Store encrypted directory or file
    %!..+get%!0                          Get and decrypt directory or file

Exploration commands:
    %!..+list%!0                         List snapshots
    %!..+tree%!0                         Export snapshot or directory tree

Use %!..+%1 help <command>%!0 or %!..+%1 <command> --help%!0 for more specific help.)", FelixTarget);
    };

    if (argc < 2) {
        print_usage(stderr);
        PrintLn(stderr);
        LogError("No command provided");
        return 1;
    }

#ifdef _WIN32
    _setmaxstdio(4096);
#else
    {
        const rlim_t max_nofile = 16384;
        struct rlimit lim;

        // Increase maximum number of open file descriptors
        if (getrlimit(RLIMIT_NOFILE, &lim) >= 0) {
            if (lim.rlim_cur < max_nofile) {
                lim.rlim_cur = std::min(max_nofile, lim.rlim_max);

                if (setrlimit(RLIMIT_NOFILE, &lim) >= 0) {
                    if (lim.rlim_cur < max_nofile) {
                        LogError("Maximum number of open descriptors is low: %1 (recommended: %2)", lim.rlim_cur, max_nofile);
                    }
                } else {
                    LogError("Could not raise RLIMIT_NOFILE to %1: %2", max_nofile, strerror(errno));
                }
            }
        } else {
            LogError("getrlimit(RLIMIT_NOFILE) failed: %1", strerror(errno));
        }
    }
#endif

    if (sodium_init() < 0) {
        LogError("Failed to initialize libsodium");
        return 1;
    }
    if (curl_global_init(CURL_GLOBAL_ALL)) {
        LogError("Failed to initialize libcurl");
        return 1;
    }
    if (ssh_init() < 0) {
        LogError("Failed to initialize libssh");
        return 1;
    }
    RG_DEFER { ssh_finalize(); };

    const char *cmd = argv[1];
    Span<const char *> arguments((const char **)argv + 2, argc - 2);

    // Handle help and version arguments
    if (TestStr(cmd, "--help") || TestStr(cmd, "help")) {
        if (arguments.len && arguments[0][0] != '-') {
            cmd = arguments[0];
            arguments[0] = (cmd[0] == '-') ? cmd : "--help";
        } else {
            print_usage(stdout);
            return 0;
        }
    } else if (TestStr(cmd, "--version")) {
        PrintLn("%!R..%1%!0 %!..+%2%!0", FelixTarget, FelixVersion);
        PrintLn("Compiler: %1", FelixCompiler);
        return 0;
    }

    if (TestStr(cmd, "init")) {
        return RunInit(arguments);
    } else if (TestStr(cmd, "export_key")) {
        return RunExportMasterKey(arguments);
    } else if (TestStr(cmd, "put")) {
        return RunPut(arguments);
    } else if (TestStr(cmd, "get")) {
        return RunGet(arguments);
    } else if (TestStr(cmd, "list")) {
        return RunList(arguments);
    } else if (TestStr(cmd, "tree")) {
        return RunTree(arguments);
    } else {
        LogError("Unknown command '%1'", cmd);
        return 1;
    }
}

}

// C++ namespaces are stupid
int main(int argc, char **argv) { return RG::RunApp(argc, argv); }
