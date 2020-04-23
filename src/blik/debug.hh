// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#pragma once

#include "../core/libcc/libcc.hh"
#include "types.hh"

namespace RG {

struct Program;
struct DebugInfo;

enum class DiagnosticType {
    Error,
    ErrorHint
};

struct SourceInfo {
    const char *filename;
    Size first_idx;
    Size line_idx;
};

struct DebugInfo {
    HeapArray<SourceInfo> sources;
    HeapArray<Size> lines;

    BlockAllocator str_alloc;
};

struct FrameInfo {
    Size pc;
    Size bp;
    const FunctionInfo *func; // Can be NULL

    // Only if DebugInfo is available
    const char *filename;
    int32_t line;
};

template <typename... Args>
void ReportDiagnostic(DiagnosticType type, Span<const char> code, const char *filename,
                      int line, Size offset, const char *fmt, Args... args)
{
    // Find entire code line and compute column from offset
    int column = 0;
    Span<const char> extract = MakeSpan(code.ptr + offset, 0);
    while (extract.ptr > code.ptr && extract.ptr[-1] != '\n') {
        extract.ptr--;
        extract.len++;

        // Ignore UTF-8 trailing bytes to count code points. Not perfect (we want
        // to count graphemes), but close enough for now.
        column += ((extract.ptr[0] & 0xC0) != 0x80);
    }
    while (extract.end() < code.end() && !strchr("\r\n", extract.ptr[extract.len])) {
        extract.len++;
    }

    // We point the user to error location with '^^^', but if the token is a single
    // character (e.g. operator) we want the second caret to be centered on it.
    // There is a small trap: we can't do that if the character before is a tabulation,
    // see below for tab handling.
    bool center = (offset > 0 && code[offset - 1] == ' ' &&
                   (offset + 1 >= code.len || IsAsciiWhite(code[offset + 1])));

    // Because we accept tabulation users, including the crazy ones who may put tabulations
    // after other characters, we can't just repeat ' ' (column - 1) times to align the
    // visual indicator. Instead, we create an alignment string containing spaces (for all
    // characters but tab) and tabulations.
    char align[1024];
    int align_more;
    {
        int align_len = std::min((int)RG_SIZE(align) - 1, column - center);
        for (Size i = 0; i < align_len; i++) {
            align[i] = (extract[i] == '\t') ? '\t' : ' ';
        }
        align[align_len] = 0;

        // Tabulations and very long lines... if you can read this comment: just stop.
        align_more = column - center - align_len;
    }

    // Make it gorgeous!
    switch (type) {
        case DiagnosticType::Error: {
            Print(stderr, "%!R..%1(%2:%3):%!0 %!..+", filename, line, column + 1);
            PrintLn(stderr, fmt, args...);
            PrintLn(stderr, "%1 |%!0  %2", FmtArg(line).Pad(-7), extract);
            PrintLn(stderr, "        |  %1%2%!M..^^^%!0", align, FmtArg(' ').Repeat(align_more));
        } break;

        case DiagnosticType::ErrorHint: {
            Print(stderr, "    %!Y..%1(%2:%3):%!0 %!..+", filename, line, column + 1);
            PrintLn(stderr, fmt, args...);
            PrintLn(stderr, "    %1 |%!0  %2", FmtArg(line).Pad(-7), extract);
            PrintLn(stderr, "            |  %1%2%!D..^^^%!0", align, FmtArg(' ').Repeat(align_more));
        } break;
    }
}

void DecodeFrames(const Program &program, const DebugInfo *debug,
                  Span<const Value> stack, Size pc, Size b, HeapArray<FrameInfo> *out_frames);

}
