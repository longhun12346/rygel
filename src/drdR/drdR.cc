// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "../libdrd/libdrd.hh"
#include "../common/Rcc.hh"

struct ClassifierInstance {
    TableSet table_set;
    AuthorizationSet authorization_set;
};

// [[Rcpp::export(name = 'drd.options')]]
SEXP R_Options(SEXP debug = R_NilValue)
{
    if (!Rf_isNull(debug)) {
        enable_debug = Rcpp::as<bool>(debug);
    }

    return Rcpp::List::create(
        Rcpp::Named("debug") = enable_debug
    );
}

// [[Rcpp::export(name = 'drd')]]
SEXP R_Drd(Rcpp::CharacterVector data_dirs = Rcpp::CharacterVector::create(),
           Rcpp::CharacterVector table_dirs = Rcpp::CharacterVector::create(),
           Rcpp::CharacterVector price_filenames = Rcpp::CharacterVector::create(),
           Rcpp::Nullable<Rcpp::String> authorization_filename = R_NilValue)
{
    RCC_SETUP_LOG_HANDLER();

    ClassifierInstance *classifier = new ClassifierInstance;
    DEFER_N(classifier_guard) { delete classifier; };

    HeapArray<const char *> data_dirs2;
    HeapArray<const char *> table_dirs2;
    HeapArray<const char *> table_filenames2;
    const char *authorization_filename2 = nullptr;
    for (const char *str: data_dirs) {
        data_dirs2.Append(str);
    }
    for (const char *str: table_dirs) {
        table_dirs2.Append(str);
    }
    for (const char *str: price_filenames) {
        table_filenames2.Append(str);
    }
    if (authorization_filename.isNotNull()) {
        authorization_filename2 = authorization_filename.as().get_cstring();
    }

    if (!InitTableSet(data_dirs2, table_dirs2, table_filenames2, &classifier->table_set) ||
            !classifier->table_set.indexes.len)
        Rcc_StopWithLastError();
    if (!InitAuthorizationSet(data_dirs2, authorization_filename2,
                              &classifier->authorization_set))
        Rcc_StopWithLastError();

    classifier_guard.disable();
    return Rcpp::XPtr<ClassifierInstance>(classifier, true);
}

struct StaysProxy {
    Size nrow;

    Rcc_Vector<const int> id;

    Rcc_Vector<int> bill_id;
    Rcc_Vector<int> stay_id;
    Rcc_Vector<Date> birthdate;
    Rcc_Vector<int> sex;
    Rcc_Vector<Date> entry_date;
    Rcc_Vector<int> entry_mode;
    Rcc_Vector<const char *> entry_origin;
    Rcc_Vector<Date> exit_date;
    Rcc_Vector<int> exit_mode;
    Rcc_Vector<int> exit_destination;
    Rcc_Vector<int> unit;
    Rcc_Vector<int> bed_authorization;
    Rcc_Vector<int> session_count;
    Rcc_Vector<int> igs2;
    Rcc_Vector<int> gestational_age;
    Rcc_Vector<int> newborn_weight;
    Rcc_Vector<Date> last_menstrual_period;

    Rcc_Vector<const char *> main_diagnosis;
    Rcc_Vector<const char *> linked_diagnosis;
} stays;

struct DiagnosesProxy {
    Size nrow;

    Rcc_Vector<int> id;

    Rcc_Vector<const char *> diag;
    Rcc_Vector<const char *> type;
} diagnoses;

struct ProceduresProxy {
    Size nrow;

    Rcc_Vector<int> id;

    Rcc_Vector<const char *> proc;
    Rcc_Vector<int> phase;
    Rcc_Vector<int> activity;
    Rcc_Vector<int> count;
    Rcc_Vector<Date> date;
};

static bool RunClassifier(const ClassifierInstance &classifier,
                          const StaysProxy &stays, Size stays_offset, Size stays_end,
                          const DiagnosesProxy &diagnoses, Size diagnoses_offset, Size diagnoses_end,
                          const ProceduresProxy &procedures, Size procedures_offset, Size procedures_end,
                          StaySet *out_stay_set, HeapArray<ClassifyResult> *out_results)
{
    out_stay_set->stays.Reserve(stays_end - stays_offset);
    out_stay_set->store.diagnoses.Reserve((stays_end - stays_offset) * 2 + diagnoses_end - diagnoses_offset);
    out_stay_set->store.procedures.Reserve(procedures_end - procedures_offset);

    int prev_id = INT_MIN;
    Size j = diagnoses_offset;
    Size k = procedures_offset;
    for (Size i = stays_offset; i < stays_end; i++) {
        Stay stay = {};

        if (UNLIKELY(stays.id[i] < prev_id ||
                     (j < diagnoses_end && diagnoses.id[j] < prev_id) ||
                     (k < procedures_end && procedures.id[k] < prev_id)))
            return false;
        prev_id = stays.id[i];

        stay.bill_id = Rcc_GetOptional(stays.bill_id, i, 0);
        stay.stay_id = Rcc_GetOptional(stays.stay_id, i, 0);
        stay.birthdate = stays.birthdate[i];
        if (UNLIKELY(!stay.birthdate.value && !stays.birthdate.IsNA(stay.birthdate))) {
            stay.error_mask |= (int)Stay::Error::MalformedBirthdate;
        }
        switch (stays.sex[i]) {
            case 1: { stay.sex = Sex::Male; } break;
            case 2: { stay.sex = Sex::Female; } break;

            default: {
                if (stays.sex[i] != NA_INTEGER) {
                    LogError("Unexpected sex %1 on row %2", stays.sex[i], i + 1);
                    stay.error_mask |= (int)Stay::Error::MalformedSex;
                }
            } break;
        }
        stay.entry.date = stays.entry_date[i];
        if (UNLIKELY(!stay.entry.date.value && !stays.entry_date.IsNA(stay.entry.date))) {
            stay.error_mask |= (int)Stay::Error::MalformedEntryDate;
        }
        stay.entry.mode = (char)('0' + stays.entry_mode[i]);
        {
            const char *origin_str = stays.entry_origin[i];
            if (origin_str[0] && !origin_str[1]) {
                stay.entry.origin = UpperAscii(origin_str[0]);
            } else if (origin_str != CHAR(NA_STRING)) {
                stay.error_mask |= (uint32_t)Stay::Error::MalformedEntryOrigin;
            }
        }
        stay.exit.date = stays.exit_date[i];
        if (UNLIKELY(!stay.exit.date.value && !stays.exit_date.IsNA(stay.exit.date))) {
            stay.error_mask |= (int)Stay::Error::MalformedExitDate;
        }
        stay.exit.mode = (char)('0' + stays.exit_mode[i]);
        stay.exit.destination = (char)('0' + Rcc_GetOptional(stays.exit_destination, i, -'0'));

        stay.unit.number = (int16_t)Rcc_GetOptional(stays.unit, i, 0);
        stay.bed_authorization = (int8_t)Rcc_GetOptional(stays.bed_authorization, i, 0);
        stay.session_count = (int16_t)Rcc_GetOptional(stays.session_count, i, 0);
        stay.igs2 = (int16_t)Rcc_GetOptional(stays.igs2, i, 0);
        stay.gestational_age = (int16_t)stays.gestational_age[i];
        stay.newborn_weight = (int16_t)stays.newborn_weight[i];
        stay.last_menstrual_period = stays.last_menstrual_period[i];

        stay.diagnoses.ptr = out_stay_set->store.diagnoses.end();
        if (diagnoses.type.Len()) {
            for (; j < diagnoses_end && diagnoses.id[j] == stays.id[i]; j++) {
                if (UNLIKELY(diagnoses.diag[j] == CHAR(NA_STRING)))
                    continue;

                DiagnosisCode diag = DiagnosisCode::FromString(diagnoses.diag[j], false);
                const char *type_str = diagnoses.type[j];

                if (LIKELY(type_str[0] && !type_str[1])) {
                    switch (type_str[0]) {
                        case 'p':
                        case 'P': {
                            stay.main_diagnosis = diag;
                            if (UNLIKELY(!stay.main_diagnosis.IsValid())) {
                                stay.error_mask |= (int)Stay::Error::MalformedMainDiagnosis;
                            }
                        } break;
                        case 'r':
                        case 'R': {
                            stay.linked_diagnosis = diag;
                            if (UNLIKELY(!stay.linked_diagnosis.IsValid())) {
                                stay.error_mask |= (int)Stay::Error::MalformedLinkedDiagnosis;
                            }
                        } break;
                        case 's':
                        case 'S': {
                            if (LIKELY(diag.IsValid())) {
                                out_stay_set->store.diagnoses.Append(diag);
                            } else {
                                stay.error_mask |= (int)Stay::Error::MalformedAssociatedDiagnosis;
                            }
                        } break;
                        case 'd':
                        case 'D': { /* Ignore documentary diagnoses */ } break;

                        default: {
                            LogError("Unexpected diagnosis type '%1' on row %2", type_str, i + 1);
                        } break;
                    }
                } else {
                    LogError("Malformed diagnosis type '%1' on row %2", type_str, i + 1);
                }
            }
        } else {
            if (LIKELY(stays.main_diagnosis[i] != CHAR(NA_STRING))) {
                stay.main_diagnosis = DiagnosisCode::FromString(stays.main_diagnosis[i], false);
                if (UNLIKELY(!stay.main_diagnosis.IsValid())) {
                    stay.error_mask |= (int)Stay::Error::MalformedMainDiagnosis;
                }
            }
            if (stays.linked_diagnosis[i] != CHAR(NA_STRING)) {
                stay.linked_diagnosis = DiagnosisCode::FromString(stays.linked_diagnosis[i], false);
                if (UNLIKELY(!stay.linked_diagnosis.IsValid())) {
                    stay.error_mask |= (int)Stay::Error::MalformedLinkedDiagnosis;
                }
            }

            for (; j < diagnoses_end && diagnoses.id[j] == stays.id[i]; j++) {
                if (UNLIKELY(diagnoses.diag[j] == CHAR(NA_STRING)))
                    continue;

                DiagnosisCode diag = DiagnosisCode::FromString(diagnoses.diag[j], false);
                if (UNLIKELY(!diag.IsValid())) {
                    stay.error_mask |= (int)Stay::Error::MalformedAssociatedDiagnosis;
                }

                out_stay_set->store.diagnoses.Append(diag);
            }
        }
        if (stay.main_diagnosis.IsValid()) {
            out_stay_set->store.diagnoses.Append(stay.main_diagnosis);
        }
        if (stay.linked_diagnosis.IsValid()) {
            out_stay_set->store.diagnoses.Append(stay.linked_diagnosis);
        }
        stay.diagnoses.len = out_stay_set->store.diagnoses.end() - stay.diagnoses.ptr;

        stay.procedures.ptr = out_stay_set->store.procedures.end();
        for (; k < procedures_end && procedures.id[k] == stays.id[i]; k++) {
            ProcedureRealisation proc = {};

            proc.proc = ProcedureCode::FromString(procedures.proc[k]);
            proc.phase = (int8_t)Rcc_GetOptional(procedures.phase, k, 0);
            {
                int activities_dec = procedures.activity[k];
                while (activities_dec) {
                    int activity = activities_dec % 10;
                    activities_dec /= 10;
                    proc.activities |= (uint8_t)(1 << activity);
                }
            }
            proc.count = (int16_t)Rcc_GetOptional(procedures.count, k, 1);
            proc.date = procedures.date[k];

            out_stay_set->store.procedures.Append(proc);
        }
        stay.procedures.len = out_stay_set->store.procedures.end() - stay.procedures.ptr;

        out_stay_set->stays.Append(stay);
    }

    // We're already running in parallel, using ClassifyParallel would slow us down,
    // because it has some overhead caused by multi-stays.
    Classify(classifier.table_set, classifier.authorization_set,
             out_stay_set->stays, ClusterMode::BillId, out_results);

    return true;
}

// [[Rcpp::export(name = '.classify')]]
SEXP R_Classify(SEXP classifier_xp, Rcpp::DataFrame stays_df,
                Rcpp::DataFrame diagnoses_df, Rcpp::DataFrame procedures_df, bool details = true)
{
    RCC_SETUP_LOG_HANDLER();

    static const int task_size = 2048;

    const ClassifierInstance *classifier = Rcpp::XPtr<ClassifierInstance>(classifier_xp).get();

#define LOAD_OPTIONAL_COLUMN(Var, Name) \
        do { \
            if ((Var ## _df).containsElementNamed(STRINGIFY(Name))) { \
                (Var).Name = (Var ##_df)[STRINGIFY(Name)]; \
            } \
        } while (false)

    LogDebug("Start");

    StaysProxy stays;
    stays.nrow = stays_df.nrow();
    stays.id = stays_df["id"];
    LOAD_OPTIONAL_COLUMN(stays, bill_id);
    LOAD_OPTIONAL_COLUMN(stays, stay_id);
    stays.birthdate = stays_df["birthdate"];
    stays.sex = stays_df["sex"];
    stays.entry_date = stays_df["entry_date"];
    stays.entry_mode = stays_df["entry_mode"];
    LOAD_OPTIONAL_COLUMN(stays, entry_origin);
    stays.exit_date = stays_df["exit_date"];
    stays.exit_mode = stays_df["exit_mode"];
    LOAD_OPTIONAL_COLUMN(stays, exit_destination);
    LOAD_OPTIONAL_COLUMN(stays, unit);
    LOAD_OPTIONAL_COLUMN(stays, bed_authorization);
    LOAD_OPTIONAL_COLUMN(stays, session_count);
    LOAD_OPTIONAL_COLUMN(stays, igs2);
    LOAD_OPTIONAL_COLUMN(stays, gestational_age);
    LOAD_OPTIONAL_COLUMN(stays, newborn_weight);
    LOAD_OPTIONAL_COLUMN(stays, last_menstrual_period);

    DiagnosesProxy diagnoses;
    diagnoses.nrow = diagnoses_df.nrow();
    diagnoses.id = diagnoses_df["id"];
    diagnoses.diag = diagnoses_df["diag"];
    if (diagnoses_df.containsElementNamed("type")) {
        diagnoses.type = diagnoses_df["type"];

        if (stays_df.containsElementNamed("main_diagnosis") ||
                stays_df.containsElementNamed("linked_diagnosis")) {
            LogError("Columns 'main_diagnosis' and 'linked_diagnosis' are ignored when the "
                     "diagnoses table has a type column");
        }
    } else {
        stays.main_diagnosis = stays_df["main_diagnosis"];
        stays.linked_diagnosis = stays_df["linked_diagnosis"];
    }

    ProceduresProxy procedures;
    procedures.nrow = procedures_df.nrow();
    procedures.id = procedures_df["id"];
    procedures.proc = procedures_df["code"];
    LOAD_OPTIONAL_COLUMN(procedures, phase);
    procedures.activity = procedures_df["activity"];
    LOAD_OPTIONAL_COLUMN(procedures, count);
    procedures.date = procedures_df["date"];

#undef LOAD_OPTIONAL_COLUMN

    LogDebug("Classify");

    struct ClassifySet {
        StaySet stay_set;

        HeapArray<ClassifyResult> results;
        ClassifySummary summary;
    };
    HeapArray<ClassifySet> classify_sets;
    classify_sets.Reserve((stays.nrow - 1) / task_size + 1);

    Async async;
    {
        Size stays_offset = 0;
        Size diagnoses_offset = 0;
        Size procedures_offset = 0;
        while (stays_offset < stays.nrow) {
            Size stays_end = std::min(stays.nrow, stays_offset + task_size);
            while (stays_end < stays.nrow &&
                   stays.bill_id[stays_end] == stays.bill_id[stays_end - 1]) {
                stays_end++;
            }

            Size diagnoses_end = diagnoses_offset;
            while (diagnoses_end < diagnoses.nrow &&
                   diagnoses.id[diagnoses_end] <= stays.id[stays_end - 1]) {
                diagnoses_end++;
            }
            Size procedures_end = procedures_offset;
            while (procedures_end < procedures.nrow &&
                   procedures.id[procedures_end] <= stays.id[stays_end - 1]) {
                procedures_end++;
            }

            ClassifySet *classify_set = classify_sets.AppendDefault();

            async.AddTask([=, &stays, &diagnoses, &procedures]() mutable {
                if (!RunClassifier(*classifier, stays, stays_offset, stays_end,
                                   diagnoses, diagnoses_offset, diagnoses_end,
                                   procedures, procedures_offset, procedures_end,
                                   &classify_set->stay_set, &classify_set->results))
                    return false;
                Summarize(classify_set->results, &classify_set->summary);
                return true;
            });

            stays_offset = stays_end;
            diagnoses_offset = diagnoses_end;
            procedures_offset = procedures_end;
        }
    }
    if (!async.Sync()) {
        Rcpp::stop("The 'id' column must be ordered in all data.frames");
    }

    LogDebug("Export");

    ClassifySummary summary = {};
    Rcc_AutoSexp summary_df;
    {
        for (const ClassifySet &classify_set: classify_sets) {
            summary += classify_set.summary;
        }

        Rcc_ListBuilder df_builder;
        df_builder.Set("ghs_cents", (double)summary.ghs_total_cents);
        df_builder.Set("rea_cents", (double)summary.supplement_cents.st.rea);
        df_builder.Set("reasi_cents", (double)summary.supplement_cents.st.reasi);
        df_builder.Set("si_cents", (double)summary.supplement_cents.st.si);
        df_builder.Set("src_cents", (double)summary.supplement_cents.st.src);
        df_builder.Set("nn1_cents", (double)summary.supplement_cents.st.nn1);
        df_builder.Set("nn2_cents", (double)summary.supplement_cents.st.nn2);
        df_builder.Set("nn3_cents", (double)summary.supplement_cents.st.nn3);
        df_builder.Set("rep_cents", (double)summary.supplement_cents.st.rep);
        df_builder.Set("price_cents", (double)summary.total_cents);
        df_builder.Set("rea_days", summary.supplement_days.st.rea);
        df_builder.Set("reasi_days", summary.supplement_days.st.reasi);
        df_builder.Set("si_days", summary.supplement_days.st.si);
        df_builder.Set("src_days", summary.supplement_days.st.src);
        df_builder.Set("nn1_days", summary.supplement_days.st.nn1);
        df_builder.Set("nn2_days", summary.supplement_days.st.nn2);
        df_builder.Set("nn3_days", summary.supplement_days.st.nn3);
        df_builder.Set("rep_days", summary.supplement_days.st.rep);
        summary_df = df_builder.BuildDataFrame();
    }

    Rcc_AutoSexp results_df;
    if (details) {
        char buf[32];

        Rcc_DataFrameBuilder df_builder(summary.results_count);
        Rcc_Vector<int> bill_id = df_builder.Add<int>("bill_id");
        Rcc_Vector<Date> exit_date = df_builder.Add<Date>("exit_date");
        Rcc_Vector<const char *> ghm = df_builder.Add<const char *>("ghm");
        Rcc_Vector<int> main_error = df_builder.Add<int>("main_error");
        Rcc_Vector<int> ghs = df_builder.Add<int>("ghs");
        Rcc_Vector<double> ghs_cents = df_builder.Add<double>("ghs_cents");
        Rcc_Vector<double> rea_cents = df_builder.Add<double>("rea_cents");
        Rcc_Vector<double> reasi_cents = df_builder.Add<double>("reasi_cents");
        Rcc_Vector<double> si_cents = df_builder.Add<double>("si_cents");
        Rcc_Vector<double> src_cents = df_builder.Add<double>("src_cents");
        Rcc_Vector<double> nn1_cents = df_builder.Add<double>("nn1_cents");
        Rcc_Vector<double> nn2_cents = df_builder.Add<double>("nn2_cents");
        Rcc_Vector<double> nn3_cents = df_builder.Add<double>("nn3_cents");
        Rcc_Vector<double> rep_cents = df_builder.Add<double>("rep_cents");
        Rcc_Vector<double> price_cents = df_builder.Add<double>("price_cents");
        Rcc_Vector<int> rea_days = df_builder.Add<int>("rea_days");
        Rcc_Vector<int> reasi_days = df_builder.Add<int>("reasi_days");
        Rcc_Vector<int> si_days = df_builder.Add<int>("si_days");
        Rcc_Vector<int> src_days = df_builder.Add<int>("src_days");
        Rcc_Vector<int> nn1_days = df_builder.Add<int>("nn1_days");
        Rcc_Vector<int> nn2_days = df_builder.Add<int>("nn2_days");
        Rcc_Vector<int> nn3_days = df_builder.Add<int>("nn3_days");
        Rcc_Vector<int> rep_days = df_builder.Add<int>("rep_days");

        Size i = 0;
        for (const ClassifySet &classify_set: classify_sets) {
            for (const ClassifyResult &result: classify_set.results) {
                bill_id[i] = result.stays[0].bill_id;
                exit_date.Set(i, result.stays[result.stays.len - 1].exit.date);
                ghm.Set(i, Fmt(buf, "%1", result.ghm));
                main_error[i] = result.main_error;
                ghs[i] = result.ghs.number;
                ghs_cents[i] = (double)result.ghs_price_cents;
                rea_cents[i] = result.supplement_cents.st.rea;
                reasi_cents[i] = result.supplement_cents.st.reasi;
                si_cents[i] = result.supplement_cents.st.si;
                src_cents[i] = result.supplement_cents.st.src;
                nn1_cents[i] = result.supplement_cents.st.nn1;
                nn2_cents[i] = result.supplement_cents.st.nn2;
                nn3_cents[i] = result.supplement_cents.st.nn3;
                rep_cents[i] = result.supplement_cents.st.rep;
                price_cents[i] = result.price_cents;
                rea_days[i] = result.supplement_days.st.rea;
                reasi_days[i] = result.supplement_days.st.reasi;
                si_days[i] = result.supplement_days.st.si;
                src_days[i] = result.supplement_days.st.src;
                nn1_days[i] = result.supplement_days.st.nn1;
                nn2_days[i] = result.supplement_days.st.nn2;
                nn3_days[i] = result.supplement_days.st.nn3;
                rep_days[i] = result.supplement_days.st.rep;

                i++;
            }
        }

        results_df = df_builder.Build();
    } else {
        results_df = R_NilValue;
    }

    Rcc_AutoSexp ret_list;
    {
        Rcc_ListBuilder ret_builder;
        ret_builder.Add("summary", summary_df);
        ret_builder.Add("results", results_df);
        ret_list = ret_builder.BuildList();
    }

    return ret_list;
}

// [[Rcpp::export(name = 'diagnoses')]]
SEXP R_Diagnoses(SEXP classifier_set_xp, SEXP date_xp)
{
    RCC_SETUP_LOG_HANDLER();

    const ClassifierInstance *classifier = Rcpp::XPtr<ClassifierInstance>(classifier_set_xp).get();
    Date date = Rcc_Vector<Date>(date_xp).Value();
    if (!date.value)
        Rcc_StopWithLastError();

    const TableIndex *index = classifier->table_set.FindIndex(date);
    if (!index) {
        LogError("No table index available on '%1'", date);
        Rcc_StopWithLastError();
    }

    Rcc_AutoSexp diagnoses_df;
    {
        Rcc_DataFrameBuilder df_builder(index->diagnoses.len);
        Rcc_Vector<const char *> diag = df_builder.Add<const char *>("diag");
        Rcc_Vector<int> cmd_m = df_builder.Add<int>("cmd_m");
        Rcc_Vector<int> cmd_f = df_builder.Add<int>("cmd_f");

        for (Size i = 0; i < index->diagnoses.len; i++) {
            const DiagnosisInfo &info = index->diagnoses[i];
            char buf[32];

            diag.Set(i, Fmt(buf, "%1", info.diag));
            cmd_m[i] = info.Attributes(Sex::Male).cmd;
            cmd_f[i] = info.Attributes(Sex::Female).cmd;
        }

        diagnoses_df = df_builder.Build();
    }

    return diagnoses_df;
}

// [[Rcpp::export(name = 'procedures')]]
SEXP R_Procedures(SEXP classifier_set_xp, SEXP date_xp)
{
    RCC_SETUP_LOG_HANDLER();

    const ClassifierInstance *classifier = Rcpp::XPtr<ClassifierInstance>(classifier_set_xp).get();
    Date date = Rcc_Vector<Date>(date_xp).Value();
    if (!date.value)
        Rcc_StopWithLastError();

    const TableIndex *index = classifier->table_set.FindIndex(date);
    if (!index) {
        LogError("No table index available on '%1'", date);
        Rcc_StopWithLastError();
    }

    Rcc_AutoSexp procedures_df;
    {
        Rcc_DataFrameBuilder df_builder(index->procedures.len);
        Rcc_Vector<const char *> proc = df_builder.Add<const char *>("proc");
        Rcc_Vector<int> phase = df_builder.Add<int>("phase");
        Rcc_Vector<int> activities = df_builder.Add<int>("activities");
        Rcc_Vector<Date> start_date = df_builder.Add<Date>("start_date");
        Rcc_Vector<Date> end_date = df_builder.Add<Date>("end_date");

        for (Size i = 0; i < index->procedures.len; i++) {
            const ProcedureInfo &info = index->procedures[i];
            char buf[32];

            proc.Set(i, Fmt(buf, "%1", info.proc));
            phase[i] = info.phase;
            {
                int activities_dec = 0;
                for (int activities_bin = info.activities, i = 0; activities_bin; i++) {
                    if (activities_bin & 0x1) {
                        activities_dec = (activities_dec * 10) + i;
                    }
                    activities_bin >>= 1;
                }
                activities[i] = activities_dec;
            }
            start_date.Set(i, info.limit_dates[0]);
            end_date.Set(i, info.limit_dates[1]);
        }

        procedures_df = df_builder.Build();
    }

    return procedures_df;
}
