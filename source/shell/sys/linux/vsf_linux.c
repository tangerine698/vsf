/*****************************************************************************
 *   Copyright(C)2009-2019 by VSF Team                                       *
 *                                                                           *
 *  Licensed under the Apache License, Version 2.0 (the "License");          *
 *  you may not use this file except in compliance with the License.         *
 *  You may obtain a copy of the License at                                  *
 *                                                                           *
 *     http://www.apache.org/licenses/LICENSE-2.0                            *
 *                                                                           *
 *  Unless required by applicable law or agreed to in writing, software      *
 *  distributed under the License is distributed on an "AS IS" BASIS,        *
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. *
 *  See the License for the specific language governing permissions and      *
 *  limitations under the License.                                           *
 *                                                                           *
 ****************************************************************************/

/*============================ INCLUDES ======================================*/

#include "./vsf_linux_cfg.h"

#if VSF_USE_LINUX == ENABLED

#define __VSF_EDA_CLASS_INHERIT__
#define __VSF_SIMPLE_STREAM_CLASS_INHERIT__
#define __VSF_FS_CLASS_INHERIT__
#define __VSF_LINUX_CLASS_IMPLEMENT

#if VSF_LINUX_CFG_RELATIVE_PATH == ENABLED
#   include "./include/unistd.h"
#   include "./include/semaphore.h"
#   include "./include/signal.h"
#   include "./include/poll.h"
#   include "./include/sys/stat.h"
#   include "./include/sys/select.h"
#   include "./include/sys/wait.h"
#   include "./include/sys/mount.h"
#   include "./include/sys/ipc.h"
#   include "./include/sys/shm.h"
#   include "./include/fcntl.h"
#   include "./include/errno.h"
#   include "./include/termios.h"
#   include "./include/pwd.h"
#else
#   include <unistd.h>
#   include <semaphore.h>
#   include <signal.h>
#   include <poll.h>
#   include <sys/stat.h>
#   include <sys/select.h>
#   include <sys/wait.h>
#   include <sys/mount.h>
#   include <sys/ipc.h>
#   include <sys/shm.h>
#   include <fcntl.h>
#   include <errno.h>
#   include <termios.h>
#   include <pwd.h>
#endif
#include <stdarg.h>
#if VSF_LINUX_CFG_RELATIVE_PATH == ENABLED && VSF_LINUX_USE_SIMPLE_STDLIB == ENABLED
#   include "./include/simple_libc/stdlib.h"
#else
#   include <stdlib.h>
#endif
#if VSF_LINUX_CFG_RELATIVE_PATH == ENABLED && VSF_LINUX_USE_SIMPLE_STRING == ENABLED
#   include "./include/simple_libc/string.h"
#else
#   include <string.h>
#endif
#if VSF_LINUX_CFG_RELATIVE_PATH == ENABLED && VSF_LINUX_USE_SIMPLE_STDIO == ENABLED
#   include "./include/simple_libc/stdio.h"
#else
#   include <stdio.h>
#endif

#if __IS_COMPILER_IAR__
//! statement is unreachable
#   pragma diag_suppress=pe111
#endif

#if __IS_COMPILER_GCC__
#   pragma GCC diagnostic push
#   pragma GCC diagnostic ignored "-Wcast-align"
#endif

/*============================ MACROS ========================================*/

#if VSF_KERNEL_CFG_EDA_SUPPORT_ON_TERMINATE != ENABLED
#   error VSF_KERNEL_CFG_EDA_SUPPORT_ON_TERMINATE MUST be enbled
#endif

#if VSF_KERNEL_CFG_EDA_SUPPORT_SUB_CALL != ENABLED
#   error VSF_KERNEL_CFG_EDA_SUPPORT_SUB_CALL MUST be enabled
#endif

#ifndef VSF_LINUX_CFG_PRIO_LOWEST
#   define VSF_LINUX_CFG_PRIO_LOWEST        vsf_prio_0
#endif

#ifndef VSF_LINUX_CFG_PRIO_HIGHEST
#   define VSF_LINUX_CFG_PRIO_HIGHEST       vsf_prio_0
#endif

/*============================ MACROFIED FUNCTIONS ===========================*/
/*============================ TYPES =========================================*/

#if VSF_LINUX_CFG_SHM_NUM > 0
dcl_vsf_bitmap(vsf_linux_shm_bitmap, VSF_LINUX_CFG_SHM_NUM);
typedef struct vsf_linux_shm_mem_t {
    key_t key;
    void *buffer;
    uint32_t size;
} vsf_linux_shm_mem_t;
#endif

typedef struct vsf_linux_t {
    int cur_tid;
    int cur_pid;
    vsf_dlist_t process_list;

    vsf_linux_process_t *kernel_process;
#if VSF_LINUX_CFG_SUPPORT_SIG == ENABLED
    int sig_pid;
#endif

    vsf_linux_stdio_stream_t stdio_stream;

#if VSF_LINUX_CFG_SHM_NUM > 0
    struct {
        vsf_bitmap(vsf_linux_shm_bitmap) bitmap;
        vsf_linux_shm_mem_t mem[VSF_LINUX_CFG_SHM_NUM];
    } shm;
#endif
} vsf_linux_t;

typedef struct vsf_linux_main_priv_t {
    vsf_linux_process_ctx_t *ctx;
} vsf_linux_main_priv_t;

typedef struct vsf_linux_stream_priv_t {
    vsf_stream_t *stream;
} vsf_linux_stream_priv_t;

typedef struct vsf_linux_pipe_buffer_t {
    vsf_slist_node_t buffer_node;
    size_t size;
    size_t pos;
} vsf_linux_pipe_buffer_t;

typedef struct vsf_linux_pipe_rx_priv_t {
    int fd_tx;
    vsf_slist_queue_t buffer_queue;
} vsf_linux_pipe_rx_priv_t;

typedef struct vsf_linux_pipe_tx_priv_t {
    int fd_rx;
} vsf_linux_pipe_tx_priv_t;

/*============================ GLOBAL VARIABLES ==============================*/

int errno;

const struct passwd __vsf_default_passwd = {
    .pw_name            = "vsf",
    .pw_passwd          = "vsf",
    .pw_uid             = (uid_t)0,
    .pw_gid             = (gid_t)0,
    .pw_gecos           = "vsf",
    .pw_dir             = "/home",
    .pw_shell           = "vsh",
};

/*============================ PROTOTYPES ====================================*/

extern int vsf_linux_create_fhs(void);

#if VSF_LINUX_USE_SIMPLE_LIBC == ENABLED
extern void vsf_linux_glibc_init(void);
#endif

static void __vsf_linux_main_on_run(vsf_thread_cb_t *cb);

static int __vsf_linux_fs_fcntl(vsf_linux_fd_t *sfd, int cmd, long arg);
static ssize_t __vsf_linux_fs_read(vsf_linux_fd_t *sfd, void *buf, size_t count);
static ssize_t __vsf_linux_fs_write(vsf_linux_fd_t *sfd, const void *buf, size_t count);
static int __vsf_linux_fs_close(vsf_linux_fd_t *sfd);

static int __vsf_linux_stream_fcntl(vsf_linux_fd_t *sfd, int cmd, long arg);
static ssize_t __vsf_linux_stream_read(vsf_linux_fd_t *sfd, void *buf, size_t count);
static ssize_t __vsf_linux_stream_write(vsf_linux_fd_t *sfd, const void *buf, size_t count);
static int __vsf_linux_stream_close(vsf_linux_fd_t *sfd);

static int __vsf_linux_pipe_fcntl(vsf_linux_fd_t *sfd, int cmd, long arg);
static ssize_t __vsf_linux_pipe_read(vsf_linux_fd_t *sfd, void *buf, size_t count);
static ssize_t __vsf_linux_pipe_write(vsf_linux_fd_t *sfd, const void *buf, size_t count);
static int __vsf_linux_pipe_close(vsf_linux_fd_t *sfd);

static vsf_linux_process_t * __vsf_linux_start_process_internal(int stack_size,
        vsf_linux_main_entry_t entry, vsf_prio_t prio);

/*============================ LOCAL VARIABLES ===============================*/

static NO_INIT vsf_linux_t __vsf_linux;

static const vsf_linux_thread_op_t __vsf_linux_main_op = {
    .priv_size          = sizeof(vsf_linux_main_priv_t),
    .on_run             = __vsf_linux_main_on_run,
    .on_terminate       = vsf_linux_thread_on_terminate,
};

const vsf_linux_fd_op_t __vsf_linux_fs_fdop = {
    .priv_size          = sizeof(vsf_linux_fs_priv_t),
    .fn_fcntl           = __vsf_linux_fs_fcntl,
    .fn_read            = __vsf_linux_fs_read,
    .fn_write           = __vsf_linux_fs_write,
    .fn_close           = __vsf_linux_fs_close,
};

static const vsf_linux_fd_op_t __vsf_linux_stream_fdop = {
    .priv_size          = sizeof(vsf_linux_stream_priv_t),
    .fn_fcntl           = __vsf_linux_stream_fcntl,
    .fn_read            = __vsf_linux_stream_read,
    .fn_write           = __vsf_linux_stream_write,
    .fn_close           = __vsf_linux_stream_close,
};

static const vsf_linux_fd_op_t __vsf_linux_pipe_rx_fdop = {
    .priv_size          = sizeof(vsf_linux_pipe_rx_priv_t),
    .fn_fcntl           = __vsf_linux_pipe_fcntl,
    .fn_read            = __vsf_linux_pipe_read,
    .fn_close           = __vsf_linux_pipe_close,
};

static const vsf_linux_fd_op_t __vsf_linux_pipe_tx_fdop = {
    .priv_size          = sizeof(vsf_linux_pipe_tx_priv_t),
    .fn_fcntl           = __vsf_linux_pipe_fcntl,
    .fn_write           = __vsf_linux_pipe_write,
    .fn_close           = __vsf_linux_pipe_close,
};

/*============================ IMPLEMENTATION ================================*/

#ifndef WEAK_VSF_LINUX_CREATE_FHS
WEAK(vsf_linux_create_fhs)
int vsf_linux_create_fhs(void)
{
    return 0;
}
#endif

int vsf_linux_generate_path(char *path_out, int path_out_lenlen, char *dir, char *path_in)
{
    char working_dir[MAX_PATH];
    if (NULL == dir) {
        getcwd(working_dir, sizeof(working_dir));
        dir = working_dir;
    }

    if (path_in[0] == '/') {
        if (strlen(path_in) >= path_out_lenlen) {
            return -ENOMEM;
        }
        strcpy(path_out, path_in);
    } else {
        if (strlen(dir) + strlen(path_in) >= path_out_lenlen) {
            return -ENOMEM;
        }
        strcpy(path_out, dir);
        strcat(path_out, path_in);
    }

    // process .. an .
    char *tmp, *tmp_replace;
    while ((tmp = (char *)strstr(path_out, "/..")) != NULL) {
        tmp[0] = '\0';
        tmp_replace = (char *)strrchr(path_out, '/');
        if (NULL == tmp_replace) {
            return -ENOENT;
        }
        strcpy(tmp_replace, &tmp[3]);
    }
    while ((tmp = (char *)strstr(path_out, "/./")) != NULL) {
        strcpy(tmp, &tmp[2]);
    }

    // fix surfix "/."
    size_t len = strlen(path_out);
    if ((len >= 2) && ('.' == path_out[len - 1]) && ('/' == path_out[len - 2])) {
        path_out[len - 2] = '\0';
    }
    return 0;
}

static int __vsf_linux_init_thread(int argc, char *argv[])
{
    int err = vsf_linux_create_fhs();
    if (err) { return err; }
    return execl("/sbin/init", "init", NULL);
}

static int __vsf_linux_kernel_thread(int argc, char *argv[])
{
    __vsf_linux.kernel_process = vsf_linux_get_cur_process();

#if VSF_LINUX_CFG_SUPPORT_SIG != ENABLED
    __vsf_linux_init_thread(argc, argv);
#else
    // create init process(pid1)
    __vsf_linux_start_process_internal(0, __vsf_linux_init_thread, VSF_LINUX_CFG_PRIO_HIGHEST);

    vsf_linux_sig_handler_t *handler;
    vsf_linux_process_t *process;
    vsf_evt_t evt;
    unsigned long sig_mask;
    int sig;
    bool found_handler;

    while (1) {
        evt = vsf_thread_wait();
        VSF_LINUX_ASSERT(VSF_EVT_MESSAGE == evt);

        process = vsf_eda_get_cur_msg();
        sig_mask = process->sig.pending.sig[0] & ~process->sig.mask.sig[0];
        while (sig_mask) {
            sig = ffz(~sig_mask);
            sig_mask &= ~(1 << sig);

            found_handler = false;
            __vsf_dlist_foreach_unsafe(vsf_linux_sig_handler_t, node, &process->sig.handler_list) {
                if (_->sig == sig) {
                    handler = _;
                    found_handler = true;
                    break;
                }
            }

            __vsf_linux.sig_pid = process->id.pid;
            if (found_handler && (handler != SIG_DFL)) {
                if (handler != (vsf_linux_sig_handler_t *)SIG_IGN) {
                    siginfo_t siginfo = {
                        .si_signo   = sig,
                        .si_errno   = errno,
                    };
                    handler->handler(sig, &siginfo, NULL);
                }
            } else if (!((1 << sig) & ((1 << SIGURG) | (1 << SIGCONT) | (1 << SIGWINCH)))) {
                // TODO: terminate other thread is not supported in VSF, so just ignore
//                VSF_LINUX_ASSERT(false);
            }
        }
    }
#endif
    // actually will not return, just make compiler happy
    return 0;
}

vsf_err_t vsf_linux_init(vsf_linux_stdio_stream_t *stdio_stream)
{
    VSF_LINUX_ASSERT(stdio_stream != NULL);
    memset(&__vsf_linux, 0, sizeof(__vsf_linux));
    __vsf_linux.stdio_stream = *stdio_stream;
    vk_fs_init();

#if VSF_LINUX_USE_SIMPLE_LIBC == ENABLED
    vsf_linux_glibc_init();
#endif

    // create kernel process(pid0)
    if (NULL != __vsf_linux_start_process_internal(0, __vsf_linux_kernel_thread, VSF_LINUX_CFG_PRIO_LOWEST)) {
        return VSF_ERR_NONE;
    }
    return VSF_ERR_FAIL;
}

vsf_linux_thread_t * vsf_linux_create_thread(vsf_linux_process_t *process,
            const vsf_linux_thread_op_t *op,
            int stack_size, void *stack)
{
    vsf_linux_thread_t *thread;

    if (!process) {
        process = ((vsf_linux_thread_t *)vsf_eda_get_cur())->process;
    }
    if (!stack_size) {
        stack_size = VSF_LINUX_CFG_STACKSIZE;
    }

    uint_fast32_t thread_size = op->priv_size + sizeof(*thread);
    uint_fast32_t alignment;
    uint_fast32_t all_size;
    if (NULL == stack) {
        stack_size += (1 << VSF_KERNEL_CFG_THREAD_STACK_ALIGN_BIT) - 1;
        stack_size &= ~((1 << VSF_KERNEL_CFG_THREAD_STACK_ALIGN_BIT) - 1);
        thread_size += (1 << VSF_KERNEL_CFG_THREAD_STACK_ALIGN_BIT) - 1;
        thread_size &= ~((1 << VSF_KERNEL_CFG_THREAD_STACK_ALIGN_BIT) - 1);
        all_size = thread_size + stack_size;
        all_size += (1 << VSF_KERNEL_CFG_THREAD_STACK_ALIGN_BIT) - 1;
        alignment = 1 << VSF_KERNEL_CFG_THREAD_STACK_ALIGN_BIT;
    } else {
        all_size = thread_size;
        alignment = 0;
    }

    thread = (vsf_linux_thread_t *)vsf_heap_malloc_aligned(all_size, alignment);
    if (thread != NULL) {
        memset(thread, 0, thread_size);
        thread->process = process;

        // set entry and on_terminate
        thread->on_terminate = (vsf_eda_on_terminate_t)op->on_terminate;
        thread->entry = (vsf_thread_entry_t *)op->on_run;

        // set stack
        thread->stack_size = stack_size;
        if (stack != NULL) {
            thread->stack = stack;
        } else {
            thread->stack = (void *)((uintptr_t)thread + thread_size);
        }

        vsf_protect_t orig = vsf_protect_sched();
            thread->tid = __vsf_linux.cur_tid++;
            vsf_dlist_add_to_tail(vsf_linux_thread_t, thread_node, &process->thread_list, thread);
        vsf_unprotect_sched(orig);
    }
    return thread;
}

int vsf_linux_start_thread(vsf_linux_thread_t *thread, vsf_prio_t priority)
{
    if (vsf_prio_inherit == priority) {
        priority = thread->process->prio;
    }
    vsf_thread_start(&thread->use_as__vsf_thread_t, &thread->use_as__vsf_thread_cb_t, priority);
    return 0;
}

static vsf_linux_process_t * __vsf_linux_create_process(int stack_size)
{
    vsf_linux_process_t *process = calloc(1, sizeof(vsf_linux_process_t));
    if (process != NULL) {
        process->prio = vsf_prio_inherit;

        vsf_linux_thread_t *thread = vsf_linux_create_thread(process, &__vsf_linux_main_op, stack_size, NULL);
        if (NULL == thread) {
            free(process);
            return NULL;
        }

        vsf_linux_main_priv_t *priv = vsf_linux_thread_get_priv(thread);
        priv->ctx = &process->ctx;

        vsf_protect_t orig = vsf_protect_sched();
            process->id.pid = __vsf_linux.cur_pid++;
            vsf_dlist_add_to_tail(vsf_linux_process_t, process_node, &__vsf_linux.process_list, process);
        vsf_unprotect_sched(orig);
        if (process->id.pid) {
            process->id.ppid = getpid();
        }
    }
    return process;
}

vsf_linux_process_t * vsf_linux_create_process(int stack_size)
{
    vsf_linux_process_t *process = __vsf_linux_create_process(stack_size);
    if (process != NULL) {
        vsf_linux_process_t *parent_process = vsf_linux_get_cur_process();
        VSF_LINUX_ASSERT(parent_process != NULL);
        process->working_dir = strdup(parent_process->working_dir);
        process->stdio_stream = parent_process->stdio_stream;
        VSF_LINUX_ASSERT(process->working_dir != NULL);
    }
    return process;
}

int vsf_linux_start_process(vsf_linux_process_t *process)
{
    // TODO: check if already started
    vsf_linux_thread_t *thread;
    vsf_dlist_peek_head(vsf_linux_thread_t, thread_node, &process->thread_list, thread);
    return vsf_linux_start_thread(thread, vsf_prio_inherit);
}

static vsf_linux_process_t * __vsf_linux_start_process_internal(int stack_size,
        vsf_linux_main_entry_t entry, vsf_prio_t prio)
{
    VSF_LINUX_ASSERT((prio >= VSF_LINUX_CFG_PRIO_LOWEST) && (prio <= VSF_LINUX_CFG_PRIO_HIGHEST));
    vsf_linux_process_t *process = __vsf_linux_create_process(stack_size);
    if (process != NULL) {
        process->prio = prio;
        process->ctx.entry = entry;
        process->working_dir = strdup("/");
        process->stdio_stream = __vsf_linux.stdio_stream;
        VSF_LINUX_ASSERT(process->working_dir != NULL);
        vsf_linux_start_process(process);
    }
    return process;
}

vsf_linux_process_t * vsf_linux_get_process(pid_t pid)
{
    vsf_protect_t orig = vsf_protect_sched();
    __vsf_dlist_foreach_unsafe(vsf_linux_process_t, process_node, &__vsf_linux.process_list) {
        if (_->id.pid == pid) {
            vsf_unprotect_sched(orig);
            return _;
        }
    }
    vsf_unprotect_sched(orig);
    return NULL;
}

vsf_linux_thread_t * vsf_linux_get_thread(int tid)
{
    vsf_linux_process_t *process = vsf_linux_get_cur_process();
    vsf_protect_t orig = vsf_protect_sched();
    __vsf_dlist_foreach_unsafe(vsf_linux_thread_t, thread_node, &process->thread_list) {
        if (_->tid == tid) {
            vsf_unprotect_sched(orig);
            return _;
        }
    }
    vsf_unprotect_sched(orig);
    return NULL;
}

vsf_linux_thread_t * vsf_linux_get_cur_thread(void)
{
    return (vsf_linux_thread_t *)vsf_eda_get_cur();
}

vsf_linux_process_t * vsf_linux_get_cur_process(void)
{
    vsf_linux_thread_t *thread = vsf_linux_get_cur_thread();
    vsf_linux_process_t *process = thread->process;
    return process->id.pid != (pid_t)0 || NULL == __vsf_linux.kernel_process ?
                process : __vsf_linux.kernel_process;
}

static void __vsf_linux_main_on_run(vsf_thread_cb_t *cb)
{
    vsf_linux_thread_t *thread = container_of(cb, vsf_linux_thread_t, use_as__vsf_thread_cb_t);
    vsf_linux_process_t *process = thread->process;
    vsf_linux_main_priv_t *priv = vsf_linux_thread_get_priv(thread);
    vsf_linux_process_ctx_t *ctx = priv->ctx;

    vsf_linux_fd_t *sfd;
    vsf_linux_stream_priv_t *stream_priv;

    vsf_linux_create_fd(&sfd, &__vsf_linux_stream_fdop);
    stream_priv = (vsf_linux_stream_priv_t *)sfd->priv;
    stream_priv->stream = thread->process->stdio_stream.in;
    sfd->flags = O_RDONLY;

    vsf_linux_create_fd(&sfd, &__vsf_linux_stream_fdop);
    stream_priv = (vsf_linux_stream_priv_t *)sfd->priv;
    stream_priv->stream = thread->process->stdio_stream.out;
    sfd->flags = O_WRONLY;

    vsf_linux_create_fd(&sfd, &__vsf_linux_stream_fdop);
    stream_priv = (vsf_linux_stream_priv_t *)sfd->priv;
    stream_priv->stream = thread->process->stdio_stream.err;
    sfd->flags = O_WRONLY;

    VSF_LINUX_ASSERT(ctx->entry != NULL);
    thread->retval = ctx->entry(ctx->arg.argc, (char **)ctx->arg.argv);

    // clean up
    do {
        vsf_dlist_peek_head(vsf_linux_fd_t, fd_node, &process->fd_list, sfd);
        if (sfd != NULL) {
            close(sfd->fd);
        }
    } while (sfd != NULL);
}

void vsf_linux_thread_on_terminate(vsf_linux_thread_t *thread)
{
    if (thread->thread_pending != NULL) {
        thread->thread_pending->retval = thread->retval;
        vsf_eda_post_evt(&thread->thread_pending->use_as__vsf_eda_t, VSF_EVT_USER);
    }

    vsf_linux_process_t *process = thread->process;
    vsf_linux_thread_t *thread_main;
    vsf_dlist_peek_head(vsf_linux_thread_t, thread_node, &process->thread_list, thread_main);
    vsf_protect_t orig = vsf_protect_sched();
        vsf_dlist_remove(vsf_linux_thread_t, thread_node, &process->thread_list, thread);
    vsf_unprotect_sched(orig);
    vsf_heap_free(thread);

    if (thread == thread_main) {
        vsf_protect_t orig = vsf_protect_sched();
            vsf_dlist_remove(vsf_linux_process_t, process_node, &__vsf_linux.process_list, process);
        vsf_unprotect_sched(orig);

        if (process->thread_pending != NULL) {
            vsf_eda_post_evt(&process->thread_pending->use_as__vsf_eda_t, VSF_EVT_USER);
        }
        if (process->working_dir != NULL) {
            free(process->working_dir);
        }
        free(process);
    }
}

#if defined(__WIN__) && defined(__CPU_X64__)
intptr_t execv(const char *pathname, char const* const* argv)
#else
int execv(const char *pathname, char const* const* argv)
#endif
{
    char fullpath[MAX_PATH];
    if (vsf_linux_generate_path(fullpath, sizeof(fullpath), NULL, (char *)pathname)) {
        return -1;
    }

    // fd will be closed after entry return
    vsf_linux_main_entry_t entry;
    int fd = vsf_linux_fs_get_executable(fullpath, &entry);
    if (fd < 0) {
        return -1;
    }

    vsf_linux_process_t *process = vsf_linux_get_cur_process();
    vsf_linux_process_ctx_t *ctx = &process->ctx;
    vsf_linux_thread_t *thread;

    ctx->arg.argc = 0;
    while ((*argv != NULL) && (ctx->arg.argc <= VSF_LINUX_CFG_MAX_ARG_NUM)) {
        ctx->arg.argv[ctx->arg.argc++] = *argv++;
    }
    ctx->entry = entry;

    vsf_dlist_peek_head(vsf_linux_thread_t, thread_node, &process->thread_list, thread);
    vsf_eda_post_evt(&thread->use_as__vsf_eda_t, VSF_EVT_INIT);
    vsf_thread_wfe(VSF_EVT_INVALID);
    return 0;
}

#if defined(__WIN__) && defined(__CPU_X64__)
intptr_t execl(const char *pathname, const char *arg, ...)
#else
int execl(const char *pathname, const char *arg, ...)
#endif
{
    char fullpath[MAX_PATH];
    if (vsf_linux_generate_path(fullpath, sizeof(fullpath), NULL, (char *)pathname)) {
        return -1;
    }

    // fd will be closed after entry return
    vsf_linux_main_entry_t entry;
    int fd = vsf_linux_fs_get_executable(fullpath, &entry);
    if (fd < 0) {
        return -1;
    }

    vsf_linux_process_t *process = vsf_linux_get_cur_process();
    vsf_linux_process_ctx_t *ctx = &process->ctx;
    vsf_linux_thread_t *thread;
    const char *args;
    va_list ap;

    ctx->arg.argc = 1;
    ctx->arg.argv[0] = arg;
    va_start(ap, arg);
        args = va_arg(ap, const char *);
        while ((args != NULL) && (ctx->arg.argc <= VSF_LINUX_CFG_MAX_ARG_NUM)) {
            ctx->arg.argv[ctx->arg.argc++] = args;
            args = va_arg(ap, const char *);
        }
    va_end(ap);
    ctx->entry = entry;

    vsf_dlist_peek_head(vsf_linux_thread_t, thread_node, &process->thread_list, thread);
    vsf_eda_post_evt(&thread->use_as__vsf_eda_t, VSF_EVT_INIT);
    vsf_thread_wfe(VSF_EVT_INVALID);
    return 0;
}

int system(const char * cmd)
{
    VSF_LINUX_ASSERT(false);
    return 0;
}

long sysconf(int name)
{
    switch (name) {
    case _SC_PAGESIZE:      return 256;
    }
    return 0;
}

char *realpath(const char *path, char *resolved_path)
{
    return NULL;
}

// pipe
static int __vsf_linux_pipe_fcntl(vsf_linux_fd_t *sfd, int cmd, long arg)
{
    return 0;
}

static ssize_t __vsf_linux_pipe_read(vsf_linux_fd_t *sfd_rx, void *buf, size_t count)
{
    vsf_linux_pipe_rx_priv_t *priv_rx = (vsf_linux_pipe_rx_priv_t *)sfd_rx->priv;
    vsf_linux_pipe_buffer_t *buffer;
    ssize_t read_cnt = 0, cur_size;

    vsf_trig_t trig;
    vsf_linux_fd_trigger_init(&trig);

    vsf_protect_t orig;
    while (true) {
        orig = vsf_protect_sched();
        while (!vsf_slist_queue_is_empty(&priv_rx->buffer_queue)) {
            vsf_slist_queue_peek(vsf_linux_pipe_buffer_t, buffer_node, &priv_rx->buffer_queue, buffer);
            vsf_unprotect_sched(orig);

            cur_size = buffer->size - buffer->pos;
            cur_size = min(cur_size, count);
            memcpy(buf, &buffer[1], cur_size);
            buf = (uint8_t *)buf + cur_size;
            count -= cur_size;

            orig = vsf_protect_sched();
            if (!count) {
                break;
            }
        }

        if (!read_cnt) {
            vsf_linux_fd_rx_pend(sfd_rx, &trig, orig);
            continue;
        }
        vsf_unprotect_sched(orig);
        break;
    }
    return read_cnt;
}

static ssize_t __vsf_linux_pipe_write(vsf_linux_fd_t *sfd_tx, const void *buf, size_t count)
{
    vsf_linux_pipe_tx_priv_t *priv_tx = (vsf_linux_pipe_tx_priv_t *)sfd_tx->priv;
    vsf_linux_fd_t *sfd_rx = vsf_linux_get_fd(priv_tx->fd_rx);
    vsf_linux_pipe_rx_priv_t *priv_rx = (vsf_linux_pipe_rx_priv_t *)sfd_rx->priv;

    vsf_linux_pipe_buffer_t *buffer = malloc(sizeof(*buffer) + count);
    if (NULL == buffer) {
        return -1;
    }
    vsf_slist_init_node(vsf_linux_pipe_buffer_t, buffer_node, buffer);
    buffer->pos = 0;
    buffer->size = count;

    vsf_protect_t orig = vsf_protect_sched();
    bool is_empty = vsf_slist_queue_is_empty(&priv_rx->buffer_queue);
    vsf_slist_queue_enqueue(vsf_linux_pipe_buffer_t, buffer_node, &priv_rx->buffer_queue, buffer);
    if (is_empty) {
        vsf_linux_fd_rx_trigger(sfd_rx, orig);
    } else {
        vsf_unprotect_sched(orig);
    }
    return count;
}

static int __vsf_linux_pipe_close(vsf_linux_fd_t *sfd)
{
    if (&__vsf_linux_pipe_rx_fdop == sfd->op) {
        vsf_linux_pipe_rx_priv_t *priv_rx = (vsf_linux_pipe_rx_priv_t *)sfd->priv;
        vsf_linux_pipe_buffer_t *buffer;

        vsf_protect_t orig = vsf_protect_sched();
        while (!vsf_slist_queue_is_empty(&priv_rx->buffer_queue)) {
            vsf_slist_queue_dequeue(vsf_linux_pipe_buffer_t, buffer_node, &priv_rx->buffer_queue, buffer);
            vsf_unprotect_sched(orig);
            free(buffer);
            orig = vsf_protect_sched();
        }
        vsf_unprotect_sched(orig);
    }
    return 0;
}

#if __IS_COMPILER_IAR__
//! transfer of control bypasses initialization
#   pragma diag_suppress=pe546
#endif

int pipe(int pipefd[2])
{
    vsf_linux_fd_t *sfd_rx = NULL, *sfd_tx = NULL;

    pipefd[0] = vsf_linux_create_fd(&sfd_rx, &__vsf_linux_pipe_rx_fdop);
    pipefd[1] = vsf_linux_create_fd(&sfd_tx, &__vsf_linux_pipe_tx_fdop);
    if ((pipefd[0] < 0) || (pipefd[1] < 0)) {
        goto hell;
    }

    vsf_linux_pipe_rx_priv_t *priv_rx = (vsf_linux_pipe_rx_priv_t *)sfd_rx->priv;
    vsf_linux_pipe_tx_priv_t *priv_tx = (vsf_linux_pipe_tx_priv_t *)sfd_tx->priv;

    priv_rx->fd_tx = sfd_tx->fd;
    vsf_slist_queue_init(&priv_rx->buffer_queue);
    priv_tx->fd_rx = sfd_rx->fd;

    return 0;

hell:
    if (pipefd[0] >= 0) {
        close(pipefd[0]);
    }
    if (pipefd[1] >= 0) {
        close(pipefd[1]);
    }
    return -1;
}

int kill(pid_t pid, int sig)
{
#if VSF_LINUX_CFG_SUPPORT_SIG == ENABLED
    vsf_linux_process_t *process = vsf_linux_get_process(pid);
    if (process != NULL) {
        if (!process->id.pid) {
            return -1;
        }

        vsf_protect_t orig = vsf_protect_sched();
            process->sig.pending.sig[0] |= 1 << sig;
        vsf_unprotect_sched(orig);

        vsf_linux_thread_t *thread;
        vsf_dlist_peek_head(vsf_linux_thread_t, thread_node, &__vsf_linux.kernel_process->thread_list, thread);
        // TODO: avoid posting event/message to thread,
        //  if the thread is not waiting for the dedicated event/message
        vsf_eda_post_msg(&thread->use_as__vsf_eda_t, process);
        return 0;
    }
#endif
    return -1;
}

#if !defined(__WIN__) || VSF_LINUX_CFG_WRAPPER == ENABLED
// conflicts with signal in ucrt, need VSF_LINUX_CFG_WRAPPER
sighandler_t signal(int signum, sighandler_t handler)
{
#if VSF_LINUX_CFG_SUPPORT_SIG == ENABLED
    // not supported yet
#else
    return NULL;
#endif
}
#endif

unsigned int alarm(unsigned int seconds)
{
    VSF_LINUX_ASSERT(false);
    return -1;
}

pid_t waitpid(pid_t pid, int *status, int options)
{
    if (pid <= 0) {
        VSF_LINUX_ASSERT(false);
        return -1;
    }

    vsf_linux_process_t *process = vsf_linux_get_process(pid);
    if (NULL == process) {
        return -1;
    }

    process->thread_pending = vsf_linux_get_cur_thread();
    vsf_thread_wfe(VSF_EVT_USER);
    return pid;
}

pid_t getpid(void)
{
    return vsf_linux_get_cur_process()->id.pid;
}

pid_t getppid(void)
{
    return vsf_linux_get_cur_process()->id.ppid;
}

static vk_file_t * __vsf_linux_fs_get_file(const char *pathname)
{
    vk_file_t *file;

    vk_file_open(NULL, pathname, 0, &file);
    return file;
}

static void __vsf_linux_fs_close_do(vk_file_t *file)
{
    vk_file_close(file);
}

static int __vsf_linux_fs_fcntl(vsf_linux_fd_t *sfd, int cmd, long arg)
{
    return 0;
}

static ssize_t __vsf_linux_fs_read(vsf_linux_fd_t *sfd, void *buf, size_t count)
{
    vsf_linux_fs_priv_t *priv = (vsf_linux_fs_priv_t *)sfd->priv;
    vk_file_t *file = priv->file;
    ssize_t result = 0;
    int32_t rsize;

    while (count > 0) {
        vk_file_read(file, priv->pos, count, (uint8_t *)buf);
        rsize = (int32_t)vsf_eda_get_return_value();
        if (rsize < 0) {
            return -1;
        } else if (!rsize) {
            break;
        }

        count -= rsize;
        result += rsize;
        priv->pos += rsize;
        buf = (uint8_t *)buf + rsize;
    }
    return result;
}

static ssize_t __vsf_linux_fs_write(vsf_linux_fd_t *sfd, const void *buf, size_t count)
{
    vsf_linux_fs_priv_t *priv = (vsf_linux_fs_priv_t *)sfd->priv;
    vk_file_t *file = priv->file;
    ssize_t result = 0;
    int32_t wsize;

    while (count > 0) {
        vk_file_write(file, priv->pos, count, (uint8_t *)buf);
        wsize = (int32_t)vsf_eda_get_return_value();
        if (wsize < 0) {
            return -1;
        } else if (!wsize) {
            break;
        }

        count -= wsize;
        result += wsize;
        priv->pos += wsize;
        buf = (uint8_t *)buf + wsize;
    }
    return result;
}

static int __vsf_linux_fs_close(vsf_linux_fd_t *sfd)
{
    vsf_linux_fs_priv_t *priv = (vsf_linux_fs_priv_t *)sfd->priv;
    __vsf_linux_fs_close_do(priv->file);
    return 0;
}

static int __vsf_linux_stream_fcntl(vsf_linux_fd_t *sfd, int cmd, long arg)
{
    VSF_LINUX_ASSERT(false);
    return 0;
}

static void __vsf_linux_stream_evthandler(void *param, vsf_stream_evt_t evt)
{
    vsf_linux_thread_t *thread = param;
    switch (evt) {
    case VSF_STREAM_ON_RX:
        vsf_stream_disconnect_rx(thread->pending.stream);
        thread->pending.stream = NULL;
        vsf_eda_post_evt(&thread->use_as__vsf_eda_t, VSF_EVT_USER);
        break;
    case VSF_STREAM_ON_TX:
        vsf_stream_disconnect_tx(thread->pending.stream);
        thread->pending.stream = NULL;
        vsf_eda_post_evt(&thread->use_as__vsf_eda_t, VSF_EVT_USER);
        break;
    }
}

static ssize_t __vsf_linux_stream_read(vsf_linux_fd_t *sfd, void *buf, size_t count)
{
    vsf_linux_thread_t *thread = vsf_linux_get_cur_thread();
    vsf_stream_t *stream = ((vsf_linux_stream_priv_t *)sfd->priv)->stream;
    uint_fast32_t size = count, cursize;

    while (size > 0) {
        vsf_protect_t orig = vsf_protect_sched();
        if (!vsf_stream_get_data_size(stream)) {
            stream->rx.evthandler = __vsf_linux_stream_evthandler;
            stream->rx.param = thread;
            thread->pending.stream = stream;
            vsf_stream_connect_rx(stream);
            vsf_unprotect_sched(orig);

            vsf_thread_wfe(VSF_EVT_USER);
        } else {
            vsf_unprotect_sched(orig);
        }

        cursize = vsf_stream_read(stream, buf, size);
        if (sfd->fd == STDIN_FILENO) {
            static int skip_echo = 0;
            char ch;
            for (uint_fast32_t i = 0; i < cursize; i++) {
                ch = ((char *)buf)[i];
                switch (ch) {
                case '\033':skip_echo = 2;                      break;
                case '\r':  write(STDOUT_FILENO, "\r\n", 2);    break;
                case 0x7F:
                case '\b':  write(STDOUT_FILENO, "\b \b", 3);   break;
                default:
                    if (skip_echo) {
                        skip_echo--;
                        break;
                    }
                    write(STDOUT_FILENO, &ch, 1);
                    break;
                }
            }
        }

        size -= cursize;
        buf = (uint8_t *)buf + cursize;
    }
    return count;
}

static ssize_t __vsf_linux_stream_write(vsf_linux_fd_t *sfd, const void *buf, size_t count)
{
    vsf_linux_thread_t *thread = vsf_linux_get_cur_thread();
    vsf_stream_t *stream = ((vsf_linux_stream_priv_t *)sfd->priv)->stream;
    uint_fast32_t size = count, cursize;

    while (size > 0) {
        vsf_protect_t orig = vsf_protect_sched();
        if (!vsf_stream_get_free_size(stream)) {
            stream->tx.evthandler = __vsf_linux_stream_evthandler;
            stream->tx.param = thread;
            thread->pending.stream = stream;
            vsf_stream_connect_tx(stream);
            vsf_unprotect_sched(orig);

            vsf_thread_wfe(VSF_EVT_USER);
        } else {
            vsf_unprotect_sched(orig);
        }

        cursize = vsf_stream_write(stream, (uint8_t *)buf, size);
        size -= cursize;
        buf = (uint8_t *)buf + cursize;
    }
    return count;
}

static int __vsf_linux_stream_close(vsf_linux_fd_t *sfd)
{
    return 0;
}

vsf_linux_fd_t * vsf_linux_get_fd(int fd)
{
    vsf_dlist_t *fd_list = &vsf_linux_get_cur_process()->fd_list;
    vsf_protect_t orig = vsf_protect_sched();
    __vsf_dlist_foreach_unsafe(vsf_linux_fd_t, fd_node, fd_list) {
        if (_->fd == fd) {
            vsf_unprotect_sched(orig);
            return _;
        }
    }
    vsf_unprotect_sched(orig);
    return NULL;
}

int vsf_linux_create_fd(vsf_linux_fd_t **sfd, const vsf_linux_fd_op_t *op)
{
    vsf_linux_process_t *process = vsf_linux_get_cur_process();
    int priv_size = (op != NULL) ? op->priv_size : 0;
    vsf_linux_fd_t *new_sfd = calloc(1, sizeof(vsf_linux_fd_t) + priv_size);
    if (!new_sfd) {
        errno = ENOMEM;
        return -1;
    }

    new_sfd->op = op;
    new_sfd->rxpend = new_sfd->txpend = NULL;
    new_sfd->fd = process->cur_fd++;

    vsf_protect_t orig = vsf_protect_sched();
        vsf_dlist_add_to_tail(vsf_linux_fd_t, fd_node, &process->fd_list, new_sfd);
    vsf_unprotect_sched(orig);

    if (sfd != NULL) {
        *sfd = new_sfd;
    }
    return new_sfd->fd;
}

void vsf_linux_delete_fd(int fd)
{
    vsf_linux_fd_t *sfd = vsf_linux_get_fd(fd);
    vsf_linux_process_t *process = vsf_linux_get_cur_process();

    vsf_protect_t orig = vsf_protect_sched();
        vsf_dlist_remove(vsf_linux_fd_t, fd_node, &process->fd_list, sfd);
    vsf_unprotect_sched(orig);
    free(sfd);
}

void vsf_linux_fd_trigger_init(vsf_trig_t *trig)
{
    vsf_eda_trig_init(trig, false, true);
}

int vsf_linux_fd_tx_pend(vsf_linux_fd_t *sfd, vsf_trig_t *trig, vsf_protect_t orig)
{
    if (sfd->txevt) {
        sfd->txevt = false;
        vsf_unprotect_sched(orig);
    } else {
        sfd->txpend = trig;
        vsf_unprotect_sched(orig);
        vsf_thread_trig_pend(trig, -1);
    }
    return 0;
}

int vsf_linux_fd_rx_pend(vsf_linux_fd_t *sfd, vsf_trig_t *trig, vsf_protect_t orig)
{
    if (sfd->rxevt) {
        sfd->rxevt = false;
        vsf_unprotect_sched(orig);
    } else {
        sfd->rxpend = trig;
        vsf_unprotect_sched(orig);
        vsf_thread_trig_pend(trig, -1);
    }
    return 0;
}

// vsf_linux_fd_xx_trigger MUST be called scheduler protected
int vsf_linux_fd_tx_trigger(vsf_linux_fd_t *sfd, vsf_protect_t orig)
{
    if (sfd->txpend != NULL) {
        vsf_unprotect_sched(orig);
        vsf_trig_t *trig = sfd->txpend;
        sfd->txpend = NULL;
        vsf_eda_trig_set(trig);
    } else {
        sfd->txevt = true;
        vsf_unprotect_sched(orig);
    }
    return 0;
}

int vsf_linux_fd_rx_trigger(vsf_linux_fd_t *sfd, vsf_protect_t orig)
{
    if (sfd->rxpend != NULL) {
        vsf_unprotect_sched(orig);
        vsf_trig_t *trig = sfd->rxpend;
        sfd->rxpend = NULL;
        vsf_eda_trig_set(trig);
    } else {
        sfd->rxevt = true;
        vsf_unprotect_sched(orig);
    }
    return 0;
}

int __vsf_linux_poll_tick(struct pollfd *fds, nfds_t nfds, vsf_timeout_tick_t timeout)
{
    vsf_protect_t orig;
    vsf_linux_fd_t *sfd;
    int ret = 0;
    nfds_t i;
    vsf_trig_t trig;

    vsf_linux_fd_trigger_init(&trig);
    while (1) {
        orig = vsf_protect_sched();
        for (i = 0; i < nfds; i++) {
            sfd = vsf_linux_get_fd(fds[i].fd);
            if (sfd->rxevt || sfd->txevt) {
                if ((fds[i].events & POLLIN) && sfd->rxevt) {
                    fds[i].revents |= POLLIN;
                }
                if ((fds[i].events & POLLOUT) && sfd->txevt) {
                    fds[i].revents |= POLLOUT;
                }
                if (fds[i].revents) {
                    ret++;
                }
            }
        }
        if (ret || (0 == timeout)) {
            vsf_unprotect_sched(orig);
            return ret;
        }

        for (i = 0; i < nfds; i++) {
            sfd = vsf_linux_get_fd(fds[i].fd);
            if (fds[i].events & POLLIN) {
                sfd->rxpend = &trig;
            }
            if (fds[i].events & POLLOUT) {
                sfd->txpend = &trig;
            }
        }
        vsf_unprotect_sched(orig);

        vsf_sync_reason_t r = vsf_thread_trig_pend(&trig, timeout);
        if (VSF_SYNC_TIMEOUT == r) {
            return 0;
        } else if (r != VSF_SYNC_GET) {
            return -1;
        }

        for (i = 0; i < nfds; i++) {
            sfd = vsf_linux_get_fd(fds[i].fd);
            orig = vsf_protect_sched();
                if (fds[i].events & POLLIN) {
                    if (NULL == sfd->rxpend) {
                        sfd->rxevt = true;
                    } else {
                        sfd->rxpend = NULL;
                    }
                }
                if (fds[i].events & POLLOUT) {
                    if (NULL == sfd->txpend) {
                        sfd->txevt = true;
                    } else {
                        sfd->txpend = NULL;
                    }
                }
            vsf_unprotect_sched(orig);
        }
    }
    return 0;
}

int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *execeptfds, struct timeval *timeout)
{
    int fd_num = 0;

    if (execeptfds != NULL) {
        VSF_LINUX_ASSERT(false);
        return -1;
    }

    fd_set mask;
    FD_ZERO(&mask);
    for (int i = 0; i < nfds; i++) {
        if (    ((readfds != NULL) && (FD_ISSET(i, readfds)))
            ||  ((writefds != NULL) && (FD_ISSET(i, writefds)))) {
            FD_SET(i, &mask);
            fd_num++;
        }
    }

    struct pollfd *fds = NULL;
    if (fd_num > 0) {
        fds = malloc(fd_num * sizeof(struct pollfd));
        if (NULL == fds) {
            return -1;
        }
        memset(fds, 0, fd_num * sizeof(struct pollfd));

        for (int i = 0, idx = 0; i < nfds; i++) {
            if (FD_ISSET(i, &mask)) {
                fds[idx].fd = i;
                if ((readfds != NULL) && (FD_ISSET(i, readfds))) {
                    fds[idx].events |= POLLIN;
                }
                if ((writefds != NULL) && (FD_ISSET(i, writefds))) {
                    fds[idx].events |= POLLOUT;
                }
                idx++;
            }
        }

        if (readfds != NULL) {
            FD_ZERO(readfds);
        }
        if (writefds != NULL) {
            FD_ZERO(writefds);
        }
    }

    vsf_timeout_tick_t timeout_tick = -1;
    if (timeout != NULL) {
        timeout_tick = vsf_systimer_ms_to_tick(1000 * timeout->tv_sec);
        timeout_tick += vsf_systimer_us_to_tick(timeout->tv_usec);
    }
    int ret = __vsf_linux_poll_tick(fds, fd_num, timeout_tick);
    if (ret > 0) {
        for (int i = 0; i < fd_num; i++) {
            if (fds[i].revents & POLLIN) {
                FD_SET(fds[i].fd, readfds);
            }
            if (fds[i].revents & POLLOUT) {
                FD_SET(fds[i].fd, writefds);
            }
        }
    }
    if (fds != NULL) {
        free(fds);
    }
    return ret;
}

int poll(struct pollfd *fds, nfds_t nfds, int timeout)
{
    vsf_timeout_tick_t timeout_tick = (timeout < 0) ? -1 : vsf_systimer_ms_to_tick(timeout);
    return __vsf_linux_poll_tick(fds, nfds, timeout_tick);
}

int ppoll(struct pollfd *fds, nfds_t nfds, const struct timespec *timeout_ts, const sigset_t *sigmask)
{
    sigset_t origmask;
    int timeout, ready;

    timeout = (timeout_ts == NULL) ? -1 : (timeout_ts->tv_sec * 1000 + timeout_ts->tv_nsec / 1000000);
    sigprocmask(SIG_SETMASK, sigmask, &origmask);
    ready = poll(fds, nfds, timeout);
    sigprocmask(SIG_SETMASK, &origmask, NULL);
    return ready;
}

int sigprocmask(int how, const sigset_t *set, sigset_t *oldset)
{
    vsf_linux_process_t *process = vsf_linux_get_cur_process();

    if (oldset != NULL) {
        *oldset = process->sig.mask;
    }

    vsf_protect_t orig = vsf_protect_sched();
        switch (how) {
        case SIG_BLOCK:     sigaddsetmask(&process->sig.mask, set->sig[0]); break;
        case SIG_UNBLOCK:   sigdelsetmask(&process->sig.mask, set->sig[0]); break;
        case SIG_SETMASK:   process->sig.mask = *set;                       break;
        }
    vsf_unprotect_sched(orig);
    return 0;
}

static int __vsf_linux_fs_create(const char* pathname, mode_t mode, vk_file_attr_t attr, uint_fast64_t size)
{
    int err = 0;
    char *path = strdup(pathname), *name = NULL, *name_tmp;
    if (!path) {
        errno = ENOENT;
        return -1;
    }

    name_tmp = vk_file_getfilename((char *)pathname);
    path[name_tmp - pathname] = '\0';
    vk_file_t *dir = __vsf_linux_fs_get_file(path);
    if (!dir) {
        err = -1;
        goto do_return;
    }

    // TODO: name is allocated, so if created file is removed
    //  how to free name?
    name = strdup(name_tmp);
    vk_file_create(dir, name, attr, size);
    if (VSF_ERR_NONE != (vsf_err_t)vsf_eda_get_return_value()) {
        err = -1;
    }
    __vsf_linux_fs_close_do(dir);
    if (!err) {
        err = open(pathname, 0);
    }

do_return:
    if ((err < 0) && (name != NULL)) {
        free(name);
    }
    free(path);
    return err;
}

int mkdir(const char* pathname, mode_t mode)
{
    char fullpath[MAX_PATH];
    if (vsf_linux_generate_path(fullpath, sizeof(fullpath), NULL, (char *)pathname)) {
        return -1;
    }

    int fd = __vsf_linux_fs_create(fullpath, mode, VSF_FILE_ATTR_DIRECTORY, 0);
    if (fd >= 0) {
        close(fd);
        fd = 0;
    }
    return fd;
}

int vsf_linux_chdir(vsf_linux_process_t *process, char *pathname)
{
    VSF_LINUX_ASSERT(process != NULL);
    char fullpath[MAX_PATH];
    if (vsf_linux_generate_path(fullpath, sizeof(fullpath), NULL, (char *)pathname)) {
        return -1;
    }

    int len = strlen(fullpath);
    if (fullpath[len - 1] != '/') {
        VSF_LINUX_ASSERT(len + 1 < MAX_PATH);
        fullpath[len] = '/';
        fullpath[len + 1] = '\0';
    }

    vk_file_t *file = __vsf_linux_fs_get_file(fullpath);
    if (NULL == file) {
        return -1;
    }
    vk_file_attr_t attr = file->attr;
    __vsf_linux_fs_close_do(file);
    if (!(attr & VSF_FILE_ATTR_DIRECTORY)) {
        return -1;
    }

    free(process->working_dir);
    process->working_dir = strdup(fullpath);
    VSF_LINUX_ASSERT(process->working_dir != NULL);
    return 0;
}

int chdir(const char *pathname)
{
    vsf_linux_process_t *process = vsf_linux_get_cur_process();
    return vsf_linux_chdir(process, (char *)pathname);
}

char * getcwd(char *buffer, size_t maxlen)
{
    vsf_linux_process_t *process = vsf_linux_get_cur_process();
    VSF_LINUX_ASSERT(process != NULL);
    if (strlen(process->working_dir) >= maxlen) {
        errno = ERANGE;
        return NULL;
    }
    strcpy(buffer, process->working_dir);
    return buffer;
}

int creat(const char *pathname, mode_t mode)
{
    char fullpath[MAX_PATH];
    if (vsf_linux_generate_path(fullpath, sizeof(fullpath), NULL, (char *)pathname)) {
        return -1;
    }
    return __vsf_linux_fs_create(fullpath, mode, 0, 0);
}

int open(const char *pathname, int flags, ...)
{
    vk_file_t *file;
    vsf_linux_fd_t *sfd;
    char fullpath[MAX_PATH];
    int fd;

    if (vsf_linux_generate_path(fullpath, sizeof(fullpath), NULL, (char *)pathname)) {
        return -1;
    }

    file = __vsf_linux_fs_get_file(fullpath);
    if (!file) {
        if (flags & O_CREAT) {
            va_list ap;
            mode_t mode;

            va_start(ap, flags);
                mode = va_arg(ap, mode_t);
            va_end(ap);

            return creat(fullpath, mode);
        }
        return -1;
    }

    fd = vsf_linux_create_fd(&sfd, &__vsf_linux_fs_fdop);
    if (fd < 0) {
        __vsf_linux_fs_close_do(file);
    } else {
        vsf_linux_fs_priv_t *priv = (vsf_linux_fs_priv_t *)sfd->priv;
        sfd->flags = flags;
        priv->file = file;
        if ((flags & O_TRUNC) && !(file->attr & VSF_FILE_ATTR_DIRECTORY)) {
            // todo:
        }
    }
    return fd;
}

int close(int fd)
{
    vsf_linux_fd_t *sfd = vsf_linux_get_fd(fd);
    if (!sfd) { return -1; }

    int err = sfd->op->fn_close(sfd);
    vsf_linux_delete_fd(fd);
    return err;
}

int fcntl(int fd, int cmd, ...)
{
    vsf_linux_fd_t *sfd = vsf_linux_get_fd(fd);
    va_list ap;
    long arg;

    if (!sfd) { return -1; }
    va_start(ap, cmd);
        arg = va_arg(ap, long);
    va_end(ap);
    return sfd->op->fn_fcntl(sfd, cmd, arg);
}

ssize_t read(int fd, void *buf, size_t count)
{
    vsf_linux_fd_t *sfd = vsf_linux_get_fd(fd);
    if (!sfd || (sfd->flags & O_WRONLY)) { return -1; }
    return sfd->op->fn_read(sfd, buf, count);
}

ssize_t write(int fd, const void *buf, size_t count)
{
    vsf_linux_fd_t *sfd = vsf_linux_get_fd(fd);
    if (!sfd || (sfd->flags & O_RDONLY)) { return -1; }
    return sfd->op->fn_write(sfd, buf, count);
}

off_t lseek(int fd, off_t offset, int whence)
{
    vsf_linux_fd_t *sfd = vsf_linux_get_fd(fd);
    VSF_LINUX_ASSERT(sfd->op == &__vsf_linux_fs_fdop);
    vsf_linux_fs_priv_t *priv = (vsf_linux_fs_priv_t *)sfd->priv;
    uint_fast64_t new_pos;

    switch (whence) {
    case SEEK_SET:  new_pos = 0;                break;
    case SEEK_CUR:  new_pos = priv->pos;        break;
    case SEEK_END:  new_pos = priv->file->size; break;
    default:        return -1;
    }

    new_pos += offset;
    if (new_pos > priv->file->size) {
        return -1;
    }
    priv->pos = new_pos;
    return (off_t)new_pos;
}

int fstat(int fd, struct stat *buf)
{
    vsf_linux_fd_t *sfd = vsf_linux_get_fd(fd);

    memset(buf, 0, sizeof(*buf));
    if (&__vsf_linux_fs_fdop == sfd->op) {
        vsf_linux_fs_priv_t *priv = (vsf_linux_fs_priv_t *)sfd->priv;
        vk_file_t *file = priv->file;

        buf->st_mode = file->attr;
        if (!(file->attr & S_IFDIR)) {
            buf->st_mode |= S_IFREG;
        }
        buf->st_size = file->size;
        return 0;
    } else {
        return -1;
    }
}

int stat(const char *pathname, struct stat *buf)
{
    int fd = open(pathname, 0);
    if (fd < 0) { return -1; }

    int ret = fstat(fd, buf);
    close(fd);
    return ret;
}

int access(const char *pathname, int mode)
{
    int fd = open(pathname, mode);
    if (fd < 0) { return -1; }

    int ret = 0;
    vk_vfs_file_t *vfs_file = vsf_linux_fs_get_vfs(fd);
    if (    ((mode & R_OK) && !(vfs_file->attr & VSF_FILE_ATTR_READ))
        ||  ((mode & W_OK) && !(vfs_file->attr & VSF_FILE_ATTR_WRITE))
        ||  ((mode & X_OK) && !(vfs_file->attr & VSF_FILE_ATTR_EXECUTE))) {
        ret = -1;
    }

    close(fd);
    return ret;
}

int link(const char *oldpath, const char *newpath)
{
    return -1;
}

int unlink(const char *pathname)
{
    char fullpath[MAX_PATH];
    if (vsf_linux_generate_path(fullpath, sizeof(fullpath), NULL, (char *)pathname)) {
        return -1;
    }

    vk_file_t *file = __vsf_linux_fs_get_file(fullpath), *dir;
    if (!file) {
        errno = ENOENT;
        return -1;
    }

    pathname = &fullpath[strlen(fullpath) - strlen(file->name)];
    dir = vk_file_get_parent(file);
    __vsf_linux_fs_close_do(file);

    vk_file_unlink(dir, pathname);

    vsf_err_t err = (vsf_err_t)vsf_eda_get_return_value();
    __vsf_linux_fs_close_do(dir);
    return VSF_ERR_NONE == err ? 0 : -1;
}

DIR * opendir(const char *name)
{
    int fd = open(name, 0);
    if (fd < 0) {
        return NULL;
    }

    vsf_linux_fd_t *sfd = vsf_linux_get_fd(fd);
    vsf_linux_fs_priv_t *priv = (vsf_linux_fs_priv_t *)sfd->priv;
    if (!(priv->file->attr & VSF_FILE_ATTR_DIRECTORY)) {
        close(fd);
        sfd = NULL;
    }
    return sfd;
}

struct dirent * readdir(DIR *dir)
{
    vsf_linux_fs_priv_t *priv = (vsf_linux_fs_priv_t *)dir->priv;
    vk_file_t *file = priv->file, *child;

    if (priv->child != NULL) {
        vk_file_close(priv->child);
        priv->child = NULL;
    }

    vk_file_open(file, NULL, priv->dir.d_ino++, &priv->child);
    if (NULL == priv->child) {
        return NULL;
    }

    child = priv->child;
    priv->dir.d_name = child->name;
    priv->dir.d_reclen = vk_file_get_name_length(child);
    priv->dir.d_type = child->attr & VSF_FILE_ATTR_DIRECTORY ? DT_DIR :
                child->attr & VSF_FILE_ATTR_EXECUTE ? DT_EXE : DT_REG;
    return &priv->dir;
}

int closedir(DIR *dir)
{
    vsf_linux_fs_priv_t *priv = (vsf_linux_fs_priv_t *)dir->priv;

    if (priv->child != NULL) {
        vk_file_close(priv->child);
        priv->child = NULL;
    }
    return close(dir->fd);
}

int mount(const char *source, const char *target,
    const vk_fs_op_t *filesystem, unsigned long mountflags, const void *data)
{
    int fd = open(target, 0);
    if (fd < 0) { return fd; }

    vsf_linux_fd_t *sfd = vsf_linux_get_fd(fd);
    vsf_linux_fs_priv_t *priv = (vsf_linux_fs_priv_t *)sfd->priv;
    vk_file_t *dir = priv->file;
    vsf_err_t err;

    if (filesystem != NULL) {
        vk_fs_mount(dir, filesystem, (void *)data);
        err = (vsf_err_t)vsf_eda_get_return_value();
    } else {
#if VSF_FS_USE_MALFS == ENABLED
        vk_malfs_mounter_t mounter;
        mounter.dir = dir;
        mounter.mal = (vk_mal_t *)data;
        vk_malfs_mount_mbr(&mounter);
        err = mounter.err;
#else
        err = VSF_ERR_NOT_SUPPORT;
#endif
    }

    close(fd);
    if (VSF_ERR_NONE != err) {
        return -1;
    }
    return 0;
}

int umount(const char *target)
{
    int fd = open(target, 0);
    if (fd < 0) { return fd; }

    vsf_linux_fd_t *sfd = vsf_linux_get_fd(fd);
    vsf_linux_fs_priv_t *priv = (vsf_linux_fs_priv_t *)sfd->priv;
    vk_file_t *dir = priv->file;
    vk_fs_unmount(dir);
    close(fd);
    if (VSF_ERR_NONE != (vsf_err_t)vsf_eda_get_return_value()) {
        return -1;
    }
    return 0;
}

vk_vfs_file_t * vsf_linux_fs_get_vfs(int fd)
{
    vsf_linux_fd_t *sfd = vsf_linux_get_fd(fd);
    if ((NULL == sfd) || (sfd->op != &__vsf_linux_fs_fdop)) {
        return NULL;
    }

    vk_file_t *file = ((vsf_linux_fs_priv_t *)sfd->priv)->file;
    if ((file->fsop != &vk_vfs_op) || (file->attr & VSF_FILE_ATTR_DIRECTORY)) {
        return NULL;
    }
    return (vk_vfs_file_t *)file;
}

int vsf_linux_fs_get_executable(const char *pathname, vsf_linux_main_entry_t *entry)
{
    int fd = open(pathname, 0);
    if (fd < 0) {
        return -1;
    }

    vk_vfs_file_t *vfs_file = vsf_linux_fs_get_vfs(fd);
    if ((NULL == vfs_file) || !(vfs_file->attr & VSF_FILE_ATTR_EXECUTE)) {
        return -1;
    }

    // TODO: support other executable files?
    if (entry != NULL) {
        *entry = (vsf_linux_main_entry_t)vfs_file->f.param;
    }
    return fd;
}

int vsf_linux_fs_bind_target(int fd, void *target,
        vsf_param_eda_evthandler_t peda_read,
        vsf_param_eda_evthandler_t peda_write)
{
    vk_vfs_file_t *vfs_file = vsf_linux_fs_get_vfs(fd);
    if (NULL == vfs_file) {
        return -1;
    }

    vfs_file->f.param = target;
    vfs_file->f.callback.fn_read = peda_read;
    vfs_file->f.callback.fn_write = peda_write;
    return 0;
}

int vsf_linux_fs_bind_executable(int fd, vsf_linux_main_entry_t entry)
{
    int err = vsf_linux_fs_bind_target(fd, (void *)entry, NULL, NULL);
    if (!err) {
        vk_vfs_file_t *vfs_file = vsf_linux_fs_get_vfs(fd);
        vfs_file->attr |= VSF_FILE_ATTR_EXECUTE;
    }
    return err;
}

int tcgetattr(int fd, struct termios *termios)
{
    return 0;
}

int tcsetattr(int fd, int optional_actions, const struct termios *termios)
{
    return 0;
}

#if VSF_KERNEL_CFG_EDA_SUPPORT_TIMER == ENABLED
void usleep(int usec)
{
#   if VSF_KERNEL_CFG_TIMER_MODE == VSF_KERNEL_CFG_TIMER_MODE_TICKLESS
    vsf_teda_set_timer_us(usec);
#   else
    // us sleep is not available in non tickless mode
    VSF_LINUX_ASSERT(false);
#   endif
    vsf_thread_wfe(VSF_EVT_TIMER);
}

// TODO: wakeup after signal
unsigned sleep(unsigned sec)
{
    vsf_teda_set_timer_ms(sec * 1000);
    vsf_thread_wfe(VSF_EVT_TIMER);
    return 0;
}
#endif

// malloc.h
void * memalign(size_t alignment, size_t size)
{
    return vsf_heap_malloc_aligned(size, alignment);
}

// ipc.h
key_t ftok(const char *pathname, int id)
{
    VSF_LINUX_ASSERT(false);
    return -1;
}

// sys/time.h
int gettimeofday(struct timeval *tv, struct timezone *tz)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    tv->tv_sec = ts.tv_sec;
    tv->tv_usec = ts.tv_nsec / 1000;
    return 0;
}

#if VSF_LINUX_CFG_SHM_NUM > 0
// shm.h
int shmget(key_t key, size_t size, int shmflg)
{
    VSF_LINUX_ASSERT((IPC_PRIVATE == key) || (shmflg & IPC_CREAT));

    vsf_protect_t orig = vsf_protect_sched();
        key = vsf_bitmap_ffz(&__vsf_linux.shm.bitmap, VSF_LINUX_CFG_SHM_NUM);
        if (key >= 0) {
            vsf_bitmap_set(&__vsf_linux.shm.bitmap, key);
        }
    vsf_unprotect_sched(orig);

    if (key < 0) {
        return key;
    }

    vsf_linux_shm_mem_t *mem = &__vsf_linux.shm.mem[key++];
    mem->size = size;
    mem->key = key;
    mem->buffer = malloc(size);
    if (NULL == mem->buffer) {
        shmctl(key, IPC_RMID, NULL);
        return -1;
    }

    return key;
}

void * shmat(int shmid, const void *shmaddr, int shmflg)
{
    vsf_linux_shm_mem_t *mem = &__vsf_linux.shm.mem[shmid];
    return mem->buffer;
}

int shmdt(const void *shmaddr)
{
    return 0;
}

int shmctl(int shmid, int cmd, struct shmid_ds *buf)
{
    shmid--;
    VSF_LINUX_ASSERT(shmid < VSF_LINUX_CFG_SHM_NUM);

    vsf_linux_shm_mem_t *mem = &__vsf_linux.shm.mem[shmid];
    switch (cmd) {
    case IPC_STAT:
        memset(buf, 0, sizeof(*buf));
        buf->shm_segsz = mem->size;
        buf->shm_perm.key = mem->key;
        break;
    case IPC_SET:
        VSF_LINUX_ASSERT(false);
        break;
    case IPC_RMID: {
            if (mem->buffer != NULL) {
                free(mem->buffer);
                mem->buffer = NULL;
            }

            vsf_protect_t orig = vsf_protect_sched();
                vsf_bitmap_clear(&__vsf_linux.shm.bitmap, shmid);
            vsf_unprotect_sched(orig);
        }
        break;
    }
    return 0;
}
#endif      // VSF_LINUX_CFG_SHM_NUM

// sched
int sched_get_priority_max(int policy)
{
    return VSF_LINUX_CFG_PRIO_HIGHEST;
}

int sched_get_priority_min(int policy)
{
    return VSF_LINUX_CFG_PRIO_LOWEST;
}

int sched_yield(void)
{
    vsf_thread_yield();
    return 0;
}

#if __IS_COMPILER_GCC__
#   pragma GCC diagnostic pop
#endif

#endif      // VSF_USE_LINUX
