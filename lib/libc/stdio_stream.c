#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "lock.h"
#include "stdio_internal.h"

static FileState* streams;
static volatile uint32_t streams_lock;

/* 新的 popen 子进程必须关闭此前 popen 流的父端，避免意外延长管道寿命。 */
int __libc_popen_descriptors(int* descriptors, size_t capacity)
{
    size_t count = 0;
    libcWaitLock(&streams_lock);
    for (FileState* state = streams; state; state = state->next)
    {
        if (!state->child) continue;
        if (count == capacity) { libcWaitUnlock(&streams_lock); errno = EMFILE; return -1; }
        descriptors[count++] = state->stream->fileDescriptor;
    }
    libcWaitUnlock(&streams_lock);
    return (int)count;
}

/* 流第一次使用时才建立状态；发布指针前先完成默认模式和链表登记。 */
FileState* __libc_stream_state(FILE* stream)
{
    if (!stream) { errno = EINVAL; return NULL; }
    FileState* state = __atomic_load_n(&stream->state, __ATOMIC_ACQUIRE);
    if (state) return state;
    libcWaitLock(&streams_lock);
    state = stream->state;
    if (!state)
    {
        state = calloc(1, sizeof(*state));
        if (state)
        {
            int saved = errno;
            int flags = fcntl(stream->fileDescriptor, F_GETFL);
            state->readable = flags >= 0 && (flags & O_ACCMODE) != O_WRONLY;
            state->writable = flags >= 0 && (flags & O_ACCMODE) != O_RDONLY;
            state->append = flags >= 0 && (flags & O_APPEND);
            /* 现有交互程序直接 read 取键；终端提示须即时显示，普通文件默认缓冲。 */
            state->mode = stream == stderr || isatty(stream->fileDescriptor) ? _IONBF : _IOFBF;
            errno = saved;
            state->stream = stream;
            state->wide_pushed = WEOF;
            state->numeric_pushed = WEOF;
            state->next = streams;
            streams = state;
            __atomic_store_n(&stream->state, state, __ATOMIC_RELEASE);
        }
    }
    libcWaitUnlock(&streams_lock);
    return state;
}

/* 流锁允许同一线程递归获取，fprintf 等复合操作可复用基础读写入口。 */
void flockfile(FILE* stream)
{
    FileState* state = __libc_stream_state(stream);
    if (!state) return;
    uint64_t self = __libc_os_get_thread_id();
    if (__atomic_load_n(&state->owner, __ATOMIC_ACQUIRE) == self)
    {
        ++state->depth;
        return;
    }
    libcWaitLock(&state->lock);
    state->depth = 1;
    __atomic_store_n(&state->owner, self, __ATOMIC_RELEASE);
}

int ftrylockfile(FILE* stream)
{
    FileState* state = __libc_stream_state(stream);
    if (!state) return -1;
    uint64_t self = __libc_os_get_thread_id();
    if (__atomic_load_n(&state->owner, __ATOMIC_ACQUIRE) == self)
    {
        ++state->depth;
        return 0;
    }
    uint32_t expected = 0;
    if (!__atomic_compare_exchange_n(&state->lock, &expected, 1, 0,
                                       __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) return 1;
    state->depth = 1;
    __atomic_store_n(&state->owner, self, __ATOMIC_RELEASE);
    return 0;
}

void funlockfile(FILE* stream)
{
    FileState* state = stream ? stream->state : NULL;
    if (!state || __atomic_load_n(&state->owner, __ATOMIC_ACQUIRE) != __libc_os_get_thread_id()) return;
    if (--state->depth) return;
    __atomic_store_n(&state->owner, 0, __ATOMIC_RELEASE);
    libcWaitUnlock(&state->lock);
}

/* 模式解析只接受已定义的修饰符，所有创建入口使用同一校验规则。 */
int __libc_stream_mode(const char* mode, int* flags)
{
    if (!mode || !*mode || !strchr("rwa", *mode)) { errno = EINVAL; return -1; }
    *flags = *mode == 'r' ? O_RDONLY : *mode == 'w' ? O_WRONLY | O_CREAT | O_TRUNC : O_WRONLY | O_CREAT | O_APPEND;
    unsigned seen = 0;
    for (const char* cursor = mode + 1; *cursor; ++cursor)
    {
        unsigned bit = *cursor == '+' ? 1 : *cursor == 'b' ? 2 : *cursor == 'e' ? 4 : *cursor == 'x' ? 8 : 0;
        if (!bit || (seen & bit) || (*cursor == 'x' && *mode != 'w')) { errno = EINVAL; return -1; }
        seen |= bit;
    }
    if (seen & 1) *flags = (*flags & ~O_ACCMODE) | O_RDWR;
    if (seen & 4) *flags |= O_CLOEXEC;
    if (seen & 8) *flags |= O_EXCL;
    return 0;
}

/* 内存流每次刷新都同步结尾 NUL 和调用者可见的地址、长度。 */
static void publish(FileState* state)
{
    if (!state->memory_kind) return;
    if (state->memory_capacity && state->writable &&
        !(state->memory_kind == 1 && state->length >= state->memory_capacity && !state->memory_truncated))
    {
        size_t end = state->length < state->memory_capacity ? state->length : state->memory_capacity - 1;
        if (state->memory_kind >= 3) ((wchar_t*)state->memory)[end] = 0;
        else state->memory[end] = 0;
    }
    if (state->result) *state->result = (char*)state->memory;
    if (state->wide_result) *state->wide_result = (wchar_t*)state->memory;
    if (state->result_size) *state->result_size = state->position < state->length ? state->position : state->length;
}

/* 后端写入处理短写；内存容量不足时只记下实际写入的字节数。 */
static size_t backend_write(FileState* state, const unsigned char* bytes, size_t count)
{
    if (state->memory_kind)
    {
        size_t unit = state->memory_kind >= 3 ? sizeof(wchar_t) : 1;
        if (state->append) state->position = state->length;
        if (count == SIZE_MAX || state->position > SIZE_MAX - count - 1) { errno = EOVERFLOW; return 0; }
        size_t needed = state->position + count;
        if (needed >= SIZE_MAX / unit) { errno = EOVERFLOW; return 0; }
        if ((state->memory_kind == 2 || state->memory_kind == 3) && needed + 1 > state->memory_capacity)
        {
            size_t capacity = state->memory_capacity ? state->memory_capacity : 64;
            while (capacity <= needed)
            {
                if (capacity > SIZE_MAX / 2) { capacity = needed + 1; break; }
                capacity *= 2;
            }
            if (capacity > SIZE_MAX / unit) capacity = needed + 1;
            unsigned char* memory = realloc(state->memory, capacity * unit);
            if (!memory) return 0;
            state->memory = memory;
            state->memory_capacity = capacity;
        }
        if (count > state->memory_capacity - state->position)
        {
            count = state->memory_capacity - state->position;
            errno = ENOSPC;
        }
        if (state->position > state->length) memset(state->memory + state->length * unit, 0, (state->position - state->length) * unit);
        memcpy(state->memory + state->position * unit, bytes, count * unit);
        state->position += count;
        if (state->position > state->length) state->length = state->position;
        return count;
    }
    size_t done = 0;
    while (done < count)
    {
        ssize_t written = write(state->stream->fileDescriptor, bytes + done, count - done);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) { if (!written) errno = EIO; break; }
        done += (size_t)written;
    }
    return done;
}

static ssize_t backend_read(FileState* state, unsigned char* bytes, size_t count)
{
    if (!state->memory_kind) return read(state->stream->fileDescriptor, bytes, count);
    if (state->memory_kind == 5)
    {
        if (!count || state->position >= state->length) return 0;
        wchar_t value = ((wchar_t*)state->memory)[state->position++];
        bytes[0] = (unsigned)value < 128 ? (unsigned char)value : 0x80;
        if ((unsigned)value >= 128) state->numeric_pushed = (wint_t)value;
        return 1;
    }
    size_t available = state->position < state->length ? state->length - state->position : 0;
    if (count > available) count = available;
    if (!count) return 0;
    memcpy(bytes, state->memory + state->position, count);
    state->position += count;
    return (ssize_t)count;
}

/* 输入缓冲的预读部分不属于逻辑文件位置，定位或切换写入前必须退回。 */
int __libc_stream_flush(FileState* state)
{
    if (state->phase == 2 && state->end)
    {
        size_t written = backend_write(state, state->buffer, state->end);
        if (written != state->end)
        {
            memmove(state->buffer, state->buffer + written, state->end - written);
            state->end -= written;
            state->stream->error = 1;
            return EOF;
        }
    }
    else if (state->phase == 1)
    {
        size_t unread = state->end - state->begin + (state->stream->pushed != EOF);
        if (unread)
        {
            if (state->memory_kind) {
                if (state->position < unread) { errno = EINVAL; return EOF; }
                state->position -= unread;
            }
            else if (lseek(state->stream->fileDescriptor, -(off_t)unread, SEEK_CUR) < 0) return EOF;
        }
        state->stream->pushed = EOF;
    }
    state->begin = state->end = 0;
    state->phase = 0;
    publish(state);
    return 0;
}

static int buffer_ready(FileState* state)
{
    if (state->mode == _IONBF || state->buffer) return 0;
    state->buffer = malloc(BUFSIZ);
    if (!state->buffer) return -1;
    state->capacity = BUFSIZ;
    state->buffer_owned = 1;
    return 0;
}

/* 所有字节输入共用这一入口，ungetc 的字节也计入 fread 返回值。 */
static size_t read_bytes(FileState* state, unsigned char* bytes, size_t count)
{
    FILE* stream = state->stream;
    if (!state->readable) { errno = EBADF; stream->error = 1; return 0; }
    if (state->phase == 2 && __libc_stream_flush(state)) return 0;
    if (buffer_ready(state)) { stream->error = 1; return 0; }
    state->phase = 1;
    size_t done = 0;
    if (count && stream->pushed != EOF)
    {
        bytes[done++] = (unsigned char)stream->pushed;
        stream->pushed = EOF;
    }
    while (done < count)
    {
        size_t available = state->end - state->begin;
        if (available)
        {
            if (available > count - done) available = count - done;
            memcpy(bytes + done, state->buffer + state->begin, available);
            state->begin += available;
            done += available;
            continue;
        }
        if (stream->endOfFile) break;
        ssize_t size = state->mode == _IONBF ? backend_read(state, bytes + done, count - done) :
            backend_read(state, state->buffer, state->capacity);
        if (size <= 0)
        {
            if (!size) stream->endOfFile = 1;
            else stream->error = 1;
            break;
        }
        if (state->mode == _IONBF) done += (size_t)size;
        else { state->begin = 0; state->end = (size_t)size; }
    }
    return done;
}

static size_t write_bytes(FileState* state, const unsigned char* bytes, size_t count)
{
    FILE* stream = state->stream;
    if (!state->writable) { errno = EBADF; stream->error = 1; return 0; }
    if (state->phase == 1 && __libc_stream_flush(state)) { stream->error = 1; return 0; }
    if (buffer_ready(state)) { stream->error = 1; return 0; }
    state->phase = 2;
    size_t done = 0;
    if (state->mode == _IONBF)
    {
        done = backend_write(state, bytes, count);
        if (done != count) stream->error = 1;
        return done;
    }
    while (done < count)
    {
        if (state->end == state->capacity)
        {
            if (__libc_stream_flush(state)) break;
            state->phase = 2;
        }
        unsigned char byte = bytes[done++];
        state->buffer[state->end++] = byte;
        if (state->mode == _IOLBF && byte == '\n')
        {
            if (__libc_stream_flush(state)) break;
            state->phase = 2;
        }
    }
    return done;
}

size_t fread(void* buffer, size_t size, size_t count, FILE* stream)
{
    if (!size || !count) return 0;
    if (count > SIZE_MAX / size) { errno = EOVERFLOW; if (stream) stream->error = 1; return 0; }
    FileState* state = __libc_stream_state(stream);
    if (!state) return 0;
    flockfile(stream);
    size_t done = read_bytes(state, buffer, size * count);
    if (!stream->orientation) stream->orientation = -1;
    funlockfile(stream);
    return done / size;
}

size_t fwrite(const void* buffer, size_t size, size_t count, FILE* stream)
{
    if (!size || !count) return 0;
    if (count > SIZE_MAX / size) { errno = EOVERFLOW; if (stream) stream->error = 1; return 0; }
    FileState* state = __libc_stream_state(stream);
    if (!state) return 0;
    flockfile(stream);
    size_t done = write_bytes(state, buffer, size * count);
    if (!stream->orientation) stream->orientation = -1;
    funlockfile(stream);
    return done / size;
}

int __libc_getc_unlocked(FILE* stream)
{
    FileState* state = __libc_stream_state(stream);
    unsigned char byte;
    return state && read_bytes(state, &byte, 1) == 1 ? byte : EOF;
}

int __libc_putc_unlocked(int value, FILE* stream)
{
    FileState* state = __libc_stream_state(stream);
    unsigned char byte = (unsigned char)value;
    return state && write_bytes(state, &byte, 1) == 1 ? byte : EOF;
}

int fgetc(FILE* stream) { unsigned char byte; return fread(&byte, 1, 1, stream) == 1 ? byte : EOF; }
int fputc(int value, FILE* stream) { unsigned char byte = (unsigned char)value; return fwrite(&byte, 1, 1, stream) == 1 ? byte : EOF; }
int getchar(void) { return fgetc(stdin); }
int putchar(int value) { return fputc(value, stdout); }
int fputs(const char* value, FILE* stream) { size_t size = strlen(value); return fwrite(value, 1, size, stream) == size ? 0 : EOF; }
int puts(const char* value) { return fputs(value, stdout) == 0 && fputc('\n', stdout) != EOF ? 0 : EOF; }

char* fgets(char* buffer, int size, FILE* stream)
{
    if (!buffer || size <= 0 || !__libc_stream_state(stream)) return NULL;
    flockfile(stream);
    if (!stream->orientation) stream->orientation = -1;
    int used = 0;
    while (used + 1 < size)
    {
        int byte = __libc_getc_unlocked(stream);
        if (byte == EOF) break;
        buffer[used++] = (char)byte;
        if (byte == '\n') break;
    }
    buffer[used] = 0;
    funlockfile(stream);
    return used || size == 1 ? buffer : NULL;
}

FILE* fopen(const char* path, const char* mode)
{
    int flags;
    if (__libc_stream_mode(mode, &flags)) return NULL;
    int descriptor = open(path, flags, 0666);
    if (descriptor < 0) return NULL;
    FILE* stream = fdopen(descriptor, mode);
    if (!stream) { int saved = errno; close(descriptor); errno = saved; }
    return stream;
}

FILE* fdopen(int descriptor, const char* mode)
{
    int flags;
    if (__libc_stream_mode(mode, &flags)) return NULL;
    int actual = fcntl(descriptor, F_GETFL);
    if (actual < 0) return NULL;
    int wanted = flags & O_ACCMODE;
    if (((actual & O_ACCMODE) == O_RDONLY && wanted != O_RDONLY) ||
        ((actual & O_ACCMODE) == O_WRONLY && wanted != O_WRONLY)) { errno = EINVAL; return NULL; }
    if ((flags & O_APPEND) && !(actual & O_APPEND) && fcntl(descriptor, F_SETFL, actual | O_APPEND) < 0) return NULL;
    if ((flags & O_CLOEXEC) && fcntl(descriptor, F_SETFD, FD_CLOEXEC) < 0) return NULL;
    FILE* stream = calloc(1, sizeof(*stream));
    if (!stream) return NULL;
    stream->fileDescriptor = descriptor;
    stream->owned = 1;
    stream->pushed = EOF;
    FileState* state = __libc_stream_state(stream);
    if (!state) { free(stream); return NULL; }
    state->readable = wanted != O_WRONLY;
    state->writable = wanted != O_RDONLY;
    return stream;
}

int fflush(FILE* stream)
{
    if (stream)
    {
        FileState* state = __libc_stream_state(stream);
        if (!state) return EOF;
        flockfile(stream);
        int result = __libc_stream_flush(state);
        funlockfile(stream);
        return result;
    }
    int result = 0;
    libcWaitLock(&streams_lock);
    for (FileState* state = streams; state; state = state->next)
    {
        if (state->phase != 2) continue;
        if (fflush(state->stream)) result = EOF;
    }
    libcWaitUnlock(&streams_lock);
    return result;
}

/* 关闭时先刷新，仍要释放 FD 和库拥有的缓冲；内存流输出的所有权交给调用者。 */
int fclose(FILE* stream)
{
    if (!stream) { errno = EINVAL; return EOF; }
    libcWaitLock(&streams_lock);
    FileState* state = stream->state;
    int result = state && (state->phase == 2 || state->memory_kind) ? fflush(stream) : 0;
    int saved = errno;
    if (stream->fileDescriptor >= 0 && close(stream->fileDescriptor)) { result = EOF; saved = errno; }
    if (state)
    {
        FileState** link = &streams;
        while (*link && *link != state) link = &(*link)->next;
        if (*link) *link = state->next;
        if (state->buffer_owned) free(state->buffer);
        if (state->memory_owned && state->memory_kind < 2) free(state->memory);
        free(state);
        stream->state = NULL;
    }
    libcWaitUnlock(&streams_lock);
    if (stream != stdin && stream != stdout && stream != stderr) free(stream);
    else stream->fileDescriptor = -1;
    errno = saved;
    return result;
}

int setvbuf(FILE* restrict stream, char* restrict buffer, int mode, size_t size)
{
    if (mode != _IONBF && mode != _IOLBF && mode != _IOFBF) { errno = EINVAL; return -1; }
    FileState* state = __libc_stream_state(stream);
    if (!state) return -1;
    if (mode != _IONBF && !size) { if (buffer) { errno = EINVAL; return -1; } size = BUFSIZ; }
    unsigned char* replacement = mode == _IONBF ? NULL : (unsigned char*)buffer;
    if (mode != _IONBF && !replacement) { replacement = malloc(size); if (!replacement) return -1; }
    flockfile(stream);
    int result = __libc_stream_flush(state);
    if (!result)
    {
        if (state->buffer_owned) free(state->buffer);
        state->buffer = replacement;
        state->capacity = mode == _IONBF ? 0 : size;
        state->buffer_owned = mode != _IONBF && !buffer;
        state->mode = mode;
    }
    else if (!buffer) free(replacement);
    funlockfile(stream);
    return result ? -1 : 0;
}

void setbuf(FILE* restrict stream, char* restrict buffer) { setvbuf(stream, buffer, buffer ? _IOFBF : _IONBF, buffer ? BUFSIZ : 0); }
int fileno(FILE* stream) { if (!stream || stream->fileDescriptor < 0) { errno = EBADF; return -1; } return stream->fileDescriptor; }
int feof(FILE* stream) { return stream ? stream->endOfFile : 0; }
int ferror(FILE* stream) { return stream ? stream->error : 1; }
void clearerr(FILE* stream) { if (stream) stream->error = stream->endOfFile = 0; }

/* 内部接口供定位和内存流创建复用，避免各自维护一套缓冲算法。 */
FILE* __libc_memory_stream(void* memory, size_t size, const char* mode, int grow,
                            char** result, size_t* result_size)
{
    int flags;
    if (__libc_stream_mode(mode, &flags)) return NULL;
    FILE* stream = calloc(1, sizeof(*stream));
    if (!stream) return NULL;
    stream->fileDescriptor = -1;
    stream->pushed = EOF;
    FileState* state = __libc_stream_state(stream);
    if (!state) { free(stream); return NULL; }
    state->mode = _IONBF;
    state->memory_kind = grow == 3 ? 3 : grow ? 2 : 1;
    state->readable = (flags & O_ACCMODE) != O_WRONLY;
    state->writable = (flags & O_ACCMODE) != O_RDONLY;
    state->append = (flags & O_APPEND) != 0;
    state->memory_capacity = grow ? 64 : size;
    state->memory = memory ? memory : calloc(state->memory_capacity ? state->memory_capacity : 1,
                                               grow == 3 ? sizeof(wchar_t) : 1);
    if (!state->memory) { state->memory_capacity = 0; fclose(stream); return NULL; }
    state->memory_owned = !memory;
    state->memory_truncated = *mode == 'w';
    state->length = grow || *mode == 'w' ? 0 : *mode == 'a' ? strnlen((char*)state->memory, size) : size;
    state->position = *mode == 'a' ? state->length : 0;
    state->result = result;
    state->result_size = result_size;
    if (*mode == 'w' && state->memory_capacity) state->memory[0] = 0;
    if (grow || *mode == 'w') publish(state);
    return stream;
}

/* 定位前清空输出或退回预读，内存文件也使用相同的逻辑位置。 */
int fseeko(FILE* stream, off_t offset, int whence)
{
    if (whence != SEEK_SET && whence != SEEK_CUR && whence != SEEK_END) { errno = EINVAL; return -1; }
    FileState* state = __libc_stream_state(stream);
    if (!state) return -1;
    flockfile(stream);
    int result = __libc_stream_flush(state) ? -1 : 0;
    if (!result && state->memory_kind)
    {
        size_t base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? state->position : state->length;
        if (base > INT64_MAX || (offset >= 0 && (uint64_t)offset > INT64_MAX - base) ||
            (offset < 0 && (uint64_t)(-(offset + 1)) + 1 > base)) { errno = EINVAL; result = -1; }
        else
        {
            size_t position = offset < 0 ? base - ((uint64_t)(-(offset + 1)) + 1) : base + (size_t)offset;
            if ((state->memory_kind == 1 || state->memory_kind >= 4) && position > state->memory_capacity) { errno = EINVAL; result = -1; }
            else state->position = position;
        }
    }
    else if (!result && lseek(stream->fileDescriptor, offset, whence) < 0) result = -1;
    if (!result) { stream->pushed = EOF; stream->endOfFile = 0; state->wide_pushed = WEOF; }
    funlockfile(stream);
    return result;
}

off_t ftello(FILE* stream)
{
    FileState* state = __libc_stream_state(stream);
    if (!state) return -1;
    flockfile(stream);
    off_t result = state->memory_kind ? (off_t)state->position : lseek(stream->fileDescriptor, 0, SEEK_CUR);
    if (result >= 0)
    {
        if (state->phase == 1) result -= (off_t)(state->end - state->begin);
        if (state->phase == 2) result += (off_t)state->end;
        if (stream->pushed != EOF) --result;
    }
    funlockfile(stream);
    return result;
}

FILE* freopen(const char* restrict path, const char* restrict mode, FILE* restrict stream)
{
    if (!stream || !path) { errno = EINVAL; return NULL; }
    libcWaitLock(&streams_lock);
    FileState* old = stream->state;
    if (old) fflush(stream);
    if (stream->fileDescriptor >= 0) close(stream->fileDescriptor);
    if (old)
    {
        FileState** link = &streams;
        while (*link && *link != old) link = &(*link)->next;
        if (*link) *link = old->next;
        if (old->buffer_owned) free(old->buffer);
        if (old->memory_owned && old->memory_kind < 2) free(old->memory);
        free(old);
    }
    stream->state = NULL;
    stream->fileDescriptor = -1;
    libcWaitUnlock(&streams_lock);
    /* 必须先刷新并关闭旧流，才能以 w 模式截断同一路径。打开失败时旧流也已关闭。 */
    FILE* fresh = fopen(path, mode);
    if (!fresh)
    {
        if (stream != stdin && stream != stdout && stream != stderr) free(stream);
        return NULL;
    }
    libcWaitLock(&streams_lock);
    *stream = *fresh;
    stream->state->stream = stream;
    free(fresh);
    libcWaitUnlock(&streams_lock);
    return stream;
}

FILE* fmemopen(void* restrict memory, size_t size, const char* restrict mode)
{
    if (size > SSIZE_MAX) { errno = EINVAL; return NULL; }
    return __libc_memory_stream(memory, size, mode, 0, NULL, NULL);
}

FILE* open_memstream(char** result, size_t* size)
{
    if (!result || !size) { errno = EINVAL; return NULL; }
    return __libc_memory_stream(NULL, 0, "w+", 1, result, size);
}

/* 宽内存流按 wchar_t 计位置和长度，不经过 UTF-8 字节编码。 */
FILE* open_wmemstream(wchar_t** result, size_t* size)
{
    if (!result || !size) { errno = EINVAL; return NULL; }
    FILE* stream = __libc_memory_stream(NULL, 0, "w+", 3, NULL, NULL);
    if (!stream) return NULL;
    FileState* state = stream->state;
    state->readable = 0;
    state->wide_result = result;
    state->result_size = size;
    stream->orientation = 1;
    publish(state);
    return stream;
}

/* 字符串格式化使用有限容量，字符串扫描直接读取 wchar_t，不受编码区域设置影响。 */
FILE* __libc_wide_string_stream(wchar_t* memory, size_t length, int input)
{
    FILE* stream = __libc_memory_stream(memory, length, input ? "r" : "w", 0, NULL, NULL);
    if (!stream) return NULL;
    stream->orientation = 1;
    stream->state->memory_kind = input ? 5 : 4;
    if (!input && length) memory[0] = 0;
    return stream;
}

int __libc_stream_write_wchar(FileState* state, wchar_t value)
{
    if (!state->writable) { errno = EBADF; state->stream->error = 1; return -1; }
    state->phase = 2;
    if (backend_write(state, (const unsigned char*)&value, 1) == 1) return 0;
    state->stream->error = 1;
    return -1;
}

int ungetc(int value, FILE* stream)
{
    if (value == EOF || !__libc_stream_state(stream)) return EOF;
    flockfile(stream);
    int result = EOF;
    if (stream->pushed == EOF)
    {
        stream->pushed = (unsigned char)value;
        stream->endOfFile = 0;
        result = stream->pushed;
    }
    funlockfile(stream);
    return result;
}

/* fork 前固定流表和各流锁；子进程只继承当前线程，必须清掉失去主人的锁。 */
void __libc_stdio_fork_prepare(void)
{
    libcWaitLock(&streams_lock);
    for (FileState* state = streams; state; state = state->next) flockfile(state->stream);
}

void __libc_stdio_fork_parent(void)
{
    for (FileState* state = streams; state; state = state->next) funlockfile(state->stream);
    libcWaitUnlock(&streams_lock);
}

void __libc_stdio_fork_child(void)
{
    for (FileState* state = streams; state; state = state->next)
    {
        state->owner = 0;
        state->depth = 0;
        state->lock = 0;
    }
    streams_lock = 0;
}
