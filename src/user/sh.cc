#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

extern "C" pid_t clone(int (*fn)(void *), void *stack, int flags, void *arg);

static char current_path[256] = "/";

static void resolve_path(const char *input, char *output, size_t size) {
    if (input[0] == '/') {
        strncpy(output, input, size - 1);
        output[size - 1] = '\0';
    } else {
        snprintf(output, size, "%s/%s", current_path, input);
    }
}

static int parse_args(char *buf, char **argv, int max_args) {
    int argc = 0;
    char *p = buf;
    
    while (*p && argc < max_args - 1) {
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0') break;
        
        if (*p == '"') {
            p++;
            argv[argc++] = p;
            while (*p && *p != '"') p++;
            if (*p == '"') *p++ = '\0';
        } else {
            argv[argc++] = p;
            while (*p && *p != ' ' && *p != '\t') p++;
            if (*p) *p++ = '\0';
        }
    }
    argv[argc] = nullptr;
    return argc;
}

static void cmd_ls(const char *path) {
    DIR *dir = opendir(path);
    if (!dir) {
        printf("ls: cannot open directory '%s'\n", path);
        return;
    }
    
    struct dirent *entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_name[0] == '.') continue;
        printf("%s  ", entry->d_name);
    }
    printf("\n");
    closedir(dir);
}

static void cmd_cd(const char *path) {
    if (!path || !path[0]) {
        strcpy(current_path, "/");
        return;
    }
    
    if (path[0] == '/') {
        if (chdir(path) == 0) {
            strncpy(current_path, path, 255);
        } else {
            printf("cd: %s: No such directory\n", path);
        }
    } else {
        char new_path[512];
        snprintf(new_path, sizeof(new_path), "%s/%s", current_path, path);
        if (chdir(new_path) == 0) {
            strncpy(current_path, new_path, 255);
        } else {
            printf("cd: %s: No such directory\n", path);
        }
    }
}

static void cmd_pwd() {
    printf("%s\n", current_path);
}

static void cmd_cat(const char *filename) {
    if (!filename) {
        printf("cat: missing file operand\n");
        return;
    }
    
    char abs_path[512];
    resolve_path(filename, abs_path, sizeof(abs_path));
    
    int fd = open(abs_path, O_RDONLY, 0);
    if (fd < 0) {
        printf("cat: %s: No such file\n", filename);
        return;
    }
    
    char buf[512];
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        printf("%s", buf);
    }
    printf("\n");
    close(fd);
}

static void cmd_cp(const char *src, const char *dst) {
    if (!src || !dst) {
        printf("cp: missing operand\n");
        return;
    }
    
    char abs_src[512], abs_dst[512];
    resolve_path(src, abs_src, sizeof(abs_src));
    resolve_path(dst, abs_dst, sizeof(abs_dst));
    
    int src_fd = open(abs_src, O_RDONLY, 0);
    if (src_fd < 0) {
        printf("cp: %s: No such file\n", src);
        return;
    }
    
    int dst_fd = open(abs_dst, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (dst_fd < 0) {
        printf("cp: cannot create '%s'\n", dst);
        close(src_fd);
        return;
    }
    
    char buf[512];
    ssize_t n;
    while ((n = read(src_fd, buf, sizeof(buf))) > 0) {
        write(dst_fd, buf, n);
    }
    
    close(src_fd);
    close(dst_fd);
}

static void cmd_mkdir(const char *path) {
    if (!path) {
        printf("mkdir: missing operand\n");
        return;
    }
    
    char abs_path[512];
    resolve_path(path, abs_path, sizeof(abs_path));
    
    if (mkdir(abs_path, 0755) == 0) {
    } else {
        printf("mkdir: cannot create directory '%s': File exists or error\n", path);
    }
}

static void cmd_rm(const char *path) {
    if (!path) {
        printf("rm: missing operand\n");
        return;
    }
    
    char abs_path[512];
    resolve_path(path, abs_path, sizeof(abs_path));
    
    if (unlink(abs_path) == 0) {
    } else {
        printf("rm: cannot remove '%s': No such file\n", path);
    }
}

static void cmd_touch(const char *path) {
    if (!path) {
        printf("touch: missing file operand\n");
        return;
    }
    
    char abs_path[512];
    resolve_path(path, abs_path, sizeof(abs_path));
    
    int fd = open(abs_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        printf("touch: cannot create file '%s'\n", path);
    } else {
        close(fd);
    }
}

static void cmd_echo(char **argv, int argc) {
    for (int i = 1; i < argc; i++) {
        if (i > 1) printf(" ");
        printf("%s", argv[i]);
    }
    printf("\n");
}

static void cmd_help() {
    printf("Built-in commands:\n");
    printf("  ls [path]     - list directory contents\n");
    printf("  cd <path>     - change directory\n");
    printf("  pwd           - print working directory\n");
    printf("  cat <file>    - display file contents\n");
    printf("  mkdir <name>  - create directory\n");
    printf("  rm <name>     - remove file\n");
    printf("  echo <text>   - print text\n");
    printf("  clear         - clear screen\n");
    printf("  help          - show this help\n");
    printf("  exit          - exit shell\n");
}

struct exec_args {
    char path[256];
    char *argv[16];
};

static int exec_child(void *arg) {
    exec_args *args = (exec_args *)arg;
    execv(args->path, args->argv);
    printf("execv failed for %s\n", args->path);
    return 1;
}

static int try_exec_external(char **argv, int argc) {
    char path_buf[256];
    const char *paths[] = {"/bin", "/usr/bin", nullptr};
    
    for (int i = 0; paths[i]; i++) {
        snprintf(path_buf, sizeof(path_buf), "%s/%s", paths[i], argv[0]);
        
        //if (access(path_buf, X_OK) == 0) {
            exec_args *args = new exec_args;
            strcpy(args->path, path_buf);
            for (int j = 0; j <= argc && j < 15; j++) {
                args->argv[j] = argv[j] ? strdup(argv[j]) : nullptr;
            }
            args->argv[argc] = nullptr;
            
            pid_t pid = clone(exec_child, nullptr, 0, args);
            if (pid > 0) {
                int status;
                waitpid(pid, &status, 0);
                for (int j = 0; args->argv[j]; j++) {
                    free(args->argv[j]);
                }
                delete args;
                return 0;
            }
            delete args;
        //}
    }
    
    return -1;
}

int main(int argc, char *argv[]) {
    char *buf = new char[256];
    char *cmd_argv[16];
    
    printf("\n");
    printf("  ____  _____  ___   ___ ___  _   _ \n");
    printf(" / ___|| ____/ _ \\ / _ \\_ _|| \\ | |\n");
    printf(" \\___ \\|  _|| | | | | | | | |  \\| |\n");
    printf("  ___) | |__| |_| | |_| | | | |\\  |\n");
    printf(" |____/|_____\\___/ \\___/___|_| \\_|\n");
    printf("\n");
    printf("OS info shell version 0.2\n");
    printf("Type 'help' for built-in commands.\n\n");
    
    while (true) {
        printf("[root@localhost %s]# ", current_path);
        //fflush(stdout);
        
        if (!gets(buf)) {
            break;
        }
        
        int len = strlen(buf);
        if (len > 0 && buf[len - 1] == '\n') {
            buf[len - 1] = '\0';
        }
        
        if (buf[0] == '\0') continue;
        
        int cmd_argc = parse_args(buf, cmd_argv, 16);
        if (cmd_argc == 0) continue;
        
        const char *cmd = cmd_argv[0];
        
        if (strcmp(cmd, "exit") == 0) {
            break;
        } else if (strcmp(cmd, "help") == 0) {
            cmd_help();
        } else if (strcmp(cmd, "version") == 0) {
            printf("OS info version 0.2\n");
        } else if (strcmp(cmd, "clear") == 0) {
            printf("\033[2J\033[H");
        } else if (strcmp(cmd, "ls") == 0) {
            cmd_ls(cmd_argc > 1 ? cmd_argv[1] : current_path);
        } else if (strcmp(cmd, "cd") == 0) {
            cmd_cd(cmd_argc > 1 ? cmd_argv[1] : nullptr);
        } else if (strcmp(cmd, "pwd") == 0) {
            cmd_pwd();
        } else if (strcmp(cmd, "cat") == 0) {
            cmd_cat(cmd_argc > 1 ? cmd_argv[1] : nullptr);
        } else if (strcmp(cmd, "mkdir") == 0) {
            cmd_mkdir(cmd_argc > 1 ? cmd_argv[1] : nullptr);
        } else if (strcmp(cmd, "rm") == 0) {
            cmd_rm(cmd_argc > 1 ? cmd_argv[1] : nullptr);
        } else if (strcmp(cmd, "touch") == 0) {
            cmd_touch(cmd_argc > 1 ? cmd_argv[1] : nullptr);
        } else if (strcmp(cmd, "cp") == 0) {
            cmd_cp(cmd_argc > 1 ? cmd_argv[1] : nullptr, cmd_argc > 2 ? cmd_argv[2] : nullptr);
        } else if (strcmp(cmd, "echo") == 0) {
            cmd_echo(cmd_argv, cmd_argc);
        } else if (strcmp(cmd, "sh") == 0) {
            exec_args *args = new exec_args;
            strcpy(args->path, "/bin/sh");
            args->argv[0] = strdup("sh");
            args->argv[1] = nullptr;
            
            pid_t pid = clone(exec_child, nullptr, 0, args);
            if (pid > 0) {
                int status;
                waitpid(pid, &status, 0);
                free(args->argv[0]);
                delete args;
            } else {
                free(args->argv[0]);
                delete args;
                printf("sh: failed to fork\n");
            }
        } else {
            if (try_exec_external(cmd_argv, cmd_argc) < 0) {
                printf("sh: %s: command not found\n", cmd);
            }
        }
    }
    
    delete[] buf;
    printf("Goodbye!\n");
    return 0;
}
