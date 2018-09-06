// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The implementation for the FormattingTreeVisitor that pretty-prints FIDL code.

#include <map>
#include <regex>
#include <set>
#include <string>

#include "fidl/formatter.h"

namespace fidl {
namespace raw {

// Rules:
//   No more than one blank line in a row.
//   Keep blank lines before and after comments.
//   Will add newlines before top level declarations later.
void FormattingTreeVisitor::Segment::RemoveExtraBlankLines(bool respects_trailing_blankline) {
    std::set<int> blank_line_indices;
    std::set<int> comment_line_indices;
    std::map<int, int> line_offsets;

    // First, find all of the blank lines and comment lines.
    int line_num = 0;
    line_offsets[line_num++] = 0;
    for (int i = 0; i < output_.size(); i++) {
        if (output_[i] == '\n' && i + 1 != output_.size()) {
            line_offsets[line_num] = i + 1;
            bool is_blank = true;
            bool is_comment = false;
            for (int j = i + 1;
                 j < output_.size() && output_[j] != '\n'; j++) {
                if (!IsNonNewlineWS(output_[j])) {
                    is_blank = false;
                }
                if (IsStartOfComment(output_, j)) {
                    is_comment = true;
                }
            }
            if (is_blank) {
                blank_line_indices.insert(line_num);
            }
            if (is_comment) {
                comment_line_indices.insert(line_num);
            }
            line_num++;
        }
    }

    int last_line_num = line_num - 1;

    // Next, get rid of any blank line that isn't next to a comment or,
    // if we are respecting trailing blank lines, right before the end.
    // Make an exception if the previous line is blank - i.e., coalesce
    // multiple blank lines.  Work backwards so we don't screw up line
    // numbers.
    for (auto i = blank_line_indices.rbegin();
         i != blank_line_indices.rend();
         ++i) {
        int line_num = *i;
        bool is_next_to_comment =
            comment_line_indices.count(line_num - 1) != 0 ||
            comment_line_indices.count(line_num + 1) != 0;
        bool need_to_preserve_because_last =
            respects_trailing_blankline &&
            line_num == last_line_num - 1;
        bool need_to_coalesce =
            blank_line_indices.count(line_num + 1) != 0;
        if ((!is_next_to_comment && !need_to_preserve_because_last) ||
            need_to_coalesce) {
            int offset = line_offsets[line_num];
            size_t next_newline = output_.find_first_of("\n", offset);
            if (next_newline == std::string::npos) {
                next_newline = output_.size();
            }
            output_.erase(offset, next_newline - offset + 1);
        }
    }
}

// Assumptions: Leading WS has been stripped.
// Rules:
//  - newlines after ';', '{' (unless before a comment)
//  - newlines before top-level decls (unless after a comment).
void FormattingTreeVisitor::Segment::InsertRequiredNewlines(bool is_top_level) {
    // Insert lines after ';' and '{', if not already present
    for (int i = 0; i < output_.size(); i++) {
        MaybeWindPastComment(output_, i);
        char ch = output_[i];
        if (ch == ';' || ch == '{') {
            if (i == output_.size() - 1) {
                output_.append("\n");
            } else {
                size_t j = output_.find_first_not_of(kWsCharactersNoNewline, i + 1);
                // Unless the next thing is a comment.
                if (j != std::string::npos && !IsStartOfComment(output_, j)) {
                    // Make the next thing a newline.
                    if (IsNonNewlineWS(output_[i + 1])) {
                        output_[i + 1] = '\n';
                    } else if (output_[i + 1] != '\n') {
                        output_.insert(i + 1, "\n");
                    }
                }
            }
        }
    }

    // Insert lines before top level decls.
    if (is_top_level) {
        // Right before the last word in this string, we need a blank line,
        // followed by some (possibly zero) number of comment lines.  So we
        // break the string into lines, and then work backwards.

        std::stringstream ss(output_);
        std::string tmp;
        std::vector<std::string> lines;

        while (std::getline(ss, tmp, '\n')) {
            lines.push_back(tmp);
        }
        std::string terminal = lines.back();
        lines.pop_back();
        if (lines.size() == 1) {
            lines[0].append("\n");
        } else {
            // From the end of the list of lines, find the first line
            // that isn't a comment, and insert a blank line (if it
            // isn't already blank).
            int i = lines.size() - 1;
            while (i >= 0 && IsStartOfComment(lines[i], 0)) {
                i--;
            }

            if (!IsStartOfBlankLine(lines[i], 0)) {
                lines.insert(lines.begin() + i + 1, "");
            }
        }
        output_ = "";
        for (auto line : lines) {
            output_ += line + "\n";
        }
        output_ += terminal;
    }
}

int FormattingTreeVisitor::Segment::EraseMultipleSpacesAt(int pos, int leave_this_many, bool incl_newline) {
    std::function<bool(char)> is_ws;
    if (incl_newline) {
        is_ws = [](char ch) { return isspace(ch); };
    } else {
        is_ws = [](char ch) { return IsNonNewlineWS(ch); };
    }
    if (!is_ws(output_[pos])) {
        return 0;
    }
    int length_of_spaces = 0;

    int start_pos = pos;
    int end_pos = pos;
    while (start_pos > 0 && is_ws(output_[start_pos - 1])) {
        start_pos--;
    }

    // int_size - 2 can be negative, and output_.size() is unsigned,
    // cast to make the comparison work.
    int int_size = static_cast<int>(output_.size());
    while (end_pos <= int_size - 2 &&
           is_ws(output_[end_pos + 1])) {
        end_pos++;
    }

    length_of_spaces = end_pos - start_pos + 1;
    int num_deleted_spaces =
        std::max(length_of_spaces - leave_this_many, 0);
    output_.erase(start_pos, num_deleted_spaces);
    return num_deleted_spaces;
}

// Assumption: Trailing WS has been stripped, spaces have been changed to ' '
// Rules:
//  - No non-' ' or '\n' whitespace
//  - One ws token before / after every ws-requiring character
//  - No non-newline ws before / after characters that don't want it.
//  - "->" operators are never at the end of the line.
void FormattingTreeVisitor::Segment::RegularizeSpaces(bool& ws_required_next) {
    bool last_char_required_ws = false;

    // Check if this is still true from the last node.
    if (ws_required_next) {
        output_.insert(0, " ");
        ws_required_next = false;
    }

    for (int i = 0; i < output_.size(); i++) {
        // If it is a comment, jump to EOL.
        MaybeWindPastComment(output_, i);

        // If we see "->\n", change it to "\n->".
        const char arrow_nl[] = "->\n";
        if (output_.compare(i, strlen(arrow_nl), arrow_nl) == 0) {
            output_.replace(i, strlen(arrow_nl), "\n->");
            i -= EraseMultipleSpacesAt(i - 1, 0);
        }

        // Erase multiple spaces
        EraseMultipleSpacesAt(i);

        // Ensure whitespace around certain characters
        if (RequiresWSBeforeChar(output_[i])) {
            if (i == 0 || !isspace(output_[i - 1])) {
                output_.insert(i, " ");
                i++;
            }
        }

        // This is a little weird.  '(' requires ws if it follows an
        // arrow, but not if it follows a method name.  Both of these
        // are in interface method definitions, so this ends up being
        // slightly easier than having it positionally defined during
        // AST traversal.
        if (output_[i] == '(') {
            if (!last_char_required_ws && i > 0) {
                i -= EraseMultipleSpacesAt(i - 1, 0);
            }
        }

        // Ensure no whitespace around other characters
        if (NoSpacesBeforeChar(output_[i])) {
            if (i > 0) {
                i -= EraseMultipleSpacesAt(i - 1, 0, NoWSBeforeChar(output_[i]));
            }
        }

        // We don't want whitespace after these characters... unless there is a
        // comment after the WS.
        int j;
        for (j = i + 1;
             j < output_.size() && IsNonNewlineWS(output_[j]);
             j++)
            ;
        if (NoWSAfterChar(output_[i]) &&
            !IsStartOfComment(output_, j)) {
            EraseMultipleSpacesAt(i + 1, 0);
        }

        // The following clause is figuring out whether the next
        // iteration requires ws, so we need to keep it past anything
        // that uses that information in the loop.
        if (RequiresWSAfterChar(output_[i])) {
            if (i != output_.size() - 1 && !isspace(output_[i + 1])) {
                output_.insert(i + 1, " ");
                i++;
            }
            last_char_required_ws = true;
        } else {
            if (!isspace(output_[i])) {
                last_char_required_ws = false;
            }
        }
    }
    ws_required_next = last_char_required_ws;
}

// Rules are mostly obvious, but see TrackMethodInterfaceAlignment below.
// Precondition: By now, everything should have had its leading ws
// stripped, and } characters are the first things on their own lines.
void FormattingTreeVisitor::Segment::Indent(int& current_nesting) {
    for (int i = 0; i < output_.size(); i++) {
        if (output_[i] == '\n') {
            // Don't indent a blank line.
            if (output_[i + 1] == '\n') {
                continue;
            }
            // If this is an outdent line, do that.
            if (output_[i + 1] == '}') {
                current_nesting--;
            }
            int indent = current_nesting * kIndentSpaces;
            if (visitor_->newline_means_indent_more_) {
                if (visitor_->interface_method_alignment_ && visitor_->interface_method_alignment_size_ > -1) {
                    indent = visitor_->interface_method_alignment_size_;
                } else {
                    indent += kIndentSpaces;
                }
            }
            output_.insert(i + 1, indent, ' ');
        }

        int pos = i;
        // Skip comments at this point, because we don't want to
        // increase nesting based on a '{' character in a comment. :)
        MaybeWindPastComment(output_, pos);

        // 1 less than pos because i will be incremented on the next
        // iteration.  But that means it is a real character, so we need
        // to skip testing that character to see if it changes the
        // nesting level.
        if (pos != i) {
            i = pos - 1;
            continue;
        }

        if (output_[i] == '{') {
            current_nesting++;
        }
        if (output_[i] == ')') {
            visitor_->interface_method_alignment_size_ = visitor_->offset_of_first_id;
        }
        if (output_[i] == ';') {
            visitor_->interface_method_alignment_size_ = -1;
            visitor_->interface_method_alignment_ = false;
            visitor_->newline_means_indent_more_ = false;
        }
    }
}

// The purpose of this method is to figure out what the indentation will be if
// we encounter a newline.  The rule is :
//  - If there isn't a parameter on the same line after the '(' character, +1
//    indent past the beginning of the method name.
//  - If there is a parameter on the same line after the '(' character,
//    align at the same vertical column as that parameter.
void FormattingTreeVisitor::TrackInterfaceMethodAlignment(const std::string& str) {
    if (interface_method_alignment_) {
        bool next_nonws_char_is_checkpoint = false;
        for (int i = 0; i < str.size(); i++) {
            MaybeWindPastComment(str, i);

            char ch = str[i];
            if (ch == '\n') {
                distance_from_last_newline_ = 0;
            } else {
                distance_from_last_newline_++;
            }

            // This figures out if we are supposed to align to the '(' or the
            // method name.
            if (ch == '(') {
                bool align_on_oparen = false;
                for (int j = i + 1; j < str.size(); j++) {
                    MaybeWindPastComment(str, j);
                    if (str[j] == '\n')
                        break;
                    if (!IsNonNewlineWS(str[j]))
                        align_on_oparen = true;
                }
                if (align_on_oparen) {
                    interface_method_alignment_size_ = distance_from_last_newline_;
                }
            }

            // This tracks the distance from the beginning of the method name,
            // in case we need it (i.e., in case we don't indent to the '('
            // character.
            if (!isspace(ch) && next_nonws_char_is_checkpoint) {
                offset_of_first_id =
                    interface_method_alignment_size_ =
                        distance_from_last_newline_ + kIndentSpaces - 1;
                next_nonws_char_is_checkpoint = false;
            }
            if (str[i] == ':' && interface_method_alignment_size_ == -1) {
                // The first ':' we see - means it is the gap after the ordinal.
                // The next thing we see is the method name, so that might
                // become the indentation level.
                next_nonws_char_is_checkpoint = true;
            }
        }
    }
}

void FormattingTreeVisitor::OnFile(std::unique_ptr<fidl::raw::File> const& element) {
    // Eat ws at the beginning of the file.
    fidl::Token real_start = element->start_;
    fidl::StringView start_view = real_start.previous_end().data();
    const char* start_ptr = start_view.data();
    int initial_length = start_view.size();
    size_t offset = strspn(start_ptr, kWsCharacters);
    fidl::StringView processed_file_start(
        start_ptr + offset, initial_length - offset);
    element->start_.set_previous_end(
        fidl::SourceLocation(processed_file_start, real_start.previous_end().source_file()));

    fidl::raw::DeclarationOrderTreeVisitor::OnFile(element);
}

} // namespace raw
} // namespace fidl
