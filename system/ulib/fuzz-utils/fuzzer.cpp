// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include <fbl/auto_call.h>
#include <fbl/macros.h>
#include <fbl/string.h>
#include <fbl/string_buffer.h>
#include <fbl/string_piece.h>
#include <fbl/string_printf.h>
#include <fuzz-utils/fuzzer.h>
#include <fuzz-utils/path.h>
#include <fuzz-utils/string-list.h>
#include <lib/fdio/spawn.h>
#include <lib/zx/process.h>
#include <lib/zx/time.h>
#include <task-utils/walker.h>
#include <zircon/errors.h>
#include <zircon/status.h>
#include <zircon/syscalls/object.h>
#include <zircon/types.h>

namespace fuzzing {
namespace {

// List of supported subcommands
enum Command : uint32_t {
    kNone,
    kHelp,
    kList,
    kSeeds,
    kStart,
    kCheck,
    kStop,
    kRepro,
    kMerge,
};

// Usage information for specific tool subcommands.
const struct {
    Command cmd;
    const char* name;
    const char* args;
    const char* desc;
} kCommands[] = {
    {kHelp, "help", "", "Print this message and exit."},
    {kList, "list", "[name]", "Lists fuzzers matching 'name' if provided, or all fuzzers."},
    {kSeeds, "seeds", "name", "Lists the seed corpus location(s) for the fuzzer."},
    {kStart, "start", "name [...]",
     "Starts the named fuzzer.  Additional arguments are passed to the fuzzer."},
    {kCheck, "check", "name",
     "Reports information about the named fuzzer, such as execution status, corpus size, and "
     "number of artifacts."},
    {kStop, "stop", "name",
     "Stops all instances of the named fuzzer."},
    {kRepro, "repro", "name [...]",
     "Runs the named fuzzer on specific inputs. If no additional inputs are provided, uses "
     "previously found artifacts."},
    {kMerge, "merge", "name [...]",
     "Merges the corpus for the named fuzzer.  If no additional inputs are provided, minimizes the "
     "current corpus."},
};

// |kArtifactPrefixes| should matches the prefixes in libFuzzer passed to |Fuzzer::DumpCurrentUnit|
// or |Fuzzer::WriteUnitToFileWithPrefix|.
constexpr const char* kArtifactPrefixes[] = {
    "crash", "leak", "mismatch", "oom", "slow-unit", "timeout",
};
constexpr size_t kArtifactPrefixesLen = sizeof(kArtifactPrefixes) / sizeof(kArtifactPrefixes[0]);

} // namespace

// Public methods

Fuzzer::~Fuzzer() {}

zx_status_t Fuzzer::Main(int argc, char** argv) {
    Fuzzer fuzzer;
    StringList args(argv + 1, argc - 1);
    return fuzzer.Run(&args);
}

// Protected methods

Fuzzer::Fuzzer() : cmd_(kNone), out_(stdout), err_(stderr) {}

void Fuzzer::Reset() {
    cmd_ = kNone;
    name_.clear();
    target_.clear();
    root_.clear();
    resource_path_.Reset();
    data_path_.Reset();
    inputs_.clear();
    options_.clear();
    process_.reset();
    out_ = stdout;
    err_ = stderr;
}

zx_status_t Fuzzer::Run(StringList* args) {
    ZX_DEBUG_ASSERT(args);
    zx_status_t rc;

    if ((rc = SetCommand(args->first())) != ZX_OK || (rc = SetFuzzer(args->next())) != ZX_OK ||
        (rc = LoadOptions()) != ZX_OK) {
        return rc;
    }
    const char* arg;
    while ((arg = args->next())) {
        if (*arg != '-') {
            inputs_.push_back(arg);
        } else if ((rc = SetOption(arg + 1)) != ZX_OK) {
            return rc;
        }
    }
    switch (cmd_) {
    case kHelp:
        return Help();
    case kList:
        return List();
    case kSeeds:
        return Seeds();
    case kStart:
        return Start();
    case kCheck:
        return Check();
    case kStop:
        return Stop();
    case kRepro:
        return Repro();
    case kMerge:
        return Merge();
    default:
        // Shouldn't get here.
        ZX_DEBUG_ASSERT(false);
        return ZX_ERR_INTERNAL;
    }
}

zx_status_t Fuzzer::SetOption(const char* option) {
    ZX_DEBUG_ASSERT(option);

    const char* ptr = option;
    while (*ptr && *ptr != '#' && (*ptr == '-' || isspace(*ptr))) {
        ++ptr;
    }
    const char* mark = ptr;
    while (*ptr && *ptr != '#' && *ptr != '=' && !isspace(*ptr)) {
        ++ptr;
    }
    fbl::String key(mark, ptr - mark);
    while (*ptr && *ptr != '#' && (*ptr == '=' || isspace(*ptr))) {
        ++ptr;
    }
    mark = ptr;
    while (*ptr && *ptr != '#' && !isspace(*ptr)) {
        ++ptr;
    }
    fbl::String val(mark, ptr - mark);

    return SetOption(key.c_str(), val.c_str());
}

zx_status_t Fuzzer::SetOption(const char* key, const char* value) {
    ZX_DEBUG_ASSERT(key);
    ZX_DEBUG_ASSERT(value);

    // Ignore blank options
    if (*key == '\0' && *value == '\0') {
        return ZX_OK;
    }

    // Must have both key and value
    if (*key == '\0' || *value == '\0') {
        fprintf(err_, "Empty key or value: '%s'='%s'\n", key, value);
        return ZX_ERR_INVALID_ARGS;
    }

    // Save the option
    options_.set(key, value);

    return ZX_OK;
}

zx_status_t Fuzzer::RebasePath(const char* path, Path* out) {
    zx_status_t rc;

    out->Reset();
    if (!root_.empty() && (rc = out->Push(root_.c_str())) != ZX_OK) {
        fprintf(err_, "failed to move to '%s': %s\n", root_.c_str(), zx_status_get_string(rc));
        return rc;
    }
    if ((rc = out->Push(path)) != ZX_OK) {
        return rc;
    }

    return ZX_OK;
}

zx_status_t Fuzzer::GetPackagePath(const char* package, Path* out) {
    zx_status_t rc;

    if ((rc = RebasePath("pkgfs/packages", out)) != ZX_OK) {
        return rc;
    }
    auto pop_prefix = fbl::MakeAutoCall([&out]() { out->Pop(); });
    if ((rc = out->Push(package)) != ZX_OK) {
        fprintf(err_, "failed to move to '%s': %s\n", package, zx_status_get_string(rc));
        return rc;
    }
    auto pop_package = fbl::MakeAutoCall([&out]() { out->Pop(); });

    auto versions = out->List();
    long int max = -1;
    const char* max_version = nullptr;
    for (const char* version = versions->first(); version; version = versions->next()) {
        if (version[0] == '\0') {
            continue;
        }
        char* endptr = nullptr;
        long int val = strtol(version, &endptr, 10);
        if (endptr[0] != '\0') {
            continue;
        }
        if (val > max) {
            max = val;
            max_version = version;
        }
    }
    if (!max_version) {
        fprintf(err_, "No versions available for package: %s\n", package);
        return ZX_ERR_NOT_FOUND;
    }

    if ((rc = out->Push(max_version)) != ZX_OK) {
        fprintf(err_, "failed to move to '%s': %s\n", max_version, zx_status_get_string(rc));
        return rc;
    }

    pop_package.cancel();
    pop_prefix.cancel();
    return ZX_OK;
}

void Fuzzer::FindZirconFuzzers(const char* zircon_path, const char* target, StringMap* out) {
    Path path;
    if (RebasePath(zircon_path, &path) != ZX_OK) {
        return;
    }

    auto targets = path.List();
    for (const char* t = targets->first(); t; t = targets->next()) {
    }

    targets->keep_if(target);
    for (const char* t = targets->first(); t; t = targets->next()) {
        out->set(fbl::StringPrintf("zircon_fuzzers/%s", t).c_str(), path.Join(t).c_str());
    }
}

void Fuzzer::FindFuchsiaFuzzers(const char* package, const char* target, StringMap* out) {
    Path path;
    if (RebasePath("pkgfs/packages", &path) != ZX_OK) {
        return;
    }

    auto packages = path.List();
    packages->keep_if("_fuzzers");
    packages->keep_if(package);

    for (const char* p = packages->first(); p; p = packages->next()) {
        if (GetPackagePath(p, &path) != ZX_OK || path.Push("meta") != ZX_OK) {
            continue;
        }

        auto targets = path.List();
        targets->keep_if(target);
        targets->keep_if(".cmx");

        fbl::String abspath;
        for (const char* t = targets->first(); t; t = targets->next()) {
            fbl::String t1(t, strlen(t) - strlen(".cmx"));
            out->set(fbl::StringPrintf("%s/%s", p, t1.c_str()).c_str(),
                     fbl::StringPrintf("fuchsia-pkg://fuchsia.com/%s#meta/%s", p, t).c_str());
        }
    }
}

void Fuzzer::FindFuzzers(const char* package, const char* target, StringMap* out) {
    if (strstr("zircon_fuzzers", package) != nullptr) {
        FindZirconFuzzers("boot/test/fuzz", target, out);
        FindZirconFuzzers("system/test/fuzz", target, out);
    }
    FindFuchsiaFuzzers(package, target, out);
}

static zx_status_t ParseName(const char* name, fbl::String* out_package, fbl::String* out_target) {
    const char* sep = name ? strchr(name, '/') : nullptr;
    if (!sep) {
        return ZX_ERR_NOT_FOUND;
    }
    out_package->Set(name, sep - name);
    out_target->Set(sep + 1);
    return ZX_OK;
}

void Fuzzer::FindFuzzers(const char* name, StringMap* out) {
    ZX_DEBUG_ASSERT(out);

    // Scan the system for available fuzzers
    out->clear();
    fbl::String package, target;
    if (ParseName(name, &package, &target) == ZX_OK) {
        FindFuzzers(package.c_str(), target.c_str(), out);
    } else if (name) {
        FindFuzzers(name, "", out);
        FindFuzzers("", name, out);
    } else {
        FindFuzzers("", "", out);
    }
}

void Fuzzer::GetArgs(StringList* out) {
    out->clear();
    if (strstr(target_.c_str(), "fuchsia-pkg://fuchsia.com/") == target_.c_str()) {
        out->push_back("/system/bin/run");
    }
    out->push_back(target_.c_str());
    const char* key;
    const char* val;
    options_.begin();
    while (options_.next(&key, &val)) {
        out->push_back(fbl::StringPrintf("-%s=%s", key, val).c_str());
    }
    for (const char* input = inputs_.first(); input; input = inputs_.next()) {
        out->push_back(input);
    }
}

zx_status_t Fuzzer::Execute(bool wait_for_completion) {
    zx_status_t rc;

    StringList args;
    GetArgs(&args);

    size_t argc = args.length();
    const char* argv[argc + 1];

    argv[0] = args.first();
    fprintf(out_, "+ %s", argv[0]);
    for (size_t i = 1; i < argc; ++i) {
        argv[i] = args.next();
        fprintf(out_, " %s", argv[i]);
    }
    argv[argc] = nullptr;
    fprintf(out_, "\n");

    if ((rc = fdio_spawn(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL, argv[0], argv,
                         process_.reset_and_get_address())) != ZX_OK) {
        fprintf(err_, "Failed to spawn '%s': %s\n", argv[0], zx_status_get_string(rc));
        return rc;
    }

    if (!wait_for_completion) {
        return ZX_OK;
    }

    if ((rc = process_.wait_one(ZX_TASK_TERMINATED, zx::time::infinite(), nullptr)) != ZX_OK) {
        fprintf(err_, "Failed while waiting for process to end: %s\n", zx_status_get_string(rc));
        return rc;
    }

    zx_info_process_t proc_info;
    if ((rc = process_.get_info(ZX_INFO_PROCESS, &proc_info, sizeof(proc_info), nullptr,
                                nullptr)) != ZX_OK) {
        fprintf(err_, "Failed to get exit code for process: %s\n", zx_status_get_string(rc));
        return rc;
    }

    if (proc_info.return_code != ZX_OK) {
        fprintf(out_, "Fuzzer returned non-zero exit code: %" PRId64 "\n", proc_info.return_code);
    }

    return ZX_OK;
}

// |fuzzing::Walker| is a |TaskEnumerator| used to find a given fuzzer |executable| and print status
// or end it.
class Walker final : public TaskEnumerator {
public:
    explicit Walker(const Fuzzer* fuzzer, bool kill) : fuzzer_(fuzzer), kill_(kill), killed_(0) {}
    ~Walker() {}

    size_t killed() const { return killed_; }

    zx_status_t OnProcess(int depth, zx_handle_t task, zx_koid_t koid, zx_koid_t pkoid) override {
        if (!fuzzer_->CheckProcess(task, kill_)) {
            return ZX_OK;
        }
        if (kill_) {
            ++killed_;
            return ZX_OK;
        }
        return ZX_ERR_STOP;
    }

protected:
    bool has_on_process() const override { return true; }

private:
    const Fuzzer* fuzzer_;
    bool kill_;
    size_t killed_;
};

bool Fuzzer::CheckProcess(zx_handle_t task, bool kill) const {
    char name[ZX_MAX_NAME_LEN];

    if (zx_object_get_property(task, ZX_PROP_NAME, name, sizeof(name)) != ZX_OK) {
        return false;
    }

    const char* target = target_.c_str();
    const char* meta = strstr(target, "#meta/");
    if (meta) {
        target = meta + strlen("#meta/");
    }
    if (strcmp(name, target) != 0) {
        return false;
    }
    if (kill) {
        zx_task_kill(task);
        return true;
    }

    zx_info_process_t info;

    if (zx_object_get_info(task, ZX_INFO_PROCESS, &info, sizeof(info), nullptr, nullptr) != ZX_OK) {
        return false;
    }
    if (!info.started) {
        fprintf(out_, "%s: NOT STARTED\n", name_.c_str());
    } else if (!info.exited) {
        fprintf(out_, "%s: RUNNING\n", name_.c_str());
    } else {
        fprintf(out_, "%s: EXITED (return code = %" PRId64 ")\n", name_.c_str(), info.return_code);
    }

    return true;
}

// Private methods

zx_status_t Fuzzer::SetCommand(const char* command) {
    cmd_ = kNone;
    options_.clear();
    inputs_.clear();

    if (!command) {
        fprintf(err_, "Missing command. Try 'help'.\n");
        return ZX_ERR_INVALID_ARGS;
    }
    for (size_t i = 0; i < sizeof(kCommands) / sizeof(kCommands[0]); ++i) {
        if (strcmp(command, kCommands[i].name) == 0) {
            cmd_ = kCommands[i].cmd;
            break;
        }
    }
    if (cmd_ == kNone) {
        fprintf(err_, "Unknown command '%s'. Try 'help'.\n", command);
        return ZX_ERR_INVALID_ARGS;
    }

    return ZX_OK;
}

zx_status_t Fuzzer::SetFuzzer(const char* name) {
    zx_status_t rc;

    // Early exit for commands that don't need a single, selected fuzzer
    switch (cmd_) {
    case kHelp:
    case kList:
        if (name) {
            name_.Set(name);
        }
        return ZX_OK;

    default:
        break;
    }

    if (!name) {
        fprintf(err_, "Missing fuzzer name.\n");
        return ZX_ERR_INVALID_ARGS;
    }
    name_.Set(name);

    // Determine the fuzzer
    StringMap fuzzers;
    FindFuzzers(name, &fuzzers);
    switch (fuzzers.size()) {
    case 0:
        fprintf(err_, "No matching fuzzers for '%s'.\n", name);
        return ZX_ERR_NOT_FOUND;
    case 1:
        break;
    default:
        fprintf(err_, "Multiple matching fuzzers for '%s':\n", name);
        List();
        return ZX_ERR_INVALID_ARGS;
    }

    const char* executable;
    fuzzers.begin();
    fuzzers.next(&name, &executable);
    name_.Set(name);
    target_.Set(executable);

    fbl::String package, target;
    if ((rc = ParseName(name_.c_str(), &package, &target)) != ZX_OK) {
        return rc;
    }

    // Determine the directory that holds the fuzzing resources. It may not be present if fuzzing
    // Zircon standalone.
    if ((rc = GetPackagePath(package.c_str(), &resource_path_)) != ZX_OK ||
        (rc = resource_path_.Push("data")) != ZX_OK ||
        (rc = resource_path_.Push(target.c_str())) != ZX_OK) {
        // No-op: The directory may not be present when fuzzing standalone Zircon.
        resource_path_.Reset();
    }

    // Ensure the directory that will hold the fuzzing artifacts is present.
    if ((rc = RebasePath("data", &data_path_)) != ZX_OK ||
        (rc = data_path_.Ensure("fuzzing")) != ZX_OK ||
        (rc = data_path_.Push("fuzzing")) != ZX_OK ||
        (rc = data_path_.Ensure(package.c_str())) != ZX_OK ||
        (rc = data_path_.Push(package.c_str())) != ZX_OK ||
        (rc = data_path_.Ensure(target.c_str())) != ZX_OK ||
        (rc = data_path_.Push(target.c_str())) != ZX_OK) {
        fprintf(err_, "Failed to establish data path for '%s/%s': %s\n", package.c_str(),
                target.c_str(), zx_status_get_string(rc));
        return ZX_ERR_IO;
    }

    return ZX_OK;
}

zx_status_t Fuzzer::LoadOptions() {
    zx_status_t rc;
    switch (cmd_) {
    case kHelp:
    case kList:
    case kSeeds:
        // No options needed
        return ZX_OK;

    case kMerge:
        if ((rc = SetOption("merge", "1")) != ZX_OK ||
            (rc = SetOption("merge_control_file", data_path_.Join(".mergefile").c_str())) !=
                ZX_OK) {
            return rc;
        }
        break;

    default:
        break;
    }

    // Artifacts go in the data directory
    if ((rc = SetOption("artifact_prefix", data_path_.c_str())) != ZX_OK) {
        return rc;
    }

    // Early exit if no resources
    if (strlen(resource_path_.c_str()) <= 1) {
        return ZX_OK;
    }

    // Record the (optional) dictionary
    size_t dict_size;
    if ((rc = resource_path_.GetSize("dictionary", &dict_size)) == ZX_OK && dict_size != 0 &&
        (rc = SetOption("dict", resource_path_.Join("dictionary").c_str())) != ZX_OK) {
        fprintf(err_, "failed to set dictionary option: %s\n", zx_status_get_string(rc));
        return rc;
    }

    // Read the (optional) options file
    fbl::String options = resource_path_.Join("options");
    FILE* f = fopen(options.c_str(), "r");
    if (f) {
        auto close_f = fbl::MakeAutoCall([&f]() { fclose(f); });
        char buffer[PATH_MAX];
        while (fgets(buffer, sizeof(buffer), f)) {
            if ((rc = SetOption(buffer)) != ZX_OK) {
                fprintf(err_, "Failed to set option: %s", zx_status_get_string(rc));
                return rc;
            }
        }
    }

    return ZX_OK;
}

// Specific subcommands

zx_status_t Fuzzer::Help() {
    fprintf(out_, "usage: fuzz <command> [args]\n\n");
    fprintf(out_, "Supported commands are:\n");
    for (size_t i = 0; i < sizeof(kCommands) / sizeof(kCommands[0]); ++i) {
        fprintf(out_, "  %s %s\n", kCommands[i].name, kCommands[i].args);
        fprintf(out_, "    %s\n\n", kCommands[i].desc);
    }
    return ZX_OK;
}

zx_status_t Fuzzer::List() {
    StringMap fuzzers;
    FindFuzzers(name_.c_str(), &fuzzers);
    if (fuzzers.is_empty()) {
        fprintf(out_, "No matching fuzzers.\n");
        return ZX_OK;
    }
    fprintf(out_, "Found %zu matching fuzzers:\n", fuzzers.size());
    const char* name;
    fuzzers.begin();
    while (fuzzers.next(&name, nullptr)) {
        fprintf(out_, "  %s\n", name);
    }
    return ZX_OK;
}

zx_status_t Fuzzer::Seeds() {
    if (strlen(resource_path_.c_str()) <= 1) {
        fprintf(out_, "No seed corpora found for %s.\n", name_.c_str());
        return ZX_OK;
    }
    fbl::String corpora = resource_path_.Join("corpora");
    FILE* f = fopen(corpora.c_str(), "r");
    if (!f) {
        fprintf(out_, "No seed corpora found for %s.\n", name_.c_str());
        return ZX_OK;
    }
    auto close_f = fbl::MakeAutoCall([&f]() { fclose(f); });

    char buffer[PATH_MAX];
    while (fgets(buffer, sizeof(buffer), f)) {
        fprintf(out_, "%s\n", buffer);
    }
    return ZX_OK;
}

zx_status_t Fuzzer::Start() {
    zx_status_t rc;

    // If no inputs, use the default corpus
    if (inputs_.is_empty()) {
        if ((rc = data_path_.Ensure("corpus")) != ZX_OK) {
            fprintf(err_, "Failed to make empty corpus: %s\n", zx_status_get_string(rc));
            return rc;
        }
        inputs_.push_front(data_path_.Join("corpus").c_str());
    }

    return Execute(false /* !wait_for_completion */);
}

zx_status_t Fuzzer::Check() {
    zx_status_t rc;

    // Report fuzzer execution status
    Walker walker(this, false /* !kill */);
    if (walker.WalkRootJobTree() != ZX_ERR_STOP) {
        fprintf(out_, "%s: STOPPED\n", name_.c_str());
    }

    // Fuzzer details
    fprintf(out_, "    Target info:  %s\n", target_.c_str());
    fprintf(out_, "    Output path:  %s\n", data_path_.c_str());

    // Report corpus details, if present
    if ((rc = data_path_.Push("corpus")) != ZX_OK) {
        fprintf(out_, "    Corpus size:  0 inputs / 0 bytes\n");
    } else {
        auto corpus = data_path_.List();
        size_t corpus_size = 0;
        for (const char* input = corpus->first(); input; input = corpus->next()) {
            size_t input_size;
            if ((rc = data_path_.GetSize(input, &input_size)) != ZX_OK) {
                return rc;
            }
            corpus_size += input_size;
        }
        fprintf(out_, "    Corpus size:  %zu inputs / %zu bytes\n", corpus->length(), corpus_size);
        data_path_.Pop();
    }

    // Report number of artifacts.
    auto artifacts = data_path_.List();
    StringList prefixes(kArtifactPrefixes,
                        sizeof(kArtifactPrefixes) / sizeof(kArtifactPrefixes[0]));
    artifacts->keep_if_any(&prefixes);
    size_t num_artifacts = artifacts->length();
    if (num_artifacts == 0) {
        fprintf(out_, "    Artifacts:    None\n");
    } else {
        const char* artifact = artifacts->first();
        fprintf(out_, "    Artifacts:    %s\n", artifact);
        while ((artifact = artifacts->next())) {
            fprintf(out_, "                  %s\n", artifact);
        }
    }

    return ZX_OK;
}

zx_status_t Fuzzer::Stop() {
    Walker walker(this, true /* kill */);
    walker.WalkRootJobTree();
    fprintf(out_, "Stopped %zu tasks.\n", walker.killed());
    return ZX_OK;
}

zx_status_t Fuzzer::Repro() {
    zx_status_t rc;

    // If no patterns, match all artifacts
    if (inputs_.is_empty()) {
        inputs_.push_back("");
    }

    // Filter data for just artifacts that match one or more supplied patterns
    auto artifacts = data_path_.List();
    StringList prefixes(kArtifactPrefixes, kArtifactPrefixesLen);
    artifacts->keep_if_any(&prefixes);
    artifacts->keep_if_any(&inputs_);

    // Get full paths of artifacts
    inputs_.clear();
    for (const char* artifact = artifacts->first(); artifact; artifact = artifacts->next()) {
        inputs_.push_back(data_path_.Join(artifact).c_str());
    }

    // Nothing to repro
    if (inputs_.is_empty()) {
        fprintf(err_, "No matching artifacts found.\n");
        return ZX_ERR_NOT_FOUND;
    }

    if ((rc = Execute(true /* wait_for_completion */)) != ZX_OK) {
        fprintf(err_, "Failed to execute: %s\n", zx_status_get_string(rc));
        return rc;
    }

    return ZX_OK;
}

zx_status_t Fuzzer::Merge() {
    zx_status_t rc;

    // If no inputs, minimize the previous corpus (and there must be an existing corpus!)
    if (inputs_.is_empty()) {
        if ((rc = data_path_.Remove("corpus.prev")) != ZX_OK ||
            (rc = data_path_.Rename("corpus", "corpus.prev")) != ZX_OK) {
            fprintf(err_, "Failed to move 'corpus' for minimization: %s\n",
                    zx_status_get_string(rc));
            return rc;
        }
        inputs_.push_back(data_path_.Join("corpus.prev").c_str());
    }

    // Make sure the corpus directory exists, and make sure the output corpus is the first argument
    if ((rc = data_path_.Ensure("corpus")) != ZX_OK) {
        fprintf(err_, "Failed to ensure 'corpus': %s\n", zx_status_get_string(rc));
        return rc;
    }
    inputs_.erase_if(data_path_.Join("corpus").c_str());
    inputs_.push_front(data_path_.Join("corpus").c_str());

    if ((rc = Execute(false /* !wait_for_completion */)) != ZX_OK) {
        fprintf(err_, "Failed to execute: %s\n", zx_status_get_string(rc));
        return rc;
    }

    return ZX_OK;
}

} // namespace fuzzing
