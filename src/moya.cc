#include "kutil.hh"
#include "classifier.hh"
#include "dump.hh"
#include "stays.hh"
#include "tables.hh"

static const char *const MainUsageText =
R"(Usage: moya command [options]

Commands:
    classify                 Run classifier on patient data
    dump                     Dump available classifier data tables
    list                     Print diagnosis and procedure lists
    pricing                  Print GHS pricing tables
    show                     A
    tables                   B

Global options:
    -t, --table <filename>   Load table file
    -T, --table-dir <dir>    Load table directory
    -d, --table-date <date>  Table date)";

struct ListSpecifier {
    enum class Table {
        Diagnoses,
        Procedures
    };
    enum class Type {
        Mask
    };

    bool valid;

    Table table;
    Type type;
    union {
        struct {
            uint8_t offset;
            uint8_t mask;
        } mask;
    } u;

    static ListSpecifier FromString(const char *spec_str)
    {
        ListSpecifier spec = {};

        if (!spec_str[0] || !spec_str[1])
            goto error;

        switch (spec_str[0]) {
            case 'd': case 'D': { spec.table = ListSpecifier::Table::Diagnoses; } break;
            case 'a': case 'A': { spec.table = ListSpecifier::Table::Procedures; } break;

            default:
                goto error;
        }

        switch (spec_str[1]) {
            case '$': {
                spec.type = ListSpecifier::Type::Mask;
                if (sscanf(spec_str + 2, "%" SCNu8 ".%" SCNu8,
                           &spec.u.mask.offset, &spec.u.mask.mask) != 2)
                    goto error;
            } break;

            default:
                goto error;
        }

        spec.valid = true;
        return spec;

error:
        LogError("Malformed list specifier '%1'", spec_str);
        return spec;
    }

    bool IsValid() const { return valid; }

    bool Match(ArrayRef<const uint8_t> values) const
    {
        switch (type) {
            case Type::Mask: {
                return u.mask.offset < values.len &&
                       values[u.mask.offset] & u.mask.mask;
            } break;
        }

        Assert(false);
    }
};

static Date main_set_date = {};
static HeapArray<const char *> main_table_filenames;
static ClassifierSet main_classifier_set = {};

static const ClassifierSet *GetMainClassifierSet()
{
    if (!main_classifier_set.indexes.len) {
        if (!main_table_filenames.len) {
            LogError("No table provided");
            return nullptr;
        }
        LoadClassifierSet(main_table_filenames, &main_classifier_set);
        if (!main_classifier_set.indexes.len)
            return nullptr;
    }

    return &main_classifier_set;
}

static bool HandleMainOption(OptionParser &opt_parser, Allocator &temp_alloc,
                             const char *usage_str)
{
    if (TestOption(opt_parser.current_option, "-T", "--table-dir")) {
        if (!opt_parser.RequireOptionValue(MainUsageText))
            return false;

        return EnumerateDirectoryFiles(opt_parser.current_value, "*.tab", temp_alloc,
                                       &main_table_filenames, 1024);
    } else if (TestOption(opt_parser.current_option, "-t", "--table-file")) {
        if (!opt_parser.RequireOptionValue(MainUsageText))
            return false;

        main_table_filenames.Append(opt_parser.current_value);
        return true;
    } else if (TestOption(opt_parser.current_option, "-d", "--table-date")) {
        if (!opt_parser.RequireOptionValue(MainUsageText))
            return false;

        main_set_date = Date::FromString(opt_parser.current_value);
        return !!main_set_date.value;
    } else {
        PrintLn(stderr, "Unknown option '%1'", opt_parser.current_option);
        PrintLn(stderr, "%1", usage_str);
        return false;
    }
}

static bool RunClassify(ArrayRef<const char *> arguments)
{
    static const char *const UsageText =
R"(Usage: moya classify [options] stay_file ...)";

    Allocator temp_alloc;
    OptionParser opt_parser(arguments);

    HeapArray<const char *> filenames;
    {
        const char *opt;
        while ((opt = opt_parser.ConsumeOption())) {
            if (TestOption(opt, "--help")) {
                PrintLn(stdout, UsageText);
                return true;
            } else if (!HandleMainOption(opt_parser, temp_alloc, UsageText)) {
                return false;
            }
        }

        opt_parser.ConsumeNonOptions(&filenames);
        if (!filenames.len) {
            PrintLn(stderr, "No filename provided");
            PrintLn(stderr, UsageText);
            return false;
        }
    }

    StaySet stay_set;
    {
        StaySetBuilder stay_set_builder;
        if (!stay_set_builder.LoadJson(filenames))
            return false;
        if (!stay_set_builder.Finish(&stay_set))
            return false;
    }
    PrintLn("%1 -- %2 -- %3",
            stay_set.stays.len, stay_set.store.diagnoses.len, stay_set.store.procedures.len);

    return true;
}

static bool RunDump(ArrayRef<const char *> arguments)
{
    static const char *const UsageText =
R"(Usage: moya dump [options] [filename] ...

Specific options:
    -h, --headers            Print only table headers)";

    Allocator temp_alloc;
    OptionParser opt_parser(arguments);

    bool headers = false;
    HeapArray<const char *> filenames;
    {
        const char *opt;
        while ((opt = opt_parser.ConsumeOption())) {
            if (TestOption(opt, "--help")) {
                PrintLn(stdout, "%1", UsageText);
                return true;
            } else if (TestOption(opt, "-h", "--headers")) {
                headers = true;
            } else if (!HandleMainOption(opt_parser, temp_alloc, UsageText)) {
                return false;
            }
        }

        opt_parser.ConsumeNonOptions(&filenames);
    }

    if (filenames.len) {
        ClassifierSet classifier_set;
        if (!LoadClassifierSet(filenames, &classifier_set) && !classifier_set.indexes.len)
            return false;
        DumpClassifierSet(classifier_set, !headers);
    } else {
        const ClassifierSet *classifier_set = GetMainClassifierSet();
        if (!classifier_set)
            return false;
        DumpClassifierSet(*classifier_set, !headers);
    }

    return true;
}

static bool RunList(ArrayRef<const char *> arguments)
{
    static const char *const UsageText =
R"(Usage: moya list [options] list_name ...)";

    Allocator temp_alloc;
    OptionParser opt_parser(arguments);

    HeapArray<const char *> spec_strings;
    {
        const char *opt;
        while ((opt = opt_parser.ConsumeOption())) {
            if (TestOption(opt, "--help")) {
                PrintLn(stdout, UsageText);
                return true;
            } else if (!HandleMainOption(opt_parser, temp_alloc, UsageText)) {
                return false;
            }
        }

        opt_parser.ConsumeNonOptions(&spec_strings);
        if (!spec_strings.len) {
            PrintLn(stderr, "No specifier provided");
            PrintLn(stderr, UsageText);
            return false;
        }
    }

    const ClassifierSet *classifier_set;
    const ClassifierIndex *classifier_index;
    {
        classifier_set = GetMainClassifierSet();
        if (!classifier_set)
            return false;
        classifier_index = classifier_set->FindIndex(main_set_date);
        if (!classifier_index) {
            LogError("No classifier set available at '%1'", main_set_date);
            return false;
        }
    }

    for (const char *spec_str: spec_strings) {
        ListSpecifier spec = ListSpecifier::FromString(spec_str);
        if (!spec.IsValid())
            continue;

        PrintLn("%1:", spec_str);
        switch (spec.table) {
            case ListSpecifier::Table::Diagnoses: {
                for (const DiagnosisInfo &diag: classifier_index->diagnoses) {
                    if (diag.flags & (int)DiagnosisInfo::Flag::SexDifference) {
                        if (spec.Match(diag.mask[0].values)) {
                            PrintLn("  %1 (male)", diag.code);
                        }
                        if (spec.Match(diag.mask[1].values)) {
                            PrintLn("  %1 (female)", diag.code);
                        }
                    } else {
                        if (spec.Match(diag.mask[0].values)) {
                            PrintLn("  %1", diag.code);
                        }
                    }
                }
            } break;

            case ListSpecifier::Table::Procedures: {
                for (const ProcedureInfo &proc: classifier_index->procedures) {
                    if (spec.Match(proc.values)) {
                        PrintLn("  %1", proc.code);
                    }
                }
            } break;
        }
        PrintLn();
    }

    return true;
}

static bool RunPricing(ArrayRef<const char *>)
{
    PrintLn(stderr, "Not implemented");
    return false;
}

static bool RunShow(ArrayRef<const char *> arguments)
{
    static const char *const UsageText =
R"(Usage: moya show [options] name ...)";

    Allocator temp_alloc;
    OptionParser opt_parser(arguments);

    HeapArray<const char *> names;
    {
        const char *opt;
        while ((opt = opt_parser.ConsumeOption())) {
            if (TestOption(opt, "--help")) {
                PrintLn(stdout, UsageText);
                return true;
            } else if (!HandleMainOption(opt_parser, temp_alloc, UsageText)) {
                return false;
            }
        }

        opt_parser.ConsumeNonOptions(&names);
        if (!names.len) {
            PrintLn(stderr, "No element name provided");
            PrintLn(stderr, UsageText);
            return false;
        }
    }

    const ClassifierSet *classifier_set;
    const ClassifierIndex *classifier_index;
    {
        classifier_set = GetMainClassifierSet();
        if (!classifier_set)
            return false;
        classifier_index = classifier_set->FindIndex(main_set_date);
        if (!classifier_index) {
            LogError("No classifier set available at '%1'", main_set_date);
            return false;
        }
    }

    for (const char *name: names) {
        const DiagnosisInfo *diag = classifier_index->FindDiagnosis(DiagnosisCode(name));
        if (diag) {
            DumpDiagnosisTable(*diag, classifier_index->exclusions);
            continue;
        }

        ArrayRef<const ProcedureInfo> proc = classifier_index->FindProcedure(ProcedureCode(name));
        if (proc.len) {
            DumpProcedureTable(proc);
            continue;
        }

        PrintLn(stderr, "Unknown element '%1'", name);
    }

    return true;
}

static bool RunTables(ArrayRef<const char *> arguments)
{
    static const char *const UsageText =
R"(Usage: moya tables [options]

Options:
    -v, --verbose            Show more detailed information)";

    Allocator temp_alloc;
    OptionParser opt_parser(arguments);

    bool verbose = false;
    {
        const char *opt;
        while ((opt = opt_parser.ConsumeOption())) {
            if (TestOption(opt, "--help")) {
                PrintLn(stdout, "%1", UsageText);
                return true;
            } else if (TestOption(opt, "-v", "--verbose")) {
                verbose = true;
            } else if (!HandleMainOption(opt_parser, temp_alloc, UsageText)) {
                return false;
            }
        }
    }

    const ClassifierSet *classifier_set = GetMainClassifierSet();
    if (!classifier_set)
        return false;

    for (const ClassifierIndex &index: classifier_set->indexes) {
        PrintLn("%1 to %2:", index.limit_dates[0], index.limit_dates[1]);
        for (const TableInfo *table: index.tables) {
            if (!table)
                continue;

            PrintLn("  %1: %2.%3",
                    TableTypeNames[(int)table->type], table->version[0], table->version[1]);
            if (verbose) {
                PrintLn("    Validity: %1 to %2",
                        table->limit_dates[0], table->limit_dates[1]);
                PrintLn("    Build: %1", table->build_date);
            }
        }
        PrintLn();
    }

    return true;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        PrintLn(stderr, "%1", MainUsageText);
        return 1;
    }
    if (!strcmp(argv[1], "--help") || !strcmp(argv[1], "help")) {
        if (argc > 2 && argv[2][0] != '-') {
            std::swap(argv[1], argv[2]);
            argv[2] = (char *)"--help";
        } else {
            PrintLn("%1", MainUsageText);
            return 1;
        }
    }

#define HANDLE_COMMAND(Cmd, Func) \
        do { \
            if (!(strcmp(cmd, STRINGIFY(Cmd)))) { \
                return Func(arguments); \
            } \
        } while (false)

    const char *cmd = argv[1];
    ArrayRef<const char *> arguments((const char **)argv + 2, argc - 2);

    HANDLE_COMMAND(classify, RunClassify);
    HANDLE_COMMAND(dump, RunDump);
    HANDLE_COMMAND(list, RunList);
    HANDLE_COMMAND(pricing, RunPricing);
    HANDLE_COMMAND(show, RunShow);
    HANDLE_COMMAND(tables, RunTables);

#undef HANDLE_COMMAND

    PrintLn(stderr, "Unknown command '%1'", cmd);
    PrintLn(stderr, "%1", MainUsageText);
    return 1;
}
