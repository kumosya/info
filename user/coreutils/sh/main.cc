#include <dirent.h>
#include <fcntl.h>
#include <glob.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "os/abi.h"
#include "os/task.h"
#include "os/pty.h"
#include "os/terminal.h"
#include "os/users.h"
#include "../session-environment.h"

extern "C" char** environ;

namespace {

constexpr uint32_t lineCapacity = 1024;
constexpr uint32_t tokenCapacity = 128;
constexpr uint32_t tokenTextCapacity = 384;
constexpr uint32_t argumentCapacity = 64;
constexpr uint32_t commandCapacity = 4;
constexpr uint32_t pipelineCapacity = 8;
constexpr uint32_t jobCapacity = 8;
constexpr uint32_t historyCapacity = 16;
constexpr uint32_t outputCapacity = 4096;

enum class TokenType : uint8_t {
    Word,
    Pipe,
    RedirectInput,
    RedirectOutput,
    RedirectAppend,
    Semicolon,
    Background,
    End
};

struct Token {
    TokenType type;
    char text[tokenTextCapacity];
    bool quoted[tokenTextCapacity];
};

class Environment {
   public:
    bool init(bool login = true) {
        os::UserProfile profile{};
        os::UserCredentials request{}; request.id = geteuid();
        const bool identified = os::UserClient::request(os::UserOperation::Lookup, request, profile) == 0;
        if (login && (!identified || !coreutils::SessionEnvironment::apply(profile, false, false))) return false;
        set("USER", identified ? profile.name : geteuid() == 0 ? "root" : "user");
        if (!getenv("HOME")) set("HOME", identified ? profile.home : geteuid() == 0 ? "/root" : "/home/user");
        char hostname[64] = "osinfo";
        const int host = open("/etc/hostname", O_RDONLY);
        if (host >= 0) {
            const ssize_t count = read(host, hostname, sizeof(hostname) - 1);
            close(host);
            if (count > 0) { hostname[count] = 0; for (char* p = hostname; *p; ++p) if (*p == '\n' || *p == '\r') { *p = 0; break; } }
        }
        set("HOSTNAME", hostname);
        set("PWD", "/");
        set("OLDPWD", "/");
        set("SHELL", identified ? profile.shell : "/bin/sh");
        if (!getenv("TERM")) set("TERM", "osinfo");
        return true;
    }

    const char* get(const char* name) const {
        const char* inherited=getenv(name);
        return inherited ? inherited : "";
    }

    bool set(const char* name, const char* value) {
        if (name == nullptr || *name == '\0' || value == nullptr) {
            return false;
        }
        return setenv(name, value, 1) == 0;
    }

    bool unset(const char* name) {
        return unsetenv(name) == 0;
    }
};

class TextBuffer {
   public:
    void clear() {
        size_ = 0;
        data_[0] = '\0';
    }

    bool append(char value) {
        if (size_ + 1 >= sizeof(data_)) {
            return false;
        }
        data_[size_++] = value;
        data_[size_] = '\0';
        return true;
    }

    bool append(const char* value) {
        while (value != nullptr && *value != '\0') {
            if (!append(*value++)) {
                return false;
            }
        }
        return true;
    }

    bool appendUnsigned(uint64_t value) {
        char digits[21];
        uint32_t count = 0;
        do {
            digits[count++] = static_cast<char>('0' + value % 10);
            value /= 10;
        } while (value != 0);
        while (count != 0) {
            if (!append(digits[--count])) {
                return false;
            }
        }
        return true;
    }

    const char* data() const { return data_; }
    uint32_t size() const { return size_; }

   private:
    char data_[outputCapacity]{};
    uint32_t size_ = 0;
};

class Tokenizer {
   public:
    bool tokenize(const char* line, const Environment& environment,
                  int lastStatus, Token* tokens, uint32_t& tokenCount) const {
        tokenCount = 0;
        uint32_t position = 0;
        while (line[position] != '\0') {
            while (line[position] == ' ' || line[position] == '\t') {
                ++position;
            }
            if (line[position] == '\0') {
                break;
            }
            if (tokenCount + 1 >= tokenCapacity) {
                return false;
            }
            Token& token = tokens[tokenCount];
            token = {};
            if (readOperator(line, position, token)) {
                ++tokenCount;
                continue;
            }
            token.type = TokenType::Word;
            uint32_t length = 0;
            bool singleQuoted = false;
            bool doubleQuoted = false;
            bool explicitQuoted = false;
            if (line[position] == '~') {
                uint32_t end = position + 1;
                while (line[end] && line[end] != '/' && line[end] != ' ' && line[end] != '\t' && !isOperator(line[end])) ++end;
                char home[96]{};
                if (end == position + 1) os::Runtime::copyString(home, environment.get("HOME"), sizeof(home));
                else if (end-position-1 < 32) {
                    os::UserCredentials credentials{}; os::UserProfile profile{};
                    for (uint32_t i=position+1; i<end; ++i) credentials.name[i-position-1] = line[i];
                    if (!os::UserClient::request(os::UserOperation::Lookup, credentials, profile)) os::Runtime::copyString(home, profile.home, sizeof(home));
                }
                if (*home) {
                    for (const char* p=home; *p; ++p) { token.quoted[length] = true; if (!append(token.text,length,*p)) return false; }
                    position = end;
                }
            }
            while (line[position] != '\0') {
                const char value = line[position];
                if (!singleQuoted && !doubleQuoted &&
                    (value == ' ' || value == '\t' || isOperator(value))) {
                    break;
                }
                if (!doubleQuoted && value == '\'') {
                    explicitQuoted = true;
                    singleQuoted = !singleQuoted;
                    ++position;
                    continue;
                }
                if (!singleQuoted && value == '"') {
                    explicitQuoted = true;
                    doubleQuoted = !doubleQuoted;
                    ++position;
                    continue;
                }
                if (!singleQuoted && value == '\\') {
                    ++position;
                    if (line[position] == '\0') {
                        return false;
                    }
                    if (doubleQuoted && line[position] != '$' && line[position] != '`' &&
                        line[position] != '"' && line[position] != '\\') {
                        token.quoted[length] = true;
                        if (!append(token.text,length,'\\')) return false;
                        continue;
                    }
                    const char escaped = line[position++];
                    token.quoted[length] = true;
                    if (!append(token.text, length, escaped)) {
                        return false;
                    }
                    continue;
                }
                if (!singleQuoted && value == '$') {
                    const uint32_t start = length;
                    if (!expandVariable(line, position, environment,
                                        lastStatus, token.text, length)) {
                        return false;
                    }
                    if (doubleQuoted) for (uint32_t i=start; i<length; ++i) token.quoted[i] = true;
                    continue;
                }
                token.quoted[length] = singleQuoted || doubleQuoted;
                if (!append(token.text, length, value)) {
                    return false;
                }
                ++position;
            }
            if (singleQuoted || doubleQuoted || (length == 0 && !explicitQuoted)) {
                return false;
            }
            token.text[length] = '\0';
            ++tokenCount;
        }
        tokens[tokenCount++] = {TokenType::End, {}, {}};
        return true;
    }

   private:
    static bool isOperator(char value) {
        return value == '|' || value == '<' || value == '>' || value == ';' ||
               value == '&';
    }

    static bool readOperator(const char* line, uint32_t& position,
                             Token& token) {
        if (line[position] == '|') {
            token.type = TokenType::Pipe;
        } else if (line[position] == '<') {
            token.type = TokenType::RedirectInput;
        } else if (line[position] == '>') {
            if (line[position + 1] == '>') {
                token.type = TokenType::RedirectAppend;
                ++position;
            } else {
                token.type = TokenType::RedirectOutput;
            }
        } else if (line[position] == ';') {
            token.type = TokenType::Semicolon;
        } else if (line[position] == '&') {
            token.type = TokenType::Background;
        } else {
            return false;
        }
        ++position;
        return true;
    }

    static bool append(char* text, uint32_t& length, char value) {
        if (length + 1 >= tokenTextCapacity) {
            return false;
        }
        text[length++] = value;
        return true;
    }

    static bool append(char* text, uint32_t& length, const char* value) {
        while (value != nullptr && *value != '\0') {
            if (!append(text, length, *value++)) {
                return false;
            }
        }
        return true;
    }

    static bool expandVariable(const char* line, uint32_t& position,
                               const Environment& environment, int lastStatus,
                               char* text, uint32_t& length) {
        ++position;
        if (line[position] == '?') {
            ++position;
            char digits[12];
            uint32_t count = 0;
            uint32_t value = static_cast<uint32_t>(lastStatus);
            do {
                digits[count++] = static_cast<char>('0' + value % 10);
                value /= 10;
            } while (value != 0);
            while (count != 0) {
                if (!append(text, length, digits[--count])) {
                    return false;
                }
            }
            return true;
        }
        char name[24]{};
        uint32_t nameLength = 0;
        const bool braced = line[position] == '{';
        if (braced) {
            ++position;
        }
        while ((line[position] >= 'A' && line[position] <= 'Z') ||
               (line[position] >= 'a' && line[position] <= 'z') ||
               (line[position] >= '0' && line[position] <= '9') ||
               line[position] == '_') {
            if (nameLength + 1 >= sizeof(name)) {
                return false;
            }
            name[nameLength++] = line[position++];
        }
        if (braced) {
            if (line[position] != '}') {
                return false;
            }
            ++position;
        }
        if (nameLength == 0) {
            return append(text, length, '$');
        }
        name[nameLength] = '\0';
        return append(text, length, environment.get(name));
    }
};

struct Command {
    char arguments[argumentCapacity][tokenTextCapacity];
    uint32_t argumentCount;
    char inputPath[tokenTextCapacity];
    char outputPath[tokenTextCapacity];
    bool appendOutput;
};

struct Pipeline {
    Command commands[commandCapacity];
    uint32_t commandCount;
    bool background;
};

struct CommandLine {
    Pipeline pipelines[pipelineCapacity];
    uint32_t pipelineCount;
};

class Parser {
   public:
    bool parse(const Token* tokens, CommandLine& line) const {
        line = {};
        uint32_t tokenIndex = 0;
        while (tokens[tokenIndex].type != TokenType::End) {
            if (line.pipelineCount == pipelineCapacity) {
                return false;
            }
            Pipeline& pipeline = line.pipelines[line.pipelineCount++];
            if (!parsePipeline(tokens, tokenIndex, pipeline)) {
                return false;
            }
            if (tokens[tokenIndex].type == TokenType::Semicolon) {
                ++tokenIndex;
            }
        }
        return line.pipelineCount != 0;
    }

   private:
    static bool expandWord(const Token& token, Command& command, char* redirect = nullptr) {
        char pattern[tokenTextCapacity*2]; size_t length = 0; bool wildcard = false;
        for (size_t i=0; token.text[i]; ++i) {
            const char c = token.text[i];
            if (token.quoted[i] && (c=='*' || c=='?' || c=='[' || c==']' || c=='\\')) pattern[length++] = '\\';
            if (!token.quoted[i] && (c=='*' || c=='?' || c=='[')) wildcard = true;
            pattern[length++] = c;
        }
        pattern[length] = 0;
        glob_t matches{};
        int result = wildcard ? glob(pattern, 0, nullptr, &matches) : GLOB_NOMATCH;
        if (result && result != GLOB_NOMATCH) { globfree(&matches); return false; }
        const size_t count = result == GLOB_NOMATCH ? 1 : matches.gl_pathc;
        if ((redirect && count != 1) || (!redirect && command.argumentCount + count > argumentCapacity)) { globfree(&matches); return false; }
        for (size_t i=0; i<count; ++i) {
            const char* value = result == GLOB_NOMATCH ? token.text : matches.gl_pathv[i];
            if (strlen(value) >= tokenTextCapacity) { globfree(&matches); return false; }
            os::Runtime::copyString(redirect ? redirect : command.arguments[command.argumentCount++], value, tokenTextCapacity);
        }
        globfree(&matches); return true;
    }
    static bool parsePipeline(const Token* tokens, uint32_t& tokenIndex,
                              Pipeline& pipeline) {
        while (tokens[tokenIndex].type != TokenType::End &&
               tokens[tokenIndex].type != TokenType::Semicolon &&
               tokens[tokenIndex].type != TokenType::Background) {
            if (pipeline.commandCount == commandCapacity) {
                return false;
            }
            Command& command = pipeline.commands[pipeline.commandCount++];
            if (!parseCommand(tokens, tokenIndex, command)) {
                return false;
            }
            if (tokens[tokenIndex].type == TokenType::Pipe) {
                ++tokenIndex;
                continue;
            }
            break;
        }
        if (tokens[tokenIndex].type == TokenType::Background) {
            pipeline.background = true;
            ++tokenIndex;
        }
        return pipeline.commandCount != 0;
    }

    static bool parseCommand(const Token* tokens, uint32_t& tokenIndex,
                             Command& command) {
        while (tokens[tokenIndex].type != TokenType::End &&
               tokens[tokenIndex].type != TokenType::Pipe &&
               tokens[tokenIndex].type != TokenType::Semicolon &&
               tokens[tokenIndex].type != TokenType::Background) {
            const Token& token = tokens[tokenIndex++];
            if (token.type == TokenType::Word) {
                if (!expandWord(token, command)) return false;
                continue;
            }
            if (tokens[tokenIndex].type != TokenType::Word) {
                return false;
            }
            if (token.type == TokenType::RedirectInput) {
                if (!expandWord(tokens[tokenIndex], command, command.inputPath)) return false;
            } else if (token.type == TokenType::RedirectOutput ||
                       token.type == TokenType::RedirectAppend) {
                if (!expandWord(tokens[tokenIndex], command, command.outputPath)) return false;
                command.appendOutput =
                    token.type == TokenType::RedirectAppend;
            } else {
                return false;
            }
            ++tokenIndex;
        }
        return command.argumentCount != 0;
    }
};

struct ShellJob {
    uint32_t jobId;
    pid_t task;
    pid_t processGroup;
    uint32_t remainingTasks;
    bool running;
    bool stopped;
    char command[tokenTextCapacity];
};

class Shell {
   public:
    bool init(bool login = true) {
        os::Runtime::clearMemory(jobs_, sizeof(jobs_));
        os::Runtime::clearMemory(history_, sizeof(history_));
        pipelineOutput_.clear();
        nextJobId_ = 1;
        nextHistory_ = 0;
        historyCount_ = 0;
        lastStatus_ = 0;
        builtinStatus_ = 0;
        running_ = true;
        if (!environment_.init(login)) return false;
        os::Runtime::copyString(currentDirectory_, "/", sizeof(currentDirectory_));
        if (!login) {
            if (!getcwd(currentDirectory_,sizeof(currentDirectory_))) return false;
            environment_.set("PWD",currentDirectory_);
            return true;
        }
        setForegroundTask(0);
        if (chdir("/") != 0) return false;
        FILE* profile = fopen("/etc/profile", "r");
        if (profile) {
            char line[lineCapacity];
            while (fgets(line, sizeof(line), profile)) {
                for (char* end = line; *end; ++end) if (*end == '\n' || *end == '\r') { *end = 0; break; }
                if (line[0] == '#' || line[0] == '\n') continue;
                uint32_t count = 0;
                if (tokenizer_.tokenize(line, environment_, 0, tokens_, count) &&
                    parser_.parse(tokens_, commandLine_)) execute(commandLine_);
            }
            fclose(profile);
        }
        changeDirectory(environment_.get("HOME"));
        return true;
    }

    int run() {
        puts("osinfo tty shell");
        while (running_) {
            reapJobs();
            char line[lineCapacity]{};
            if (!readLine(line) || *line == '\0') {
                continue;
            }
            addHistory(line);
            uint32_t tokenCount = 0;
            if (!tokenizer_.tokenize(line, environment_, lastStatus_, tokens_,
                                     tokenCount) ||
                !parser_.parse(tokens_, commandLine_)) {
                fputs("shell: syntax error\n", stderr);
                lastStatus_ = 2;
                continue;
            }
            execute(commandLine_);
        }
        return lastStatus_;
    }

    int runCommand(const char* command) {
        uint32_t count = 0;
        if (!tokenizer_.tokenize(command,environment_,lastStatus_,tokens_,count) ||
            !parser_.parse(tokens_,commandLine_)) return 2;
        execute(commandLine_);
        return lastStatus_;
    }

   private:
    bool readLine(char* line) {
        const bool root = geteuid() == 0;
        const char* directory = currentDirectory_;
        const char* home = environment_.get("HOME");
        size_t homeLength = strlen(home);
        while (homeLength > 1 && home[homeLength - 1] == '/') --homeLength;
        char displayedDirectory[tokenTextCapacity + 1];
        if (home[0] == '/' && homeLength > 1 &&
            strncmp(directory, home, homeLength) == 0 &&
            (directory[homeLength] == '\0' || directory[homeLength] == '/')) {
            snprintf(displayedDirectory, sizeof(displayedDirectory), "~%s",
                     directory + homeLength);
            directory = displayedDirectory;
        }
        printf("\033[%sm%s@%s\033[0m:\033[01;34m%s\033[0m%c ",
               root ? "01;31" : "01;32", environment_.get("USER"),
               environment_.get("HOSTNAME"), directory, root ? '#' : '$');
        fflush(stdout);
        if (fgets(line, lineCapacity, stdin) == nullptr) {
            return false;
        }
        const size_t length = strlen(line);
        if (length != 0 && line[length - 1] == '\n') {
            line[length - 1] = '\0';
        }
        return true;
    }

    void execute(const CommandLine& line) {
        for (uint32_t index = 0; index < line.pipelineCount && running_;
             ++index) {
            lastStatus_ = executePipeline(line.pipelines[index]);
        }
    }

    int executePipeline(const Pipeline& pipeline) {
        if (pipeline.commandCount > 1) {
            return executeExternalPipeline(pipeline);
        }
        const Command& command = pipeline.commands[0];
        pipelineOutput_.clear();
        const int status = executeCommand(
            command, pipelineOutput_, pipeline.background);
        if (pipelineOutput_.size() != 0) {
            fputs(pipelineOutput_.data(), stdout);
        }
        return status;
    }

    int executeExternalPipeline(const Pipeline& pipeline) {
        if (pipeline.background) {
            fputs("shell: background pipelines are unavailable\n", stderr);
            return 1;
        }
        int pipes[pipelineCapacity - 1][2]{};
        pid_t taskes[pipelineCapacity]{};
        char executables[pipelineCapacity][tokenTextCapacity]{};
        for (uint32_t index = 0; index + 1 < pipeline.commandCount; ++index) {
            pipes[index][0] = -1;
            pipes[index][1] = -1;
        }
        for (uint32_t index = 0; index < pipeline.commandCount; ++index) {
            const Command& command = pipeline.commands[index];
            if (!resolveExecutable(command.arguments[0], executables[index])) {
                fputs(command.arguments[0], stderr);
                fputs(": not found\n", stderr);
                return 127;
            }
        }
        for (uint32_t index = 0; index + 1 < pipeline.commandCount; ++index) {
            if (pipe(pipes[index]) != 0) {
                closePipelineHandles(pipes, pipeline.commandCount - 1);
                fputs("shell: unable to create pipe\n", stderr);
                return 1;
            }
        }
        bool launched = true;
        for (uint32_t index = 0; index < pipeline.commandCount; ++index) {
            const Command& command = pipeline.commands[index];
            const char* arguments[argumentCapacity + 1]{};
            for (uint32_t argument = 0; argument < command.argumentCount;
                 ++argument) {
                arguments[argument] = command.arguments[argument];
            }
            posix_spawn_file_actions_t actions{};
            const bool actionsInitialized =
                posix_spawn_file_actions_init(&actions) == 0;
            posix_spawnattr_t attributes{};
            const bool attributesInitialized =
                posix_spawnattr_init(&attributes) == 0;
            bool attributesReady =
                attributesInitialized &&
                posix_spawnattr_setpgroup(
                    &attributes, index == 0 ? 0 : taskes[0]) == 0 &&
                posix_spawnattr_setflags(&attributes,
                                         POSIX_SPAWN_SETPGROUP) == 0;
            bool actionsReady = actionsInitialized;
            if (actionsReady && index != 0) {
                actionsReady = posix_spawn_file_actions_adddup2(
                                   &actions, pipes[index - 1][0],
                                   STDIN_FILENO) == 0;
            }
            if (actionsReady && index + 1 != pipeline.commandCount) {
                actionsReady = posix_spawn_file_actions_adddup2(
                                   &actions, pipes[index][1],
                                   STDOUT_FILENO) == 0;
            }
            if (actionsReady) {
                actionsReady = addRedirections(actions, command);
            }
            for (uint32_t pipeIndex = 0;
                 actionsReady && pipeIndex + 1 < pipeline.commandCount;
                 ++pipeIndex) {
                actionsReady =
                    posix_spawn_file_actions_addclose(
                        &actions, pipes[pipeIndex][0]) == 0 &&
                    posix_spawn_file_actions_addclose(
                        &actions, pipes[pipeIndex][1]) == 0;
            }
            const int spawnResult =
                actionsReady && attributesReady
                    ? posix_spawn(&taskes[index], executables[index],
                                  &actions, &attributes,
                                  const_cast<char* const*>(arguments),
                                  environ)
                    : -1;
            if (actionsInitialized) {
                posix_spawn_file_actions_destroy(&actions);
            }
            if (attributesInitialized) {
                posix_spawnattr_destroy(&attributes);
            }
            if (index != 0 && pipes[index - 1][0] >= 0) {
                close(pipes[index - 1][0]);
                pipes[index - 1][0] = -1;
            }
            if (index + 1 != pipeline.commandCount && pipes[index][1] >= 0) {
                close(pipes[index][1]);
                pipes[index][1] = -1;
            }
            if (spawnResult != 0) {
                taskes[index] = 0;
                launched = false;
                break;
            }
        }
        closePipelineHandles(pipes, pipeline.commandCount - 1);
        int status = launched ? 0 : 126;
        if (!launched && taskes[0] != 0) {
            kill(-taskes[0], SIGTERM);
        }
        if (launched && pipeline.commandCount != 0) {
            setForegroundTask(taskes[0]);
            bool stopped = false;
            uint32_t remaining = pipeline.commandCount;
            for (uint32_t completed = 0; completed < pipeline.commandCount;
                 ++completed) {
                int waitStatus = 0;
                const pid_t result =
                    waitpid(-taskes[0], &waitStatus, WUNTRACED);
                if (result < 0) {
                    status = 1;
                    break;
                }
                if (WIFSTOPPED(waitStatus)) {
                    stopped = true;
                    break;
                }
                if (WIFEXITED(waitStatus) || WIFSIGNALED(waitStatus)) {
                    --remaining;
                    if (result == taskes[pipeline.commandCount - 1]) {
                        status = WIFEXITED(waitStatus)
                                     ? WEXITSTATUS(waitStatus)
                                     : 128 + WTERMSIG(waitStatus);
                    }
                }
            }
            if (stopped &&
                !addPipelineJob(taskes[0],
                                taskes[pipeline.commandCount - 1], remaining,
                                pipeline)) {
                status = 1;
            } else if (stopped) {
                status = 128 + SIGTSTP;
            }
        }
        if (!launched) {
            for (uint32_t index = 0; index < pipeline.commandCount; ++index) {
                if (taskes[index] == 0) continue;
                int waitStatus = 0;
                if (waitpid(taskes[index], &waitStatus, 0) < 0) {
                    status = 1;
                }
            }
        }
        setForegroundTask(0);
        if (!launched) {
            fputs("shell: task creation failed\n", stderr);
        }
        return status;
    }

    static void closePipelineHandles(int pipes[][2],
                                     uint32_t pipeCount) {
        for (uint32_t index = 0; index < pipeCount; ++index) {
            if (pipes[index][0] >= 0) {
                close(pipes[index][0]);
                pipes[index][0] = -1;
            }
            if (pipes[index][1] >= 0) {
                close(pipes[index][1]);
                pipes[index][1] = -1;
            }
        }
    }

    int executeCommand(const Command& command, TextBuffer& output,
                       bool background) {
        if (!background && executeBuiltin(command, output)) {
            return builtinStatus_;
        }
        char executable[tokenTextCapacity]{};
        if (!resolveExecutable(command.arguments[0], executable)) {
            output.append(command.arguments[0]);
            output.append(": not found\n");
            return 127;
        }
        const char* arguments[argumentCapacity + 1]{};
        for (uint32_t index = 0; index < command.argumentCount; ++index) {
            arguments[index] = command.arguments[index];
        }
        posix_spawn_file_actions_t actions{};
        const bool hasRedirection = command.inputPath[0] != '\0' ||
                                    command.outputPath[0] != '\0';
        bool actionsInitialized = false;
        if (hasRedirection) {
            actionsInitialized = posix_spawn_file_actions_init(&actions) == 0;
            if (!actionsInitialized || !addRedirections(actions, command)) {
                if (actionsInitialized) {
                    posix_spawn_file_actions_destroy(&actions);
                }
                output.append("shell: unable to configure redirection\n");
                return 1;
            }
        }
        posix_spawnattr_t attributes{};
        const bool attributesInitialized =
            posix_spawnattr_init(&attributes) == 0;
        const bool attributesReady =
            attributesInitialized &&
            posix_spawnattr_setpgroup(&attributes, 0) == 0 &&
            posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP) == 0;
        if (!attributesReady) {
            if (actionsInitialized) {
                posix_spawn_file_actions_destroy(&actions);
            }
            if (attributesInitialized) {
                posix_spawnattr_destroy(&attributes);
            }
            output.append("shell: unable to configure process group\n");
            return 1;
        }
        pid_t task = 0;
        const int spawnResult = posix_spawn(
            &task, executable, hasRedirection ? &actions : nullptr,
            &attributes,
            const_cast<char* const*>(arguments), environ);
        if (actionsInitialized) {
            posix_spawn_file_actions_destroy(&actions);
        }
        posix_spawnattr_destroy(&attributes);
        if (spawnResult != 0) {
            output.append("shell: task creation failed\n");
            return 126;
        }
        if (background) {
            return addJob(task, command.arguments[0]) ? 0 : 1;
        }
        setForegroundTask(task);
        int waitStatus = 0;
        const pid_t result = waitpid(task, &waitStatus, WUNTRACED);
        setForegroundTask(0);
        if (result == task && WIFSTOPPED(waitStatus)) {
            return addJob(task, command.arguments[0], true) ? 128 + SIGTSTP
                                                               : 1;
        }
        return result == task
                   ? (WIFEXITED(waitStatus) ? WEXITSTATUS(waitStatus)
                                            : 128 + WTERMSIG(waitStatus))
                   : 1;
    }

    bool executeBuiltin(const Command& command, TextBuffer& output) {
        builtinStatus_ = 0;
        const char* name = command.arguments[0];
        if (os::Runtime::stringsEqual(name, "cd")) {
            builtinStatus_ = changeDirectory(
                command.argumentCount > 1 ? command.arguments[1]
                                          : environment_.get("HOME"));
        } else if (os::Runtime::stringsEqual(name, "pwd")) {
            output.append(currentDirectory_);
            output.append("\n");
        } else if (os::Runtime::stringsEqual(name, "export")) {
            builtinStatus_ = exportVariable(command);
        } else if (os::Runtime::stringsEqual(name, "unset")) {
            builtinStatus_ = command.argumentCount == 2 &&
                                     environment_.unset(command.arguments[1])
                                 ? 0
                                 : 1;
        } else if (os::Runtime::stringsEqual(name, "set")) {
            printEnvironment(output);
        } else if (os::Runtime::stringsEqual(name, "exit") || os::Runtime::stringsEqual(name, "logout")) {
            running_ = false;
            if (command.argumentCount > 1) {
                builtinStatus_ = static_cast<int>(parseUnsigned(
                    command.arguments[1], static_cast<uint64_t>(lastStatus_)));
            }
        } else if (os::Runtime::stringsEqual(name, "history")) {
            printHistory(output);
        } else if (os::Runtime::stringsEqual(name, "jobs")) {
            reapJobs();
            printJobs(output);
        } else if (os::Runtime::stringsEqual(name, "fg")) {
            builtinStatus_ = foregroundJob(command);
        } else if (os::Runtime::stringsEqual(name, "bg")) {
            builtinStatus_ = backgroundJob(command, output);
        } else if (os::Runtime::stringsEqual(name, "clear")) {
            output.append("\033[2J\033[H");
        } else {
            return false;
        }
        if (command.outputPath[0] != '\0') {
            if (!writePath(command.outputPath, output, command.appendOutput)) {
                output.clear();
                fputs("shell: unable to redirect output\n", stderr);
                builtinStatus_ = 1;
            } else {
                output.clear();
            }
        }
        return true;
    }

    static bool addRedirections(posix_spawn_file_actions_t& actions,
                                const Command& command) {
        if (command.inputPath[0] != '\0' &&
            posix_spawn_file_actions_addopen(
                &actions, STDIN_FILENO, command.inputPath, O_RDONLY, 0) != 0) {
            return false;
        }
        if (command.outputPath[0] != '\0') {
            const int flags =
                O_WRONLY | O_CREAT |
                (command.appendOutput ? O_APPEND : O_TRUNC);
            if (posix_spawn_file_actions_addopen(
                    &actions, STDOUT_FILENO, command.outputPath, flags,
                    0644) != 0) {
                return false;
            }
        }
        return true;
    }

    int changeDirectory(const char* path) {
        char absolute[tokenTextCapacity]{};
        resolvePath(path, absolute);
        if (chdir(absolute) != 0) {
            fputs("cd: no such directory\n", stderr);
            return 1;
        }
        environment_.set("OLDPWD", currentDirectory_);
        if (getcwd(currentDirectory_, sizeof(currentDirectory_)) == nullptr) {
            return 1;
        }
        environment_.set("PWD", currentDirectory_);
        return 0;
    }

    int exportVariable(const Command& command) {
        if (command.argumentCount != 2) {
            return 1;
        }
        const char* assignment = command.arguments[1];
        uint32_t separator = 0;
        while (assignment[separator] != '\0' &&
               assignment[separator] != '=') {
            ++separator;
        }
        if (assignment[separator] != '=' || separator == 0 || separator >= 23) {
            return 1;
        }
        char name[24]{};
        for (uint32_t index = 0; index < separator; ++index) {
            name[index] = assignment[index];
        }
        return environment_.set(name, assignment + separator + 1) ? 0 : 1;
    }

    void printEnvironment(TextBuffer& output) const {
        for (char** entry = environ; entry && *entry; ++entry) {
            output.append(*entry);
            output.append("\n");
        }
    }

    static void echo(const Command& command, TextBuffer& output) {
        for (uint32_t index = 1; index < command.argumentCount; ++index) {
            if (index != 1) {
                output.append(" ");
            }
            output.append(command.arguments[index]);
        }
        output.append("\n");
    }

    int cat(const Command& command, const TextBuffer& input,
            TextBuffer& output) {
        if (command.argumentCount == 1) {
            output.append(input.data());
            return 0;
        }
        for (uint32_t index = 1; index < command.argumentCount; ++index) {
            TextBuffer file;
            file.clear();
            if (!readPath(command.arguments[index], file)) {
                output.append("cat: no such file\n");
                return 1;
            }
            output.append(file.data());
        }
        return 0;
    }

    int list(const Command& command, TextBuffer& output) {
        char path[tokenTextCapacity]{};
        resolvePath(command.argumentCount > 1 ? command.arguments[1] : ".",
                    path);
        DIR* directory = opendir(path);
        if (directory == nullptr) {
            return 1;
        }
        while (dirent* entry = readdir(directory)) {
            output.append(entry->d_name);
            output.append("\n");
        }
        closedir(directory);
        return 0;
    }

    int mutatePath(const Command& command, bool directory, bool remove,
                   TextBuffer& output) {
        if (command.argumentCount != 2) {
            output.append("shell: expected one path\n");
            return 1;
        }
        char path[tokenTextCapacity]{};
        resolvePath(command.arguments[1], path);
        if (remove) {
            struct stat status {};
            if (stat(path, &status) != 0 ||
                (directory && !S_ISDIR(status.st_mode)) ||
                (!directory && S_ISDIR(status.st_mode))) {
                output.append(directory ? "rmdir: invalid directory\n"
                                        : "rm: invalid file\n");
                return 1;
            }
            return (directory ? rmdir(path) : unlink(path)) == 0 ? 0 : 1;
        }
        struct stat status {};
        if (stat(path, &status) == 0) {
            return directory ? 1 : 0;
        }
        if (directory) {
            return mkdir(path, 0755) == 0 ? 0 : 1;
        }
        const int descriptor = open(path, O_WRONLY | O_CREAT, 0644);
        if (descriptor < 0) {
            return 1;
        }
        close(descriptor);
        return 0;
    }

    int copyPath(const Command& command, bool removeSource,
                 TextBuffer& output) {
        if (command.argumentCount != 3) {
            output.append(removeSource ? "mv: expected source and target\n"
                                       : "cp: expected source and target\n");
            return 1;
        }
        char source[tokenTextCapacity]{};
        char target[tokenTextCapacity]{};
        resolvePath(command.arguments[1], source);
        resolvePath(command.arguments[2], target);
        if (os::Runtime::stringsEqual(source, target)) {
            if (removeSource) {
                return 0;
            }
            output.append("cp: source and target are the same file\n");
            return 1;
        }
        struct stat sourceStatus {};
        if (stat(source, &sourceStatus) != 0 ||
            S_ISDIR(sourceStatus.st_mode)) {
            output.append(removeSource ? "mv: invalid source file\n"
                                       : "cp: invalid source file\n");
            return 1;
        }
        const int input = open(source, O_RDONLY);
        const int outputFile = open(target, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (input < 0 || outputFile < 0) {
            if (input >= 0) close(input);
            if (outputFile >= 0) close(outputFile);
            output.append(removeSource ? "mv: cannot open path\n"
                                       : "cp: cannot open path\n");
            return 1;
        }
        bool success = true;
        char buffer[256];
        while (true) {
            const ssize_t count = read(input, buffer, sizeof(buffer));
            if (count < 0) {
                success = false;
                break;
            }
            if (count == 0) {
                break;
            }
            if (write(outputFile, buffer, static_cast<size_t>(count)) !=
                count) {
                success = false;
                break;
            }
        }
        close(input);
        close(outputFile);
        if (!success || (removeSource && unlink(source) != 0)) {
            output.append(removeSource ? "mv: operation failed\n"
                                       : "cp: operation failed\n");
            return 1;
        }
        return 0;
    }

    int sleepCommand(const Command& command) {
        if (command.argumentCount != 2) {
            return 1;
        }
        const uint64_t seconds = parseUnsigned(command.arguments[1], 0);
        if (seconds == 0 || seconds > 3600) {
            return 1;
        }
        sleep(static_cast<unsigned int>(seconds));
        return 0;
    }

    bool readPath(const char* requestedPath, TextBuffer& output) const {
        char path[tokenTextCapacity]{};
        resolvePath(requestedPath, path);
        const int descriptor = open(path, O_RDONLY);
        if (descriptor < 0) {
            return false;
        }
        char chunk[64];
        while (true) {
            const ssize_t count = read(descriptor, chunk, sizeof(chunk));
            if (count < 0) {
                close(descriptor);
                return false;
            }
            for (ssize_t index = 0; index < count; ++index) {
                if (!output.append(chunk[index])) {
                    close(descriptor);
                    return false;
                }
            }
            if (count == 0) {
                break;
            }
        }
        close(descriptor);
        return true;
    }

    bool writePath(const char* requestedPath, const TextBuffer& output,
                   bool append) const {
        char path[tokenTextCapacity]{};
        resolvePath(requestedPath, path);
        const int flags = O_WRONLY | O_CREAT |
                          (append ? O_APPEND : O_TRUNC);
        const int descriptor = open(path, flags, 0644);
        if (descriptor < 0) {
            return false;
        }
        const bool result = write(descriptor, output.data(), output.size()) ==
                            static_cast<ssize_t>(output.size());
        close(descriptor);
        return result;
    }

    void resolvePath(const char* path, char* result) const {
        if (path == nullptr || *path == '\0' || os::Runtime::stringsEqual(path, ".")) {
            os::Runtime::copyString(result, currentDirectory_, tokenTextCapacity);
            return;
        }
        if (path[0] == '/') {
            os::Runtime::copyString(result, path, tokenTextCapacity);
            return;
        }
        os::Runtime::copyString(result, currentDirectory_, tokenTextCapacity);
        uint32_t length = static_cast<uint32_t>(os::Runtime::stringLength(result));
        if (length != 1 && length + 1 < tokenTextCapacity) {
            result[length++] = '/';
            result[length] = '\0';
        }
        if (path[0] == '.' && path[1] == '/') {
            path += 2;
        }
        os::Runtime::copyString(result + length, path, tokenTextCapacity - length);
    }

    bool resolveExecutable(const char* command, char* result) const {
        if (command == nullptr || *command == '\0') {
            return false;
        }
        if (command[0] == '/' || command[0] == '.') {
            resolvePath(command, result);
            return executableExists(result);
        }
        const char* searchPath = environment_.get("PATH");
        uint32_t position = 0;
        while (searchPath[position] != '\0') {
            uint32_t length = 0;
            while (searchPath[position] != '\0' &&
                   searchPath[position] != ':' &&
                   length + 1 < tokenTextCapacity) {
                result[length++] = searchPath[position++];
            }
            if (length != 1) {
                result[length++] = '/';
            }
            uint32_t commandIndex = 0;
            while (command[commandIndex] != '\0' &&
                   length + 1 < tokenTextCapacity) {
                result[length++] = command[commandIndex++];
            }
            result[length] = '\0';
            if (executableExists(result)) {
                return true;
            }
            if (searchPath[position] == ':') {
                ++position;
            }
        }
        return false;
    }

    bool executableExists(const char* path) const {
        struct stat status {};
        return stat(path, &status) == 0 && S_ISREG(status.st_mode);
    }

    void setForegroundTask(uint64_t taskId) const {
        const uint64_t terminalHandle = os::getStandardHandle(1);
        if (terminalHandle != 0) {
            os::Pty::setPtyForegroundGroup(terminalHandle, taskId);
        } else {
            os::Terminal::setForegroundTask(taskId);
        }
    }

    bool addJob(pid_t task, const char* command,
                bool stopped = false) {
        for (ShellJob& job : jobs_) {
            if (!job.running) {
                job.jobId = nextJobId_++;
                job.task = task;
                job.processGroup = task;
                job.remainingTasks = 1;
                job.running = true;
                job.stopped = stopped;
                os::Runtime::copyString(job.command, command, sizeof(job.command));
                TextBuffer output;
                output.clear();
                output.append("[");
                output.appendUnsigned(job.jobId);
                output.append(stopped ? "] Stopped\n" : "] started\n");
                fputs(output.data(), stdout);
                return true;
            }
        }
        kill(task, SIGTERM);
        return false;
    }

    bool addPipelineJob(pid_t processGroup, pid_t lastTask,
                        uint32_t remainingTasks, const Pipeline& pipeline) {
        for (ShellJob& job : jobs_) {
            if (!job.running) {
                job.jobId = nextJobId_++;
                job.task = lastTask;
                job.processGroup = processGroup;
                job.remainingTasks = remainingTasks;
                job.running = true;
                job.stopped = true;
                TextBuffer description;
                description.clear();
                for (uint32_t index = 0; index < pipeline.commandCount;
                     ++index) {
                    if (index != 0) description.append(" | ");
                    description.append(
                        pipeline.commands[index].arguments[0]);
                }
                os::Runtime::copyString(job.command, description.data(),
                               sizeof(job.command));
                TextBuffer output;
                output.clear();
                output.append("[");
                output.appendUnsigned(job.jobId);
                output.append("] Stopped\n");
                fputs(output.data(), stdout);
                return true;
            }
        }
        kill(-processGroup, SIGTERM);
        return false;
    }

    void reapJobs() {
        for (ShellJob& job : jobs_) {
            if (!job.running) {
                continue;
            }
            while (true) {
                int waitStatus = 0;
                const pid_t result =
                    waitpid(-job.processGroup, &waitStatus,
                            WNOHANG | WUNTRACED | WCONTINUED);
                if (result <= 0) break;
                if (WIFEXITED(waitStatus) || WIFSIGNALED(waitStatus)) {
                    if (job.remainingTasks != 0) --job.remainingTasks;
                } else if (WIFSTOPPED(waitStatus)) {
                    job.stopped = true;
                } else if (WIFCONTINUED(waitStatus)) {
                    job.stopped = false;
                }
            }
            if (job.remainingTasks == 0) {
                job.running = false;
                job.stopped = false;
            }
        }
    }

    void printJobs(TextBuffer& output) const {
        for (const ShellJob& job : jobs_) {
            if (job.running) {
                output.append("[");
                output.appendUnsigned(job.jobId);
                output.append(job.stopped ? "] Stopped " : "] Running ");
                output.append(job.command);
                output.append("\n");
            }
        }
    }

    int foregroundJob(const Command& command) {
        if (command.argumentCount != 2) {
            return 1;
        }
        const uint64_t jobId = parseUnsigned(command.arguments[1], 0);
        for (ShellJob& job : jobs_) {
            if (job.running && job.jobId == jobId) {
                setForegroundTask(job.processGroup);
                if (job.stopped) {
                    kill(-job.processGroup, SIGCONT);
                    job.stopped = false;
                }
                int status = 0;
                while (job.remainingTasks != 0) {
                    int waitStatus = 0;
                    const pid_t result = waitpid(
                        -job.processGroup, &waitStatus, WUNTRACED);
                    if (result < 0) {
                        status = 1;
                        break;
                    }
                    if (WIFSTOPPED(waitStatus)) {
                        job.stopped = true;
                        setForegroundTask(0);
                        return 128 + SIGTSTP;
                    }
                    if (WIFEXITED(waitStatus) || WIFSIGNALED(waitStatus)) {
                        if (job.remainingTasks != 0) --job.remainingTasks;
                        if (result == job.task) {
                            status = WIFEXITED(waitStatus)
                                         ? WEXITSTATUS(waitStatus)
                                         : 128 + WTERMSIG(waitStatus);
                        }
                    }
                }
                setForegroundTask(0);
                job.running = job.remainingTasks != 0;
                return status;
            }
        }
        return 1;
    }

    int backgroundJob(const Command& command, TextBuffer& output) {
        if (command.argumentCount != 2) {
            output.append("bg: expected a job id\n");
            return 1;
        }
        const uint64_t jobId = parseUnsigned(command.arguments[1], 0);
        for (ShellJob& job : jobs_) {
            if (!job.running || job.jobId != jobId) continue;
            if (!job.stopped) {
                output.append("bg: job is already running\n");
                return 0;
            }
            if (kill(-job.processGroup, SIGCONT) != 0) {
                output.append("bg: unable to continue job\n");
                return 1;
            }
            job.stopped = false;
            output.append("[");
            output.appendUnsigned(job.jobId);
            output.append("] Running ");
            output.append(job.command);
            output.append("\n");
            return 0;
        }
        output.append("bg: job not found\n");
        return 1;
    }

    void addHistory(const char* line) {
        os::Runtime::copyString(history_[nextHistory_ % historyCapacity], line,
                       lineCapacity);
        ++nextHistory_;
        if (historyCount_ < historyCapacity) {
            ++historyCount_;
        }
    }

    void printHistory(TextBuffer& output) const {
        const uint32_t first = nextHistory_ - historyCount_;
        for (uint32_t index = 0; index < historyCount_; ++index) {
            output.appendUnsigned(index + 1);
            output.append(" ");
            output.append(history_[(first + index) % historyCapacity]);
            output.append("\n");
        }
    }

    static uint64_t parseUnsigned(const char* text, uint64_t fallback) {
        if (text == nullptr || *text == '\0') {
            return fallback;
        }
        uint64_t value = 0;
        while (*text != '\0') {
            if (*text < '0' || *text > '9' || value > 1000000) {
                return fallback;
            }
            value = value * 10 + static_cast<uint64_t>(*text++ - '0');
        }
        return value;
    }

    Environment environment_;
    Tokenizer tokenizer_;
    Parser parser_;
    Token tokens_[tokenCapacity]{};
    CommandLine commandLine_{};
    TextBuffer pipelineOutput_;
    ShellJob jobs_[jobCapacity]{};
    char history_[historyCapacity][lineCapacity]{};
    char currentDirectory_[tokenTextCapacity]{};
    uint32_t nextJobId_ = 1;
    uint32_t nextHistory_ = 0;
    uint32_t historyCount_ = 0;
    int lastStatus_ = 0;
    int builtinStatus_ = 0;
    bool running_ = true;
};

Shell shell;

}  // namespace

int main(int argumentCount, char** arguments) {
    if (argumentCount < 1 || arguments == nullptr ||
        arguments[0] == nullptr || arguments[0][0] == '\0') {
        os::System::log("[SHELL] invalid main(argc, argv) state\n");
        return 1;
    }
    const bool command = argumentCount >= 3 && os::Runtime::stringsEqual(arguments[1],"-c");
    if (!shell.init(!command)) {
        os::System::log("[SHELL] service discovery failed\n");
        return 2;
    }
    return command ? shell.runCommand(arguments[2]) : shell.run();
}
