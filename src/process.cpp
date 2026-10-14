#include "astra/process.hpp"

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <stdexcept>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace astra {
ProcessResult run_process(const std::vector<std::string>& arguments, int timeout_seconds, size_t max_output_bytes) {
    if (arguments.empty() || timeout_seconds <= 0) throw std::invalid_argument("invalid process request");
    int outpipe[2], errpipe[2];
    if (pipe2(outpipe, O_CLOEXEC | O_NONBLOCK) != 0) throw std::runtime_error("cannot create process pipe");
    if (pipe2(errpipe, O_CLOEXEC | O_NONBLOCK) != 0) {
        close(outpipe[0]); close(outpipe[1]);
        throw std::runtime_error("cannot create process pipe");
    }
    pid_t child = fork();
    if (child < 0) {
        close(outpipe[0]); close(outpipe[1]); close(errpipe[0]); close(errpipe[1]);
        throw std::runtime_error("cannot fork backend process");
    }
    if (child == 0) {
        setpgid(0, 0);
        close(outpipe[0]); close(errpipe[0]);
        if (dup2(outpipe[1], STDOUT_FILENO) < 0 || dup2(errpipe[1], STDERR_FILENO) < 0) _exit(126);
        close(outpipe[1]); close(errpipe[1]);
        std::vector<char*> argv;
        argv.reserve(arguments.size() + 1);
        for (const auto& arg : arguments) argv.push_back(const_cast<char*>(arg.c_str()));
        argv.push_back(nullptr);
        execvp(argv[0], argv.data());
        _exit(errno == ENOENT ? 127 : 126);
    }
    close(outpipe[1]); close(errpipe[1]);
    ProcessResult result;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout_seconds);
    bool exited = false, out_eof = false, err_eof = false;
    int wait_status = 0;
    while (!exited || !out_eof || !err_eof) {
        auto drain = [&](int fd, std::string& destination, bool& eof) {
            char buffer[4096];
            ssize_t count;
            while ((count = read(fd, buffer, sizeof(buffer))) > 0) {
                if (destination.size() + static_cast<size_t>(count) > max_output_bytes) result.truncated = true;
                else destination.append(buffer, static_cast<size_t>(count));
            }
            if (count == 0) eof = true;
        };
        if (!out_eof) drain(outpipe[0], result.output, out_eof);
        if (!err_eof) drain(errpipe[0], result.error, err_eof);
        if (!exited) {
            pid_t waited = waitpid(child, &wait_status, WNOHANG);
            if (waited == child) exited = true;
            else if (waited < 0 && errno != EINTR) { close(outpipe[0]); close(errpipe[0]); throw std::runtime_error("cannot wait for backend process"); }
        }
        if (exited && out_eof && err_eof) break;
        auto now = std::chrono::steady_clock::now();
        if (now >= deadline || result.truncated) {
            result.timed_out = now >= deadline;
            kill(-child, SIGKILL);
            kill(child, SIGKILL);
            if (!exited) waitpid(child, &wait_status, 0);
            break;
        }
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
        pollfd pfds[2]{{outpipe[0], POLLIN, 0}, {errpipe[0], POLLIN, 0}};
        poll(pfds, 2, static_cast<int>(ms > 50 ? 50 : ms));
    }
    close(outpipe[0]); close(errpipe[0]);
    if (!result.timed_out && WIFEXITED(wait_status)) result.status = WEXITSTATUS(wait_status);
    return result;
}
}
