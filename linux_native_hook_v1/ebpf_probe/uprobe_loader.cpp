#include "ebpf_probe/uprobe_common.h"
#include "uprobe_probe.skel.h"

#include <bpf/bpf.h>
#include <bpf/libbpf.h>
#include <fcntl.h>
#include <gelf.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct Options {
    std::string mode_name = "libbpf_count_only";
    std::string libc_path;
    std::uint32_t sample_interval = 1;
    std::int64_t filter_size = -1;
    std::vector<std::string> workload_args;
};

struct ChildProcess {
    pid_t pid = -1;
    int start_write_fd = -1;
    int output_read_fd = -1;
};

struct RingState {
    std::uint64_t observed_events = 0;
    std::uint64_t observed_alloc_events = 0;
    std::uint64_t observed_free_events = 0;
};

void CloseFd(int *fd)
{
    if (*fd >= 0) {
        close(*fd);
        *fd = -1;
    }
}

void SetCloseOnExec(int fd)
{
    int flags = fcntl(fd, F_GETFD, 0);
    if (flags < 0 || fcntl(fd, F_SETFD, flags | FD_CLOEXEC) < 0) {
        throw std::runtime_error("failed to set FD_CLOEXEC");
    }
}

void SetNonBlocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        throw std::runtime_error("failed to set O_NONBLOCK");
    }
}

int ParseMode(std::string_view mode)
{
    if (mode == "libbpf_count_only") {
        return kLnhv1ModeCountOnly;
    }
    if (mode == "libbpf_sample_filter") {
        return kLnhv1ModeSampleFilter;
    }
    if (mode == "libbpf_tracking") {
        return kLnhv1ModeTracking;
    }
    if (mode == "libbpf_ring_output") {
        return kLnhv1ModeRingOutput;
    }
    throw std::runtime_error("invalid mode: " + std::string(mode));
}

void PrintUsage(const char *argv0)
{
    std::cerr
        << "usage: " << argv0
        << " --mode libbpf_count_only|libbpf_sample_filter|libbpf_tracking|libbpf_ring_output"
        << " --libc /path/to/libc.so.6 [--sample-interval N] [--filter-size N]"
        << " -- <workload> [args...]\n";
}

Options ParseArgs(int argc, char **argv)
{
    Options options;
    bool after_separator = false;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (after_separator) {
            options.workload_args.push_back(arg);
            continue;
        }

        if (arg == "--") {
            after_separator = true;
        } else if (arg == "--mode" && i + 1 < argc) {
            options.mode_name = argv[++i];
        } else if (arg == "--libc" && i + 1 < argc) {
            options.libc_path = argv[++i];
        } else if (arg == "--sample-interval" && i + 1 < argc) {
            options.sample_interval = static_cast<std::uint32_t>(std::stoul(argv[++i]));
            if (options.sample_interval == 0) {
                options.sample_interval = 1;
            }
        } else if (arg == "--filter-size" && i + 1 < argc) {
            options.filter_size = std::stoll(argv[++i]);
        } else if (arg == "--help" || arg == "-h") {
            PrintUsage(argv[0]);
            std::exit(0);
        } else {
            throw std::runtime_error("unknown argument: " + arg);
        }
    }

    if (options.libc_path.empty()) {
        throw std::runtime_error("--libc is required");
    }
    if (options.workload_args.empty()) {
        throw std::runtime_error("workload command is required after --");
    }
    ParseMode(options.mode_name);
    return options;
}

ChildProcess StartBlockedChild(const std::vector<std::string> &workload_args)
{
    int start_pipe[2] = {-1, -1};
    int output_pipe[2] = {-1, -1};
    if (pipe(start_pipe) != 0 || pipe(output_pipe) != 0) {
        throw std::runtime_error("pipe failed");
    }
    SetCloseOnExec(start_pipe[0]);
    SetCloseOnExec(start_pipe[1]);
    SetCloseOnExec(output_pipe[0]);
    SetCloseOnExec(output_pipe[1]);

    pid_t pid = fork();
    if (pid < 0) {
        throw std::runtime_error("fork failed");
    }

    if (pid == 0) {
        close(start_pipe[1]);
        close(output_pipe[0]);
        if (dup2(output_pipe[1], STDOUT_FILENO) < 0) {
            _exit(126);
        }
        close(output_pipe[1]);

        char token = 0;
        ssize_t got = read(start_pipe[0], &token, sizeof(token));
        close(start_pipe[0]);
        if (got != 1) {
            _exit(125);
        }

        std::vector<char *> exec_args;
        exec_args.reserve(workload_args.size() + 1);
        for (const std::string &item : workload_args) {
            exec_args.push_back(const_cast<char *>(item.c_str()));
        }
        exec_args.push_back(nullptr);
        execv(exec_args[0], exec_args.data());
        _exit(127);
    }

    close(start_pipe[0]);
    close(output_pipe[1]);
    SetNonBlocking(output_pipe[0]);
    return ChildProcess{pid, start_pipe[1], output_pipe[0]};
}

void StartChild(ChildProcess *child)
{
    char token = 1;
    if (write(child->start_write_fd, &token, sizeof(token)) != sizeof(token)) {
        throw std::runtime_error("failed to release child workload");
    }
    CloseFd(&child->start_write_fd);
}

void ReadAvailable(int fd, std::string *output)
{
    char buffer[4096];
    for (;;) {
        ssize_t got = read(fd, buffer, sizeof(buffer));
        if (got > 0) {
            output->append(buffer, static_cast<std::size_t>(got));
            continue;
        }
        if (got == 0) {
            return;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return;
        }
        throw std::runtime_error("failed reading workload output");
    }
}

std::string ExtractLastMetric(const std::string &text, std::string_view key)
{
    std::string needle(key);
    needle += "=";
    std::size_t pos = text.rfind(needle);
    if (pos == std::string::npos) {
        return "0";
    }
    pos += needle.size();
    std::size_t end = pos;
    while (end < text.size() && text[end] != ' ' && text[end] != '\n' &&
           text[end] != '\r' && text[end] != '\t') {
        ++end;
    }
    return text.substr(pos, end - pos);
}

std::uint64_t ResolveSymbolOffset(const std::string &binary_path,
                                  const char *symbol_name)
{
    if (elf_version(EV_CURRENT) == EV_NONE) {
        throw std::runtime_error("libelf initialization failed");
    }

    int fd = open(binary_path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        throw std::runtime_error("failed to open ELF binary: " + binary_path);
    }

    Elf *elf = elf_begin(fd, ELF_C_READ, nullptr);
    if (!elf) {
        close(fd);
        throw std::runtime_error("elf_begin failed for: " + binary_path);
    }

    std::uint64_t result = 0;
    Elf_Scn *section = nullptr;
    while ((section = elf_nextscn(elf, section)) != nullptr) {
        GElf_Shdr shdr;
        if (!gelf_getshdr(section, &shdr)) {
            continue;
        }
        if (shdr.sh_type != SHT_DYNSYM && shdr.sh_type != SHT_SYMTAB) {
            continue;
        }

        Elf_Data *data = elf_getdata(section, nullptr);
        if (!data || shdr.sh_entsize == 0) {
            continue;
        }

        std::size_t count = shdr.sh_size / shdr.sh_entsize;
        for (std::size_t i = 0; i < count; ++i) {
            GElf_Sym sym;
            if (!gelf_getsym(data, static_cast<int>(i), &sym)) {
                continue;
            }
            const char *name = elf_strptr(elf, shdr.sh_link, sym.st_name);
            if (!name || std::strcmp(name, symbol_name) != 0 || sym.st_value == 0) {
                continue;
            }
            result = sym.st_value;
            break;
        }
        if (result != 0) {
            break;
        }
    }

    elf_end(elf);
    close(fd);

    if (result == 0) {
        throw std::runtime_error(std::string("failed to resolve symbol: ") + symbol_name);
    }
    return result;
}

struct bpf_link *AttachUprobe(struct bpf_program *program,
                              const std::string &binary_path,
                              const char *symbol_name,
                              bool retprobe)
{
#ifdef LNHV1_HAS_LIBBPF_UPROBE_FUNC_NAME
    {
        struct bpf_uprobe_opts opts = {};
        opts.sz = sizeof(opts);
        opts.retprobe = retprobe;
        opts.func_name = symbol_name;
        struct bpf_link *link = bpf_program__attach_uprobe_opts(
            program, -1, binary_path.c_str(), 0, &opts);
        long err = libbpf_get_error(link);
        if (err == 0) {
            return link;
        }
        std::cerr << "func_name uprobe attach failed for " << symbol_name
                  << ", falling back to ELF symbol offset: " << -err << "\n";
    }
#endif

    std::uint64_t offset = ResolveSymbolOffset(binary_path, symbol_name);
    struct bpf_link *link = bpf_program__attach_uprobe(
        program, retprobe, -1, binary_path.c_str(), offset);
    long err = libbpf_get_error(link);
    if (err != 0) {
        throw std::runtime_error(std::string("failed to attach uprobe for ") +
                                 symbol_name + ": " + std::to_string(-err));
    }
    return link;
}

int HandleRingEvent(void *ctx, void *data, std::size_t size)
{
    auto *state = static_cast<RingState *>(ctx);
    if (size < sizeof(Lnhv1UprobeEvent)) {
        return 0;
    }
    auto *event = static_cast<Lnhv1UprobeEvent *>(data);
    state->observed_events += 1;
    if (event->type == kLnhv1EventAlloc) {
        state->observed_alloc_events += 1;
    } else if (event->type == kLnhv1EventFree) {
        state->observed_free_events += 1;
    }
    return 0;
}

Lnhv1UprobeStats ReadStats(const struct uprobe_probe_bpf *skel)
{
    Lnhv1UprobeStats total = {};
    int cpu_count = libbpf_num_possible_cpus();
    if (cpu_count <= 0) {
        throw std::runtime_error("failed to query possible CPU count");
    }

    std::vector<Lnhv1UprobeStats> values(static_cast<std::size_t>(cpu_count));
    __u32 key = 0;
    int fd = bpf_map__fd(skel->maps.stats);
    if (bpf_map_lookup_elem(fd, &key, values.data()) != 0) {
        throw std::runtime_error("failed to read stats map");
    }

    for (const Lnhv1UprobeStats &item : values) {
        total.malloc_calls += item.malloc_calls;
        total.calloc_calls += item.calloc_calls;
        total.realloc_calls += item.realloc_calls;
        total.free_calls += item.free_calls;
        total.sampled_malloc_entries += item.sampled_malloc_entries;
        total.sampled_calloc_entries += item.sampled_calloc_entries;
        total.sampled_realloc_entries += item.sampled_realloc_entries;
        total.sampled_alloc_returns += item.sampled_alloc_returns;
        total.alloc_records += item.alloc_records;
        total.matched_frees += item.matched_frees;
        total.unmatched_frees += item.unmatched_frees;
        total.output_records += item.output_records;
        total.ringbuf_drops += item.ringbuf_drops;
    }
    return total;
}

int WaitForChild(ChildProcess *child, struct ring_buffer *ringbuf, std::string *output)
{
    int status = 0;
    for (;;) {
        ReadAvailable(child->output_read_fd, output);
        if (ringbuf) {
            int err = ring_buffer__poll(ringbuf, 25);
            if (err < 0 && err != -EINTR) {
                throw std::runtime_error("ring_buffer__poll failed");
            }
        } else {
            usleep(25000);
        }

        pid_t done = waitpid(child->pid, &status, WNOHANG);
        if (done == child->pid) {
            break;
        }
        if (done < 0 && errno != EINTR) {
            throw std::runtime_error("waitpid failed");
        }
    }

    for (int i = 0; i < 8; ++i) {
        ReadAvailable(child->output_read_fd, output);
        if (ringbuf) {
            ring_buffer__poll(ringbuf, 10);
        }
    }
    CloseFd(&child->output_read_fd);
    return status;
}

}  // namespace

int main(int argc, char **argv)
{
    try {
        Options options = ParseArgs(argc, argv);
        int mode = ParseMode(options.mode_name);

        libbpf_set_strict_mode(LIBBPF_STRICT_ALL);
        ChildProcess child = StartBlockedChild(options.workload_args);

        struct uprobe_probe_bpf *skel = uprobe_probe_bpf__open();
        if (!skel) {
            throw std::runtime_error("failed to open BPF skeleton");
        }
        if (uprobe_probe_bpf__load(skel) != 0) {
            uprobe_probe_bpf__destroy(skel);
            throw std::runtime_error("failed to load BPF skeleton");
        }

        Lnhv1UprobeConfig config = {};
        config.mode = static_cast<std::uint32_t>(mode);
        config.sample_interval = options.sample_interval;
        config.filter_size = options.filter_size;
        config.target_tgid = static_cast<std::uint32_t>(child.pid);
        __u32 config_key = 0;
        if (bpf_map_update_elem(bpf_map__fd(skel->maps.probe_config),
                                &config_key,
                                &config,
                                BPF_ANY) != 0) {
            uprobe_probe_bpf__destroy(skel);
            throw std::runtime_error("failed to update config map");
        }

        struct bpf_link *malloc_entry = AttachUprobe(
            skel->progs.handle_malloc_entry, options.libc_path, "malloc", false);
        struct bpf_link *malloc_return = AttachUprobe(
            skel->progs.handle_malloc_return, options.libc_path, "malloc", true);
        struct bpf_link *calloc_entry = AttachUprobe(
            skel->progs.handle_calloc_entry, options.libc_path, "calloc", false);
        struct bpf_link *calloc_return = AttachUprobe(
            skel->progs.handle_calloc_return, options.libc_path, "calloc", true);
        struct bpf_link *realloc_entry = AttachUprobe(
            skel->progs.handle_realloc_entry, options.libc_path, "realloc", false);
        struct bpf_link *realloc_return = AttachUprobe(
            skel->progs.handle_realloc_return, options.libc_path, "realloc", true);
        struct bpf_link *free_entry = AttachUprobe(
            skel->progs.handle_free_entry, options.libc_path, "free", false);

        RingState ring_state;
        struct ring_buffer *ringbuf = nullptr;
        if (mode >= kLnhv1ModeRingOutput) {
            ringbuf = ring_buffer__new(bpf_map__fd(skel->maps.events),
                                       HandleRingEvent,
                                       &ring_state,
                                       nullptr);
            if (!ringbuf) {
                throw std::runtime_error("failed to create ring buffer reader");
            }
        }

        StartChild(&child);
        std::string workload_output;
        int status = WaitForChild(&child, ringbuf, &workload_output);
        Lnhv1UprobeStats stats = ReadStats(skel);

        if (ringbuf) {
            ring_buffer__free(ringbuf);
        }
        bpf_link__destroy(free_entry);
        bpf_link__destroy(realloc_return);
        bpf_link__destroy(realloc_entry);
        bpf_link__destroy(calloc_return);
        bpf_link__destroy(calloc_entry);
        bpf_link__destroy(malloc_return);
        bpf_link__destroy(malloc_entry);
        uprobe_probe_bpf__destroy(skel);

        std::cout << workload_output;
        if (!workload_output.empty() && workload_output.back() != '\n') {
            std::cout << "\n";
        }

        std::string throughput = ExtractLastMetric(workload_output, "throughput_ops");
        std::cout << "lnhv1_libbpf_summary"
                  << " backend=libbpf"
                  << " mode=" << options.mode_name
                  << " target_tgid=" << config.target_tgid
                  << " throughput_ops=" << throughput
                  << " malloc_calls=" << stats.malloc_calls
                  << " calloc_calls=" << stats.calloc_calls
                  << " realloc_calls=" << stats.realloc_calls
                  << " free_calls=" << stats.free_calls
                  << " sampled_malloc_entries=" << stats.sampled_malloc_entries
                  << " sampled_calloc_entries=" << stats.sampled_calloc_entries
                  << " sampled_realloc_entries=" << stats.sampled_realloc_entries
                  << " sampled_alloc_returns=" << stats.sampled_alloc_returns
                  << " alloc_records=" << stats.alloc_records
                  << " matched_frees=" << stats.matched_frees
                  << " unmatched_frees=" << stats.unmatched_frees
                  << " output_records=" << stats.output_records
                  << " ringbuf_drops=" << stats.ringbuf_drops
                  << " observed_events=" << ring_state.observed_events
                  << " observed_alloc_events=" << ring_state.observed_alloc_events
                  << " observed_free_events=" << ring_state.observed_free_events
                  << "\n";

        if (WIFEXITED(status)) {
            return WEXITSTATUS(status);
        }
        if (WIFSIGNALED(status)) {
            return 128 + WTERMSIG(status);
        }
        return 1;
    } catch (const std::exception &ex) {
        std::cerr << "uprobe_loader: " << ex.what() << "\n";
        return 1;
    }
}
