// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#ifndef _WIN32
    #include <dlfcn.h>
    #include <signal.h>
    #include <sys/types.h>
    #include <sys/stat.h>
    #include <unistd.h>
#endif

#include "../../../lib/libsodium/src/libsodium/include/sodium.h"
#include "thop.hh"
#include "config.hh"
#include "structure.hh"
#include "mco.hh"
#include "mco_casemix.hh"
#include "mco_info.hh"
#include "user.hh"
#include "../../wrappers/http.hh"
#include "../../packer/asset.hh"

struct CatalogSet {
    HeapArray<PackerAsset> catalogs;
    LinkedAllocator alloc;
};

struct Route {
    enum class Type {
        Static,
        Function
    };
    enum class Matching {
        Exact,
        Walk
    };

    Span<const char> url;
    const char *method;
    Matching matching;

    Type type;
    union {
        struct {
            PackerAsset asset;
            const char *mime_type;
        } st;

        int (*func)(const ConnectionInfo *conn, const char *url, http_Response *out_response);
    } u;

    Route() = default;
    Route(const char *url, const char *method, Matching matching,
          const PackerAsset &asset, const char *mime_type)
        : url(url), method(method), matching(matching), type(Type::Static)
    {
        u.st.asset = asset;
        u.st.mime_type = mime_type;
    }
    Route(const char *url, const char *method, Matching matching,
          int (*func)(const ConnectionInfo *conn, const char *url, http_Response *out_response))
        : url(url), method(method), matching(matching), type(Type::Function)
    {
        u.func = func;
    }

    HASH_TABLE_HANDLER(Route, url);
};

Config thop_config;
bool thop_has_casemix;

StructureSet thop_structure_set;
UserSet thop_user_set;

static CatalogSet catalog_set;
#ifndef NDEBUG
static HeapArray<PackerAsset> packer_assets;
static LinkedAllocator packer_alloc;
#else
extern const Span<const PackerAsset> packer_assets;
#endif

static HashTable<Span<const char>, Route> routes;
static BlockAllocator routes_alloc;
static char etag[64];

static bool InitCatalogSet(Span<const char *const> table_directories)
{
    BlockAllocator temp_alloc;

    HeapArray<const char *> filenames;
    {
        bool success = true;
        for (const char *resource_dir: table_directories) {
            const char *desc_dir = Fmt(&temp_alloc, "%1%/catalogs", resource_dir).ptr;
            if (TestPath(desc_dir, FileType::Directory)) {
                success &= EnumerateDirectoryFiles(desc_dir, "*.json", 1024, &temp_alloc, &filenames);
            }
        }
        if (!success)
            return false;
    }

    if (!filenames.len) {
        LogError("No catalog file specified or found");
    }

    for (const char *filename: filenames) {
        PackerAsset catalog = {};

        const char *name = SplitStrReverseAny(filename, PATH_SEPARATORS).ptr;
        Assert(name[0]);

        HeapArray<uint8_t> buf(&catalog_set.alloc);
        {
            StreamReader reader(filename);
            StreamWriter writer(&buf, nullptr, CompressionType::Gzip);
            if (!SpliceStream(&reader, Megabytes(8), &writer))
                return false;
            if (!writer.Close())
                return false;
        }

        catalog.name = DuplicateString(name, &catalog_set.alloc).ptr;
        catalog.data = buf.Leak();
        catalog.compression_type = CompressionType::Gzip;

        catalog_set.catalogs.Append(catalog);
    }

    return true;
}

static bool InitUsers(const char *profile_directory)
{
    BlockAllocator temp_alloc;

    LogInfo("Load users");

    if (const char *filename = Fmt(&temp_alloc, "%1%/users.ini", profile_directory).ptr;
            !LoadUserSet(filename, thop_structure_set, &thop_user_set))
        return false;

    // Everyone can use the default dispense mode
    for (User &user: thop_user_set.users) {
        user.mco_dispense_modes |= 1 << (int)thop_config.mco_dispense_mode;
    }

    return true;
}

static bool PatchTextFile(StreamReader &st, HeapArray<uint8_t> *out_buf)
{
    StreamWriter writer(out_buf, nullptr, st.compression.type);

    HeapArray<char> buf;
    if (st.ReadAll(Megabytes(1), &buf) < 0)
        return false;
    buf.Append(0);

    Span<const char> html = buf.Take(0, buf.len - 1);
    do {
        Span<const char> part = SplitStr(html, '$', &html);

        writer.Write(part);

        if (html.ptr[0] == '{') {
            Span<const char> var = MakeSpan(html.ptr,
                                            strspn(html.ptr + 1, "ABCDEFGHIJKLMNOPQRSTUVWXYZ_") + 2);

            if (var == "{THOP_BASE_URL}") {
                writer.Write(thop_config.base_url);
                html = html.Take(var.len, html.len - var.len);
            } else if (var == "{THOP_SHOW_USER}") {
                writer.Write(thop_has_casemix ? "true" : "false");
                html = html.Take(var.len, html.len - var.len);
            } else {
                writer.Write('$');
            }
        } else if (html.len) {
            writer.Write('$');
        }
    } while (html.len);

    return writer.Close();
}

static void InitRoutes()
{
    LogInfo("Init routes");

    routes.Clear();
    routes_alloc.ReleaseAll();

    // Static assets
    Assert(packer_assets.len > 0);
    for (const PackerAsset &asset: packer_assets) {
        const char *url = Fmt(&routes_alloc, "/static/%1", asset.name).ptr;
        const char *mime_type = http_GetMimeType(GetPathExtension(asset.name));

        routes.Set({url, "GET", Route::Matching::Exact, asset, mime_type});
    }

    // Patch HTML
    Route html = *routes.Find("/static/thop.html");
    {
        StreamReader st(html.u.st.asset.data, nullptr, html.u.st.asset.compression_type);
        HeapArray<uint8_t> buf(&routes_alloc);
        Assert(PatchTextFile(st, &buf));

        html.u.st.asset.data = buf.Leak();
    }

    // Catalogs
    for (const PackerAsset &desc: catalog_set.catalogs) {
        const char *url = Fmt(&routes_alloc, "/catalogs/%1", desc.name).ptr;
        routes.Set({url, "GET", Route::Matching::Exact, desc, http_GetMimeType(GetPathExtension(url))});
    }

    // Root
    routes.Set({"/", "GET", Route::Matching::Exact, html.u.st.asset, html.u.st.mime_type});

    // User
    routes.Set({"/login", "GET", Route::Matching::Walk, html.u.st.asset, html.u.st.mime_type});
    routes.Set({"/api/connect.json", "POST", Route::Matching::Exact, HandleConnect});
    routes.Set({"/api/disconnect.json", "POST", Route::Matching::Exact, HandleDisconnect});

    // MCO
    routes.Set({"/mco_casemix", "GET", Route::Matching::Walk, html.u.st.asset, html.u.st.mime_type});
    routes.Set({"/mco_list", "GET", Route::Matching::Walk, html.u.st.asset, html.u.st.mime_type});
    routes.Set({"/mco_pricing", "GET", Route::Matching::Walk, html.u.st.asset, html.u.st.mime_type});
    routes.Set({"/mco_results", "GET", Route::Matching::Walk, html.u.st.asset, html.u.st.mime_type});
    routes.Set({"/mco_tree", "GET", Route::Matching::Walk, html.u.st.asset, html.u.st.mime_type});
    routes.Set({"/api/mco_aggregate.json", "GET", Route::Matching::Exact, ProduceMcoAggregate});
    routes.Set({"/api/mco_results.json", "GET", Route::Matching::Exact, ProduceMcoResults});
    routes.Set({"/api/mco_settings.json", "GET", Route::Matching::Exact, ProduceMcoSettings});
    routes.Set({"/api/mco_diagnoses.json", "GET", Route::Matching::Exact, ProduceMcoDiagnoses});
    routes.Set({"/api/mco_procedures.json", "GET", Route::Matching::Exact, ProduceMcoProcedures});
    routes.Set({"/api/mco_ghm_ghs.json", "GET", Route::Matching::Exact, ProduceMcoGhmGhs});
    routes.Set({"/api/mco_tree.json", "GET", Route::Matching::Exact, ProduceMcoTree});

    // Special cases
    if (Route *favicon = routes.Find("/static/favicon.png"); favicon) {
        routes.Set({"/favicon.png", "GET", Route::Matching::Exact,
                    favicon->u.st.asset, favicon->u.st.mime_type});
    }
    routes.Remove("/static/favicon.png");
    routes.Remove("/static/thop.html");

    // We can use a global ETag because everything is in the binary
    {
        uint64_t buf[2];
        randombytes_buf(&buf, SIZE(buf));
        Fmt(etag, "%1%2", FmtHex(buf[0]).Pad0(-16), FmtHex(buf[1]).Pad0(-16));
    }
}

#ifndef NDEBUG
static bool UpdateStaticAssets()
{
    BlockAllocator temp_alloc;

    const char *filename = nullptr;
    const Span<const PackerAsset> *lib_assets = nullptr;
#ifdef _WIN32
    Assert(GetApplicationDirectory());
    filename = Fmt(&temp_alloc, "%1%/thop_assets.dll", GetApplicationDirectory()).ptr;
    {
        static FILETIME last_time;

        WIN32_FILE_ATTRIBUTE_DATA attr;
        if (!GetFileAttributesEx(filename, GetFileExInfoStandard, &attr)) {
            LogError("Cannot stat file '%1'", filename);
            return false;
        }

        if (attr.ftLastWriteTime.dwHighDateTime == last_time.dwHighDateTime &&
                attr.ftLastWriteTime.dwLowDateTime == last_time.dwLowDateTime)
            return true;
        last_time = attr.ftLastWriteTime;
    }

    HMODULE h = LoadLibrary(filename);
    if (!h) {
        LogError("Cannot load library '%1'", filename);
        return false;
    }
    DEFER { FreeLibrary(h); };

    lib_assets = (const Span<const PackerAsset> *)GetProcAddress(h, "packer_assets");
#else
    Assert(GetApplicationDirectory());
    filename = Fmt(&temp_alloc, "%1%/thop_assets.so", GetApplicationDirectory()).ptr;
    {
        static struct timespec last_time;

        struct stat sb;
        if (stat(filename, &sb) < 0) {
            LogError("Cannot stat file '%1'", filename);
            return false;
        }

#ifdef __APPLE__
        if (sb.st_mtimespec.tv_sec == last_time.tv_sec &&
                sb.st_mtimespec.tv_nsec == last_time.tv_nsec)
            return true;
        last_time = sb.st_mtimespec;
#else
        if (sb.st_mtim.tv_sec == last_time.tv_sec &&
                sb.st_mtim.tv_nsec == last_time.tv_nsec)
            return true;
        last_time = sb.st_mtim;
#endif
    }

    void *h = dlopen(filename, RTLD_LAZY | RTLD_LOCAL);
    if (!h) {
        LogError("Cannot load library '%1': %2", filename, dlerror());
        return false;
    }
    DEFER { dlclose(h); };

    lib_assets = (const Span<const PackerAsset> *)dlsym(h, "packer_assets");
#endif
    if (!lib_assets) {
        LogError("Cannot find symbol 'packer_assets' in library '%1'", filename);
        return false;
    }

    packer_assets.Clear();
    packer_alloc.ReleaseAll();
    for (const PackerAsset &asset: *lib_assets) {
        PackerAsset asset_copy;
        asset_copy.name = DuplicateString(asset.name, &packer_alloc).ptr;
        uint8_t *data_ptr = (uint8_t *)Allocator::Allocate(&packer_alloc, asset.data.len);
        memcpy(data_ptr, asset.data.ptr, (size_t)asset.data.len);
        asset_copy.data = {data_ptr, asset.data.len};
        asset_copy.compression_type = asset.compression_type;
        asset_copy.source_map = DuplicateString(asset.source_map, &packer_alloc).ptr;
        packer_assets.Append(asset_copy);
    }

    InitRoutes();

    LogInfo("Loaded assets from '%1'", SplitStrReverseAny(filename, PATH_SEPARATORS));
    return true;
}
#endif

static int QueueResponse(ConnectionInfo *conn, int code, http_Response *response)
{
    MHD_add_response_header(*response, "Referrer-Policy", "no-referrer");
    if (conn->user_mismatch) {
        DeleteSessionCookies(response);
    }

    return MHD_queue_response(conn->conn, (unsigned int)code, *response);
}

static int HandleHttpConnection(void *, MHD_Connection *conn2, const char *url, const char *method,
                                const char *, const char *upload_data, size_t *upload_data_size,
                                void **con_cls)
{
#ifndef NDEBUG
    UpdateStaticAssets();
#endif

    http_Response response = {};

    // Gather connection info
    ConnectionInfo *conn = (ConnectionInfo *)*con_cls;
    if (!conn) {
        conn = new ConnectionInfo;
        conn->conn = conn2;
        conn->user = CheckSessionUser(conn2, &conn->user_mismatch);
        *con_cls = conn;
    }

    // Handle base URL
    for (Size i = 0; thop_config.base_url[i]; i++, url++) {
        if (url[0] != thop_config.base_url[i]) {
            if (!url[0] && thop_config.base_url[i] == '/' && !thop_config.base_url[i + 1]) {
                response = MHD_create_response_from_buffer(0, nullptr, MHD_RESPMEM_PERSISTENT);
                MHD_add_response_header(response, "Location", thop_config.base_url);
                return QueueResponse(conn, 303, &response);
            } else {
                http_ProduceErrorPage(404, &response);
                return QueueResponse(conn, 404, &response);
            }
        }
    }
    url--;

    // Process POST data if any
    if (TestStr(method, "POST")) {
        if (!conn->pp) {
            conn->pp = MHD_create_post_processor(conn->conn, Kibibytes(32),
                                                 [](void *cls, enum MHD_ValueKind, const char *key,
                                                    const char *, const char *, const char *,
                                                    const char *data, uint64_t, size_t) {
                ConnectionInfo *conn = (ConnectionInfo *)cls;

                key = DuplicateString(key, &conn->temp_alloc).ptr;
                data = DuplicateString(data, &conn->temp_alloc).ptr;
                conn->post.Append(key, data);

                return MHD_YES;
            }, conn);
            if (!conn->pp) {
                http_ProduceErrorPage(422, &response);
                return QueueResponse(conn, 422, &response);
            }

            return MHD_YES;
        } else if (*upload_data_size) {
            if (MHD_post_process(conn->pp, upload_data, *upload_data_size) != MHD_YES) {
                http_ProduceErrorPage(422, &response);
                return QueueResponse(conn, 422, &response);
            }

            *upload_data_size = 0;
            return MHD_YES;
        }
    }

    // Negociate content encoding
    {
        uint32_t acceptable_encodings =
            http_ParseAcceptableEncodings(MHD_lookup_connection_value(conn->conn, MHD_HEADER_KIND,
                                                                      "Accept-Encoding"));

        if (acceptable_encodings & (1 << (int)CompressionType::Gzip)) {
            conn->compression_type = CompressionType::Gzip;
        } else if (acceptable_encodings) {
            conn->compression_type = (CompressionType)CountTrailingZeros(acceptable_encodings);
        } else {
            http_ProduceErrorPage(406, &response);
            return QueueResponse(conn, 406, &response);
        }
    }

    // Find appropriate route
    Route *route;
    bool try_cache;
    {
        Span<const char> url2 = url;

        route = routes.Find(url2);
        if (!route || !TestStr(route->method, method)) {
            while (url2.len > 1) {
                SplitStrReverse(url2, '/', &url2);

                Route *walk_route = routes.Find(url2);
                if (walk_route && walk_route->matching == Route::Matching::Walk &&
                        TestStr(walk_route->method, method)) {
                    route = walk_route;
                    break;
                }
            }

            if (!route) {
                http_ProduceErrorPage(404, &response);
                return QueueResponse(conn, 404, &response);
            }
        }

        try_cache = TestStr(method, "GET");
    }

    // Handle server-side cache validation (ETag)
    if (try_cache) {
        const char *client_etag = MHD_lookup_connection_value(conn->conn, MHD_HEADER_KIND, "If-None-Match");
        if (client_etag && TestStr(client_etag, etag)) {
            response = MHD_create_response_from_buffer(0, nullptr, MHD_RESPMEM_PERSISTENT);
            return QueueResponse(conn, 304, &response);
        }
    }

    // Execute route
    int code = 0;
    switch (route->type) {
        case Route::Type::Static: {
            code = http_ProduceStaticAsset(route->u.st.asset.data, route->u.st.asset.compression_type,
                                           route->u.st.mime_type, conn->compression_type, &response);
            if (code == 200 && route->u.st.asset.source_map) {
                MHD_add_response_header(response, "SourceMap", route->u.st.asset.source_map);
            }
        } break;

        case Route::Type::Function: {
            code = route->u.func(conn, url, &response);
        } break;
    }
    DebugAssert(response.response);

    // Add caching information
    if (try_cache) {
#ifndef NDEBUG
        response.flags |= (int)http_Response::Flag::DisableCache;
#endif

        if (!(response.flags & (int)http_Response::Flag::DisableCache)) {
            MHD_add_response_header(response, "Cache-Control", "max-age=3600");

            if (etag[0] && !(response.flags & (int)http_Response::Flag::DisableETag)) {
                MHD_add_response_header(response, "ETag", etag);
            }
        } else {
            MHD_add_response_header(response, "Cache-Control", "no-store");
        }
    }

    return QueueResponse(conn, code, &response);
}

static void ReleaseConnectionData(void **con_cls, MHD_RequestTerminationCode)
{
    ConnectionInfo *conn = (ConnectionInfo *)*con_cls;

    if (conn) {
        if (conn->pp) {
            MHD_destroy_post_processor(conn->pp);
        }
        delete conn;
    }
}

int main(int argc, char **argv)
{
    BlockAllocator temp_alloc;

    static const auto PrintUsage = [](FILE *fp) {
        PrintLn(fp, R"(Usage: thop [options] [stay_file ..]

Options:
    -C, --config_file <file>     Set configuration file
                                 (default: <executable_dir>%/profile%/thop.ini)

        --profile_dir <dir>      Set profile directory
        --table_dir <dir>        Add table directory

        --mco_auth_file <file>   Set MCO authorization file
                                 (default: <profile_dir>%/mco_authorizations.ini
                                           <profile_dir>%/mco_authorizations.txt)

        --port <port>            Change web server port
                                 (default: %1))
        --base_url <url>         Change base URL
                                 (default: %2))",
                thop_config.port, thop_config.base_url);
    };

    if (sodium_init() < 0) {
        LogError("Failed to initialize libsodium");
        return 1;
    }

    // Find config filename
    const char *config_filename = nullptr;
    {
        OptionParser opt(argc, argv, (int)OptionParser::Flag::SkipNonOptions);

        while (opt.Next()) {
            if (opt.Test("--help")) {
                PrintUsage(stdout);
                return 0;
            } else if (opt.Test("-C", "--config_file", OptionType::OptionalValue)) {
                config_filename = opt.current_value;
            }
        }

        if (!config_filename) {
            const char *app_directory = GetApplicationDirectory();
            if (app_directory) {
                const char *test_filename = Fmt(&thop_config.str_alloc, "%1%/profile/thop.ini", app_directory).ptr;
                if (TestPath(test_filename, FileType::File)) {
                    config_filename = test_filename;
                }
            }
        }
    }

    // Load config file
    if (config_filename && !LoadConfig(config_filename, &thop_config))
        return 1;

    // Parse arguments
    {
        OptionParser opt(argc, argv);

        while (opt.Next()) {
            if (opt.Test("-C", "--config_file", OptionType::Value)) {
                // Already handled
            } else if (opt.Test("--profile_dir", OptionType::Value)) {
                thop_config.profile_directory = opt.current_value;
            } else if (opt.Test("--table_dir", OptionType::Value)) {
                thop_config.table_directories.Append(opt.current_value);
            } else if (opt.Test("--mco_auth_file", OptionType::Value)) {
                thop_config.mco_authorization_filename = opt.current_value;
            } else if (opt.Test("--port", OptionType::Value)) {
                if (!ParseDec(opt.current_value, &thop_config.port))
                    return 1;
            } else if (opt.Test("--base_url", OptionType::Value)) {
                thop_config.base_url = opt.current_value;
            } else {
                LogError("Cannot handle option '%1'", opt.current_option);
                return 1;
            }
        }

        opt.ConsumeNonOptions(&thop_config.mco_stay_filenames);
    }

    // Configuration errors
    {
        bool valid = true;

        if (!thop_config.profile_directory) {
            LogError("Profile directory is missing");
            valid = false;
        }
        if (!thop_config.table_directories.len) {
            LogError("No table directory is specified");
            valid = false;
        }
        if (thop_config.port < 1 || thop_config.port > UINT16_MAX) {
            LogError("HTTP port %1 is invalid (range: 1 - %2)", thop_config.port, UINT16_MAX);
            valid = false;
        }
        if (thop_config.threads < 0 || thop_config.threads > 128) {
            LogError("HTTP threads %1 is invalid (range: 0 - 128)", thop_config.threads);
            valid = false;
        }
        if (thop_config.base_url[0] != '/' ||
                thop_config.base_url[strlen(thop_config.base_url) - 1] != '/') {
            LogError("Base URL '%1' does not start and end with '/'", thop_config.base_url);
            valid = false;
        }

        if (!valid)
            return 1;
    }

    // Do we have any site-specific (sensitive) data?
    thop_has_casemix = thop_config.mco_stay_directories.len || thop_config.mco_stay_filenames.len;

    // Init
    if (thop_has_casemix) {
        if (!InitMcoProfile(thop_config.profile_directory, thop_config.mco_authorization_filename))
            return 1;
        if (!InitUsers(thop_config.profile_directory))
            return 1;
    }
    if (!InitCatalogSet(thop_config.table_directories))
        return 1;
    if (!InitMcoTables(thop_config.table_directories))
        return 1;
    if (thop_has_casemix) {
        if (!InitMcoStays(thop_config.mco_stay_directories, thop_config.mco_stay_filenames))
            return 1;
    }

#ifndef NDEBUG
    if (!UpdateStaticAssets())
        return 1;
#else
    InitRoutes();
#endif

    http_Daemon daemon;
    daemon.release_func = ReleaseConnectionData;
    if (!daemon.Start(thop_config.ip_stack, thop_config.port, thop_config.threads,
                      HandleHttpConnection))
        return 1;
    LogInfo("Listening on port %1 (%2 stack)",
            thop_config.port, IPStackNames[(int)thop_config.ip_stack]);

    WaitForConsoleInterruption();

    LogInfo("Exit");
    return 0;
}
