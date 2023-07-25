// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "src/core/libcc/libcc.hh"
#include "src/core/libnet/libnet.hh"
extern "C" {
    #include "vendor/cmark-gfm/src/cmark-gfm.h"
    #include "vendor/cmark-gfm/extensions/cmark-gfm-core-extensions.h"
    #include "vendor/cmark-gfm/extensions/table.h"
}
#include "vendor/libsodium/src/libsodium/include/sodium/crypto_hash_sha256.h"

namespace RG {

enum class UrlFormat {
    Pretty,
    PrettySub,
    Ugly
};
static const char *const UrlFormatNames[] = {
    "Pretty",
    "PrettySub",
    "Ugly"
};

struct FileHash {
    const char *path;
    uint8_t sha256[32];

    RG_HASHTABLE_HANDLER(FileHash, path);
};

struct PageSection {
    const char *id;
    const char *title;
    int level = 0;
};

struct PageData {
    const char *src_filename;

    const char *title;
    const char *menu;
    const char *description;
    HeapArray<PageSection> sections;

    std::shared_ptr<const char> html_buf;
    Span<const char> html;

    const char *name;
    const char *url;
};

static int32_t DecodeUtf8Unsafe(const char *str);

static const HashMap<int32_t, const char *> replacements = {
    { DecodeUtf8Unsafe("Ç"), "c" },
    { DecodeUtf8Unsafe("È"), "e" },
    { DecodeUtf8Unsafe("É"), "e" },
    { DecodeUtf8Unsafe("Ê"), "e" },
    { DecodeUtf8Unsafe("Ë"), "e" },
    { DecodeUtf8Unsafe("À"), "a" },
    { DecodeUtf8Unsafe("Å"), "a" },
    { DecodeUtf8Unsafe("Â"), "a" },
    { DecodeUtf8Unsafe("Ä"), "a" },
    { DecodeUtf8Unsafe("Î"), "i" },
    { DecodeUtf8Unsafe("Ï"), "i" },
    { DecodeUtf8Unsafe("Ù"), "u" },
    { DecodeUtf8Unsafe("Ü"), "u" },
    { DecodeUtf8Unsafe("Û"), "u" },
    { DecodeUtf8Unsafe("Ú"), "u" },
    { DecodeUtf8Unsafe("Ñ"), "n" },
    { DecodeUtf8Unsafe("Ô"), "o" },
    { DecodeUtf8Unsafe("Ó"), "o" },
    { DecodeUtf8Unsafe("Ö"), "o" },
    { DecodeUtf8Unsafe("Œ"), "oe" },
    { DecodeUtf8Unsafe("Ÿ"), "y" },

    { DecodeUtf8Unsafe("ç"), "c" },
    { DecodeUtf8Unsafe("è"), "e" },
    { DecodeUtf8Unsafe("é"), "e" },
    { DecodeUtf8Unsafe("ê"), "e" },
    { DecodeUtf8Unsafe("ë"), "e" },
    { DecodeUtf8Unsafe("à"), "a" },
    { DecodeUtf8Unsafe("å"), "a" },
    { DecodeUtf8Unsafe("â"), "a" },
    { DecodeUtf8Unsafe("ä"), "a" },
    { DecodeUtf8Unsafe("î"), "i" },
    { DecodeUtf8Unsafe("ï"), "i" },
    { DecodeUtf8Unsafe("ù"), "u" },
    { DecodeUtf8Unsafe("ü"), "u" },
    { DecodeUtf8Unsafe("û"), "u" },
    { DecodeUtf8Unsafe("ú"), "u" },
    { DecodeUtf8Unsafe("ñ"), "n" },
    { DecodeUtf8Unsafe("ô"), "o" },
    { DecodeUtf8Unsafe("ó"), "o" },
    { DecodeUtf8Unsafe("ö"), "o" },
    { DecodeUtf8Unsafe("œ"), "oe" },
    { DecodeUtf8Unsafe("ÿ"), "y" }
};

static int32_t DecodeUtf8Unsafe(const char *str)
{
    int32_t uc = -1;
    Size bytes = DecodeUtf8(str, &uc);

    RG_ASSERT(bytes > 0);
    RG_ASSERT(!str[bytes]);

    return uc;
}

static const char *FileNameToPageName(const char *filename, Allocator *alloc)
{
    // File name and extension
    Span<const char> name = SplitStrReverseAny(filename, RG_PATH_SEPARATORS);
    SplitStrReverse(name, '.', &name);

    // Remove leading number and underscore if any
    {
        const char *after_number;
        strtol(name.ptr, const_cast<char **>(&after_number), 10);
        if (after_number && after_number[0] == '_') {
            name = MakeSpan(after_number + 1, name.end());
        }
    }

    // Filter out unwanted characters
    char *name2 = DuplicateString(name, alloc).ptr;
    for (Size i = 0; name2[i]; i++) {
        name2[i] = IsAsciiAlphaOrDigit(name2[i]) ? name2[i] : '_';
    }

    return name2;
}

static const char *TextToID(Span<const char> text, Allocator *alloc)
{
    Span<char> id = AllocateSpan<char>(alloc, text.len + 1);

    Size offset = 0;
    Size len = 0;
    bool skip_special = false;

    while (offset < text.len) {
        int32_t uc;
        Size bytes = DecodeUtf8(text, offset, &uc);

        if (bytes == 1) {
            if (IsAsciiAlphaOrDigit((char)uc)) {
                id[len++] = LowerAscii((char)uc);
                skip_special = false;
            } else if (!skip_special) {
                id[len++] = '_';
                skip_special = true;
            }
        } else if (bytes > 1) {
            const char *ptr = replacements.FindValue(uc, nullptr);
            Size expand = bytes;

            if (ptr) {
                expand = strlen(ptr);
            } else {
                ptr = text.ptr + offset;
            }

            memcpy_safe(id.ptr + len, ptr, (size_t)expand);
            len += expand;

            skip_special = false;
        } else {
            LogError("Illegal UTF-8 sequence");
            return nullptr;
        }

        offset += bytes;
    }

    while (len > 1 && id[len - 1] == '_') {
        len--;
    }
    if (!len)
        return nullptr;

    id.ptr[len] = 0;

    return id.ptr;
}

static bool SpliceWithChecksum(StreamReader *reader, StreamWriter *writer, uint8_t out_hash[32])
{
    if (!reader->IsValid())
        return false;

    crypto_hash_sha256_state state;
    crypto_hash_sha256_init(&state);

    do {
        LocalArray<uint8_t, 16384> buf;
        buf.len = reader->Read(buf.data);
        if (buf.len < 0)
            return false;

        if (!writer->Write(buf))
            return false;
        crypto_hash_sha256_update(&state, buf.data, buf.len);
    } while (!reader->IsEOF());

    if (!writer->Close())
        return false;
    crypto_hash_sha256_final(&state, out_hash);

    return true;
}

// XXX: Resolve page links in content
static bool RenderPageContent(PageData *page, const HashTable<const char *, const FileHash *> &assets, Allocator *alloc)
{

    HeapArray<char> content;
    if (ReadFile(page->src_filename, Mebibytes(8), &content) < 0)
        return false;
    Span<const char> remain = TrimStr(content.As());

    // Parse pseudo-YAML intro
    if (StartsWith(remain, "---\n") || StartsWith(remain, "---\r\n")) {
        SplitStrLine(remain, &remain);

        while (remain.len) {
            Span<const char> line = SplitStrLine(remain, &remain);
            if (line == "---")
                break;
            line = TrimStr(line);

            Span<const char> value;
            Span<const char> key = TrimStr(SplitStr(line, ':', &value));
            value = TrimStr(value);

            if (key == "title") {
                page->title = DuplicateString(value, alloc).ptr;
            } else if (key == "description") {
                page->description = DuplicateString(value, alloc).ptr;
            } else if (key == "menu") {
                page->menu = DuplicateString(value, alloc).ptr;
            } else {
                LogError("%1: Unknown attribute '%2'", page->src_filename, key);
            }
        }
    }

    cmark_gfm_core_extensions_ensure_registered();

    // Prepare markdown parser
    cmark_parser *parser = cmark_parser_new(CMARK_OPT_DEFAULT);
    RG_DEFER { cmark_parser_free(parser); };

    // Enable syntax extensions
    {
        static const char *const extensions[] = {
            "table",
            "strikethrough"
        };

        for (const char *name: extensions) {
            cmark_syntax_extension *ext = cmark_find_syntax_extension(name);

            if (!ext) {
                LogError("Cannot find Markdown extension '%1'", name);
                return false;
            }
            if (!cmark_parser_attach_syntax_extension(parser, ext)) {
                LogError("Failed to enable Markdown extension '%1'", name);
                return false;
            }
        }
    }

    // Parse markdown
    {
        const auto write = [&](Span<const uint8_t> buf) {
            cmark_parser_feed(parser, (const char *)buf.ptr, buf.len);
            return true;
        };

        StreamWriter writer(write, "<buffer>");

        bool success = PatchFile(remain.As<const uint8_t>(), &writer,
                                 [&](Span<const char> expr, StreamWriter *writer) {
            Span<const char> key = TrimStr(expr);

            if (key == "RANDOM") {
                Print(writer, "%1", FmtRandom(8));
            } else if (StartsWith(key, "ASSET ")) {
                Span<const char> path = TrimStr(key.Take(6, key.len - 6));
                const FileHash *hash = assets.FindValue(path, nullptr);

                if (hash) {
                    FmtArg suffix = FmtSpan(MakeSpan(hash->sha256, 8), FmtType::BigHex, "").Pad0(-2);
                    Print(writer, "/static/%1?%2", path, suffix);
                } else {
                    Print(writer, "/static/%1", path);
                }
            } else {
                Print(writer, "{{%1}}", expr);
            }
        });

        if (!success)
            return false;
        if (!writer.Close())
            return false;
    }

    // Finalize parsing
    cmark_node *root = cmark_parser_finish(parser);
    RG_DEFER { cmark_node_free(root); };

    // Customize rendered tree
    {
        cmark_iter *iter = cmark_iter_new(root);
        RG_DEFER { cmark_iter_free(iter); };

        bool has_main = false;

        cmark_event_type event;
        while ((event = cmark_iter_next(iter)) != CMARK_EVENT_DONE) {
            cmark_node *node = cmark_iter_get_node(iter);
            cmark_node_type type = cmark_node_get_type(node);

            // We want everything before the first title to live outside main
            if (!has_main && event == CMARK_EVENT_ENTER && cmark_node_get_type(node) == CMARK_NODE_HEADING) {
                cmark_node *frag = cmark_node_new(CMARK_NODE_HTML_BLOCK);
                cmark_node_set_literal(frag, "<main>");
                cmark_node_insert_before(node, frag);

                has_main = true;
            }

            // List sections and add anchors
            if (event == CMARK_EVENT_EXIT && type == CMARK_NODE_HEADING) {
                int level = cmark_node_get_heading_level(node);
                cmark_node *child = cmark_node_first_child(node);

                if (level < 3 && cmark_node_get_type(child) == CMARK_NODE_TEXT) {
                    PageSection sec = {};

                    sec.level = level;
                    sec.title = DuplicateString(cmark_node_get_literal(child), alloc).ptr;
                    sec.id = TextToID(sec.title, alloc);

                    page->sections.Append(sec);

                    cmark_node *frag = cmark_node_new(CMARK_NODE_HTML_INLINE);
                    cmark_node_set_literal(frag, Fmt(alloc, "<a id=\"%1\"></a>", sec.id).ptr);
                    cmark_node_prepend_child(node, frag);
                }
            }

            // Format our own code blocks
            if (event == CMARK_EVENT_ENTER && type == CMARK_NODE_CODE_BLOCK) {
                Span<const char> remain = TrimStr(cmark_node_get_literal(node));

                HeapArray<char> code;

                Fmt(&code, "<pre>\n");
                while (remain.len) {
                    Span<const char> line = SplitStrLine(remain, &remain);
                    Fmt(&code, "<span class=\"line\">%1</span>\n", line);
                }
                Fmt(&code, "</pre>\n");

                cmark_node *block = cmark_node_new(CMARK_NODE_CUSTOM_BLOCK);
                cmark_node_set_on_enter(block, code.ptr);

                if (cmark_node_replace(node, block)) {
                    cmark_node_free(node);
                } else {
                    cmark_node_free(block);

                    LogError("Failed to replace code block");
                    return false;
                }
            }
        }

        if (has_main) {
            cmark_node *frag = cmark_node_new(CMARK_NODE_HTML_BLOCK);
            cmark_node_set_literal(frag, "</main>");
            cmark_node_append_child(root, frag);
        }
    }

    // Render to HTML
    page->html = cmark_render_html(root, CMARK_OPT_UNSAFE, nullptr);
    page->html_buf.reset((char *)page->html.ptr, free);

    return true;
}

static bool RenderFullPage(Span<const uint8_t> html, Span<const PageData> pages, Size page_idx,
                           const HashTable<const char *, const FileHash *> &assets,
                           const char *dest_filename)
{
    StreamWriter st(dest_filename, (int)StreamWriterFlag::Atomic);

    const PageData &page = pages[page_idx];

    bool success = PatchFile(html, &st, [&](Span<const char> expr, StreamWriter *writer) {
        Span<const char> key = TrimStr(expr);

        if (key == "TITLE") {
            writer->Write(page.title);
        } else if (key == "DESCRIPTION") {
            writer->Write(page.description);
        } else if (key == "RANDOM") {
            Print(writer, "%1", FmtRandom(8));
        } else if (StartsWith(key, "ASSET ")) {
            Span<const char> path = TrimStr(key.Take(6, key.len - 6));
            const FileHash *hash = assets.FindValue(path, nullptr);

            if (hash) {
                FmtArg suffix = FmtSpan(MakeSpan(hash->sha256, 8), FmtType::BigHex, "").Pad0(-2);
                Print(writer, "/static/%1?%2", path, suffix);
            } else {
                Print(writer, "/static/%1", path);
            }
        } else if (key == "LINKS") {
            for (Size i = 0; i < pages.len; i++) {
                const PageData *menu_page = &pages[i];

                if (!menu_page->menu)
                    continue;

                if (strchr(menu_page->menu, '/')) {
                    Span<const char> category = TrimStr(SplitStr(menu_page->menu, '/'));

                    Size j = i + 1;
                    while (j < pages.len) {
                        Span<const char> new_category = TrimStr(SplitStr(pages[j].menu, '/'));

                        if (new_category != category)
                            break;
                        j++;
                    }

                    bool active = (page_idx >= i && page_idx < j);
                    PrintLn(writer, "<li><a href=\"#\"%1>%2</a><div>", active ? " class=\"active\"" : "", category);

                    for (; i < j; i++) {
                        menu_page = &pages[i];

                        const char *item = TrimStrLeft(strchr(menu_page->menu, '/') + 1).ptr;
                        SplitStr(menu_page->menu, '/', &item);

                        bool active = (page_idx == i);
                        PrintLn(writer, "<a href=\"%1\"%2>%3</a>", menu_page->url, active ? " class=\"active\"" : "", item);
                    }
                    i = j - 1;

                    PrintLn(writer, "</div></li>");
                } else {
                    bool active = (page_idx == i);
                    PrintLn(writer, "<li><a href=\"%1\"%2>%3</a></li>", menu_page->url, active ? " class=\"active\"" : "", menu_page->menu);
                }
            }
        } else if (key == "TOC") {
            if (page.sections.len > 1) {
                PrintLn(writer, "<nav id=\"side\"><menu>");

                for (const PageSection &sec: page.sections) {
                    PrintLn(writer, "<li><a href=\"#%1\" class=\"lv%2\">%3</a></li>",
                                    sec.id, sec.level, sec.title);
                }

                PrintLn(writer, "</menu></nav>");
            }
        } else if (key == "CONTENT") {
            writer->Write(page.html);
        } else {
            Print(writer, "{{%1}}", expr);
        }
    });

    if (!success)
        return false;
    if (!st.Close())
        return false;

    return true;
}

static bool BuildAll(const char *input_dir, const char *template_dir, UrlFormat urls,
                     const char *output_dir, bool gzip)
{
    BlockAllocator temp_alloc;

    // Output directory
    if (!MakeDirectory(output_dir, false))
        return false;
    LogInfo("Template: %!..+%1%!0", template_dir);
    LogInfo("Output directory: %!..+%1%!0", output_dir);

    const char *static_directories[] = {
        Fmt(&temp_alloc, "%1%/static", input_dir).ptr,
        Fmt(&temp_alloc, "%1%/static", template_dir).ptr
    };

    // Copy template assets
    BucketArray<FileHash> hashes;
    HashTable<const char *, const FileHash *> hashes_map;
    {
        Async async;

        for (const char *static_directory: static_directories) {
            if (TestFile(static_directory, FileType::Directory)) {
                HeapArray<const char *> static_filenames;
                if (!EnumerateFiles(static_directory, nullptr, 3, 1024, &temp_alloc, &static_filenames))
                    return false;

                Size prefix_len = strlen(static_directory);

                for (const char *src_filename: static_filenames) {
                    const char *basename = TrimStrLeft(src_filename + prefix_len, RG_PATH_SEPARATORS).ptr;

                    const char *dest_filename = Fmt(&temp_alloc, "%1%/static%/%2", output_dir, basename).ptr;
                    const char *gzip_filename = Fmt(&temp_alloc, "%1.gz", dest_filename).ptr;

                    FileHash *hash = hashes.AppendDefault();
                    hash->path = basename;

                    async.Run([=]() {
                        if (!EnsureDirectoryExists(dest_filename))
                            return false;

                        // Open ahead of time because src_filename won't stay valid
                        StreamReader reader(src_filename);

                        // Copy raw file
                        {
                            StreamWriter writer(dest_filename, (int)StreamWriterFlag::Atomic);

                            if (!SpliceWithChecksum(&reader, &writer, hash->sha256))
                                return false;
                            if (!writer.Close())
                                return false;
                        }

                        // Create gzipped version
                        if (gzip && http_ShouldCompressFile(dest_filename)) {
                            reader.Rewind();

                            StreamWriter writer(gzip_filename, (int)StreamWriterFlag::Atomic, CompressionType::Gzip);

                            if (!SpliceStream(&reader, -1, &writer))
                                return false;
                            if (!writer.Close())
                                return false;
                        } else {
                            UnlinkFile(gzip_filename);
                        }

                        return true;
                    });
                }
            }
        }

        if (!async.Sync())
            return false;

        for (const FileHash &hash: hashes) {
            hashes_map.Set(&hash);
        }
    }

    // List input files
    HeapArray<const char *> page_filenames;
    if (!EnumerateFiles(input_dir, "*.md", 0, 1024, &temp_alloc, &page_filenames))
        return false;
    std::sort(page_filenames.begin(), page_filenames.end(),
              [](const char *filename1, const char *filename2) { return CmpStr(filename1, filename2) < 0; });

    // List pages
    HeapArray<PageData> pages;
    {
        HashMap<const char *, Size> pages_map;

        for (const char *filename: page_filenames) {
            PageData page = {};

            page.src_filename = filename;
            if (!RenderPageContent(&page, hashes_map, &temp_alloc))
                return false;
            page.name = FileNameToPageName(filename, &temp_alloc);

            if (TestStr(page.name, "index")) {
                page.url = "/";
            } else {
                switch (urls) {
                    case UrlFormat::Pretty:
                    case UrlFormat::PrettySub: { page.url = Fmt(&temp_alloc, "/%1", page.name).ptr; } break;
                    case UrlFormat::Ugly: { page.url = Fmt(&temp_alloc, "/%1.html", page.name).ptr; } break;
                }
            }

            bool valid = true;
            if (!page.name) {
                LogError("%1: Page with empty name", page.src_filename);
                valid = false;
            }
            if (!page.title) {
                LogError("%1: Ignoring page without title", page.src_filename);
                valid = false;
            }
            if (Size prev_idx = pages_map.FindValue(page.name, -1); prev_idx >= 0) {
                LogError("%1: Ignoring duplicate of '%2'",
                         page.src_filename, pages[prev_idx].src_filename);
                valid = false;
            }

            if (valid) {
                pages_map.Set(page.name, pages.len);
                pages.Append(page);
            }
        }
    }

    // Load HTML templates
    HeapArray<uint8_t> page_html;
    HeapArray<uint8_t> index_html;
    {
        const char *page_filename = Fmt(&temp_alloc, "%1%/page.html", template_dir).ptr;
        const char *index_filename = Fmt(&temp_alloc, "%1%/index.html", template_dir).ptr;

        if (!ReadFile(page_filename, Mebibytes(1), &page_html))
            return false;
        if (TestFile(index_filename)) {
            if (!ReadFile(index_filename, Mebibytes(1), &index_html))
                return false;
        } else {
            index_html.Append(page_html);
        }
    }

    // Output fully-formed pages
    {
        Async async;

        for (Size i = 0; i < pages.len; i++) {
            const PageData &page = pages[i];

            const char *dest_filename;
            if (urls == UrlFormat::PrettySub && !TestStr(pages[i].name, "index")) {
                dest_filename = Fmt(&temp_alloc, "%1%/%2%/index.html", output_dir, page.name).ptr;
                if (!EnsureDirectoryExists(dest_filename))
                    return false;
            } else {
                dest_filename = Fmt(&temp_alloc, "%1%/%2.html", output_dir, page.name).ptr;
            }

            const char *gzip_filename = Fmt(&temp_alloc, "%1.gz", dest_filename).ptr;

            async.Run([=, &pages]() {
                Span<const uint8_t> html = TestStr(pages[i].name, "index") ? index_html : page_html;

                if (!RenderFullPage(html, pages, i, hashes_map, dest_filename))
                    return false;

                if (gzip) {
                    StreamReader reader(dest_filename);
                    StreamWriter writer(gzip_filename, (int)StreamWriterFlag::Atomic, CompressionType::Gzip);

                    if (!SpliceStream(&reader, Megabytes(4), &writer))
                        return false;
                    if (!writer.Close())
                        return false;
                } else {
                    UnlinkFile(gzip_filename);
                }

                return true;
            });
        }

        if (!async.Sync())
            return false;
    }

    return true;
}

int Main(int argc, char *argv[])
{
    RG_CRITICAL(argc >= 1, "First argument is missing");

    BlockAllocator temp_alloc;

    // Options
    const char *input_dir = nullptr;
    const char *template_dir = {};
    const char *output_dir = nullptr;
    bool gzip = false;
    UrlFormat urls = UrlFormat::Pretty;

    const auto print_usage = [=](FILE *fp) {
        PrintLn(fp, R"(Usage: %!..+%1 <input_dir> -O <output_dir>%!0

Options:
    %!..+-T, --template_dir <dir>%!0     Set template directory

    %!..+-O, --output_dir <dir>%!0       Set output directory
        %!..+--gzip%!0                   Create static gzip files

    %!..+-u, --urls <FORMAT>%!0          Change URL format (%2)
                                 %!D..(default: %3)%!0)",
                FelixTarget, FmtSpan(UrlFormatNames), UrlFormatNames[(int)urls]);
    };

    // Handle version
    if (argc >= 2 && TestStr(argv[1], "--version")) {
        PrintLn("%!R..%1%!0 %!..+%2%!0", FelixTarget, FelixVersion);
        PrintLn("Compiler: %1", FelixCompiler);
        return 0;
    }

    // Parse options
    {
        OptionParser opt(argc, argv);

        while (opt.Next()) {
            if (opt.Test("--help")) {
                print_usage(stdout);
                return 0;
            } else if (opt.Test("-T", "--template_dir", OptionType::Value)) {
                template_dir = opt.current_value;
            } else if (opt.Test("-O", "--output_dir", OptionType::Value)) {
                output_dir = opt.current_value;
            } else if (opt.Test("--gzip")) {
                gzip = true;
            } else if (opt.Test("-u", "--urls", OptionType::Value)) {
                if (!OptionToEnum(UrlFormatNames, opt.current_value, &urls)) {
                    LogError("Unknown URL format '%1'", opt.current_value);
                    return true;
                }
            } else {
                LogError("Cannot handle option '%1'", opt.current_option);
                return 1;
            }
        }

        input_dir = opt.ConsumeNonOption();
    }

    // Check arguments
    {
        bool valid = true;

        if (!input_dir) {
            LogError("Missing input directory");
            valid = false;
        }
        if (!output_dir) {
            LogError("Missing output directory");
            valid = false;
        }

        if (!valid)
            return 1;
    }

    if (!template_dir) {
        const char *directory = Fmt(&temp_alloc, "%1%/template", input_dir).ptr;

        if (!TestFile(directory, FileType::Directory)) {
            LogError("Missing template directory");
            return 1;
        }

        template_dir = directory;
    }

    if (!BuildAll(input_dir, template_dir, urls, output_dir, gzip))
        return 1;

    LogInfo("Done!");
    return 0;
}

}

// C++ namespaces are stupid
int main(int argc, char **argv) { return RG::RunApp(argc, argv); }
