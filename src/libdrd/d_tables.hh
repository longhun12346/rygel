// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#pragma once

#include "../common/kutil.hh"
#include "d_codes.hh"

enum class TableType: uint32_t {
    UnknownTable,

    GhmDecisionTree,
    DiagnosisTable,
    ProcedureTable,
    GhmRootTable,
    SeverityTable,

    GhsAccessTable,
    AuthorizationTable,
    SrcPairTable,

    PriceTable
};
static const char *const TableTypeNames[] = {
    "Unknown Table",

    "GHM Decision Tree",
    "Diagnosis Table",
    "Procedure Table",
    "GHM Root Table",
    "Severity Table",

    "GHS Access Table",
    "Authorization Table",
    "SRC Pair Table",

    "Price Table"
};

struct ListMask {
    int16_t offset;
    uint8_t value;
};

struct TableInfo {
    struct Section {
        Size raw_offset;
        Size raw_len;
        Size values_count;
        Size value_len;
    };

    const char *filename;
    Date build_date;
    uint16_t version[2];
    Date limit_dates[2];

    char raw_type[9];
    TableType type;

    LocalArray<Section, 16> sections;
};

struct GhmDecisionNode {
    enum class Type {
        Test,
        Ghm
    };

    Type type;
    union {
        struct {
            uint8_t function; // Switch to dedicated enum
            uint8_t params[2];
            Size children_count;
            Size children_idx;
        } test;

        struct {
            GhmCode ghm;
            int16_t error;
        } ghm;
    } u;
};

struct DiagnosisInfo {
    enum class Flag {
        SexDifference = 1
    };

    DiagnosisCode diag;

    uint16_t flags;
    struct Attributes {
        uint8_t raw[37];

        uint8_t cmd;
        uint8_t jump;
        uint8_t severity;
    } attributes[2];
    uint16_t warnings;

    uint16_t exclusion_set_idx;
    ListMask cma_exclusion_mask;

    const Attributes &Attributes(Sex sex) const
    {
        StaticAssert((int)Sex::Male == 1);
        return attributes[(int)sex - 1];
    }

    HASH_TABLE_HANDLER(DiagnosisInfo, diag);
};

struct ExclusionInfo {
    uint8_t raw[256];
};

struct ProcedureInfo {
    ProcedureCode proc;
    int8_t phase;

    Date limit_dates[2];
    uint8_t bytes[55];

    HASH_TABLE_HANDLER(ProcedureInfo, proc);
};

template <Size N>
struct ValueRangeCell {
    struct {
        int min;
        int max;
    } limits[N];
    int value;

    bool Test(Size idx, int value) const
    {
        DebugAssert(idx < N);
        return (value >= limits[idx].min && value < limits[idx].max);
    }
};

struct GhmRootInfo {
    GhmRootCode ghm_root;

    int8_t confirm_duration_treshold;

    bool allow_ambulatory;
    int8_t short_duration_treshold;

    int8_t young_severity_limit;
    int8_t young_age_treshold;
    int8_t old_severity_limit;
    int8_t old_age_treshold;

    int8_t childbirth_severity_list;

    ListMask cma_exclusion_mask;

    HASH_TABLE_HANDLER(GhmRootInfo, ghm_root);
};

struct GhsAccessInfo {
    GhmCode ghm;
    GhsCode ghs[2]; // 0 for public, 1 for private

    int8_t bed_authorization;
    int8_t unit_authorization;
    int8_t minimal_duration;

    int8_t minimal_age;

    ListMask main_diagnosis_mask;
    ListMask diagnosis_mask;
    LocalArray<ListMask, 4> procedure_masks;

    HASH_TABLE_HANDLER_N(GhmHandler, GhsAccessInfo, ghm);
    HASH_TABLE_HANDLER_N(GhmRootHandler, GhsAccessInfo, ghm.Root());
};

struct GhsPriceInfo {
    enum class Flag {
        ExbOnce = 1
    };

    GhsCode ghs;

    struct {
        int32_t price_cents;
        int16_t exh_treshold;
        int16_t exb_treshold;
        int32_t exh_cents;
        int32_t exb_cents;
        uint16_t flags;
    } sectors[2]; // 0 for public, 1 for private

    HASH_TABLE_HANDLER(GhsPriceInfo, ghs);
};

enum class AuthorizationScope: int8_t {
    Facility,
    Unit,
    Bed
};
static const char *const AuthorizationScopeNames[] = {
    "Facility",
    "Unit",
    "Bed"
};
struct AuthorizationInfo {
    union {
        int16_t value;
        struct {
            AuthorizationScope scope;
            int8_t code;
        } st;
    } type;
    int8_t function;

    HASH_TABLE_HANDLER(AuthorizationInfo, type.value);
};

struct SrcPair {
    DiagnosisCode diag;
    ProcedureCode proc;
};

struct PriceTable {
    Date date;
    Date build_date;
    HeapArray<GhsPriceInfo> ghs_prices;
};

Date ConvertDate1980(uint16_t days);

bool ParseTableHeaders(Span<const uint8_t> file_data, const char *filename,
                       Allocator *str_alloc, HeapArray<TableInfo> *out_tables);

bool ParseGhmDecisionTree(const uint8_t *file_data, const TableInfo &table,
                          HeapArray<GhmDecisionNode> *out_nodes);
bool ParseDiagnosisTable(const uint8_t *file_data, const TableInfo &table,
                         HeapArray<DiagnosisInfo> *out_diags);
bool ParseExclusionTable(const uint8_t *file_data, const TableInfo &table,
                         HeapArray<ExclusionInfo> *out_exclusions);
bool ParseProcedureTable(const uint8_t *file_data, const TableInfo &table,
                         HeapArray<ProcedureInfo> *out_procs);
bool ParseGhmRootTable(const uint8_t *file_data, const TableInfo &table,
                       HeapArray<GhmRootInfo> *out_ghm_roots);
bool ParseSeverityTable(const uint8_t *file_data, const TableInfo &table, int section_idx,
                        HeapArray<ValueRangeCell<2>> *out_cells);

bool ParseGhsAccessTable(const uint8_t *file_data, const TableInfo &table,
                         HeapArray<GhsAccessInfo> *out_nodes);
bool ParseAuthorizationTable(const uint8_t *file_data, const TableInfo &table,
                             HeapArray<AuthorizationInfo> *out_auths);
bool ParseSrcPairTable(const uint8_t *file_data, const TableInfo &table, int section_idx,
                       HeapArray<SrcPair> *out_pairs);

bool ParsePricesJson(StreamReader &st, HeapArray<PriceTable> *out_tables);

struct TableIndex {
    Date limit_dates[2];

    const TableInfo *tables[ARRAY_SIZE(TableTypeNames)];
    uint32_t changed_tables;

    Span<GhmDecisionNode> ghm_nodes;
    Span<DiagnosisInfo> diagnoses;
    Span<ExclusionInfo> exclusions;
    Span<ProcedureInfo> procedures;
    Span<GhmRootInfo> ghm_roots;
    Span<ValueRangeCell<2>> gnn_cells;
    Span<ValueRangeCell<2>> cma_cells[3];

    Span<GhsAccessInfo> ghs;
    Span<AuthorizationInfo> authorizations;
    Span<SrcPair> src_pairs[2];

    Span<GhsPriceInfo> ghs_prices;

    HashTable<DiagnosisCode, const DiagnosisInfo *> *diagnoses_map;
    HashTable<ProcedureCode, const ProcedureInfo *> *procedures_map;
    HashTable<GhmRootCode, const GhmRootInfo *> *ghm_roots_map;

    HashTable<GhmCode, const GhsAccessInfo *, GhsAccessInfo::GhmHandler> *ghm_to_ghs_map;
    HashTable<GhmRootCode, const GhsAccessInfo *, GhsAccessInfo::GhmRootHandler> *ghm_root_to_ghs_map;
    HashTable<int16_t, const AuthorizationInfo *> *authorizations_map;

    HashTable<GhsCode, const GhsPriceInfo *> *ghs_prices_map;

    const DiagnosisInfo *FindDiagnosis(DiagnosisCode diag) const;
    Span<const ProcedureInfo> FindProcedure(ProcedureCode proc) const;
    const ProcedureInfo *FindProcedure(ProcedureCode proc, int8_t phase, Date date) const;
    const GhmRootInfo *FindGhmRoot(GhmRootCode ghm_root) const;

    Span<const GhsAccessInfo> FindCompatibleGhs(GhmRootCode ghm_root) const;
    Span<const GhsAccessInfo> FindCompatibleGhs(GhmCode ghm) const;
    const AuthorizationInfo *FindAuthorization(AuthorizationScope scope, int8_t type) const;

    const GhsPriceInfo *FindGhsPrice(GhsCode ghs) const;
};

class TableSet {
public:
    HeapArray<TableInfo> tables;
    HeapArray<TableIndex> indexes;

    struct {
        HeapArray<GhmDecisionNode> ghm_nodes;
        HeapArray<DiagnosisInfo> diagnoses;
        HeapArray<ExclusionInfo> exclusions;
        HeapArray<ProcedureInfo> procedures;
        HeapArray<GhmRootInfo> ghm_roots;
        HeapArray<ValueRangeCell<2>> gnn_cells;
        HeapArray<ValueRangeCell<2>> cma_cells[3];

        HeapArray<GhsAccessInfo> ghs;
        HeapArray<GhsPriceInfo> ghs_prices;
        HeapArray<AuthorizationInfo> authorizations;
        HeapArray<SrcPair> src_pairs[2];
    } store;

    struct {
        HeapArray<HashTable<DiagnosisCode, const DiagnosisInfo *>> diagnoses;
        HeapArray<HashTable<ProcedureCode, const ProcedureInfo *>> procedures;
        HeapArray<HashTable<GhmRootCode, const GhmRootInfo *>> ghm_roots;

        HeapArray<HashTable<GhmCode, const GhsAccessInfo *, GhsAccessInfo::GhmHandler>> ghm_to_ghs;
        HeapArray<HashTable<GhmRootCode, const GhsAccessInfo *, GhsAccessInfo::GhmRootHandler>> ghm_root_to_ghs;
        HeapArray<HashTable<int16_t, const AuthorizationInfo *>> authorizations;

        HeapArray<HashTable<GhsCode, const GhsPriceInfo *>> ghs_prices;
    } maps;

    LinkedAllocator str_alloc;

    const TableIndex *FindIndex(Date date = {}) const;
    TableIndex *FindIndex(Date date = {})
        { return (TableIndex *)((const TableSet *)this)->FindIndex(date); }
};

class TableSetBuilder {
    struct TableLoadInfo {
        Size table_idx;
        union {
            Size price_table_idx;
            Span<uint8_t> raw_data;
        } u;
        bool loaded;
    };

    LinkedAllocator file_alloc;

    HeapArray<TableLoadInfo> table_loads;
    HeapArray<PriceTable> price_tables;

    TableSet set;

public:
    bool LoadAtihTab(StreamReader &st);
    bool LoadPriceJson(StreamReader &st);
    bool LoadFiles(Span<const char *const> tab_filenames, Span<const char *const> price_filenames);

    bool Finish(TableSet *out_set);

private:
    bool CommitIndex(Date start_date, Date end_date, TableLoadInfo *current_tables[]);
};
