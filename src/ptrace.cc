/*
 Copyright (C) 2019 Andrew Sveikauskas

 Permission to use, copy, modify, and distribute this software for any
 purpose with or without fee is hereby granted, provided that the above
 copyright notice and this permission notice appear in all copies.
*/

#include <sys/types.h>
#include <sys/param.h>
#include <sys/ptrace.h>
#include <sys/user.h>
#include <sys/wait.h>

#include <unistd.h>
#include <fcntl.h>
#include <signal.h>

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <dbg/process.h>
#include <dbg/arch.h>
#include <dbg/misc.h>

#include <common/c++/new.h>
#include <common/misc.h>
#include <common/logger.h>

using dbg::addr_t;
using dbg::reg_t;
using dbg::FormatSignal;

#if defined(__linux__)
typedef enum __ptrace_request ptrace_op_t;
typedef uintptr_t ptrace_unit_t;
#else
typedef int ptrace_op_t;
typedef int ptrace_unit_t;
#endif

// If reading from the "user struct" is not present, use PT_GETREGS
//
#if !defined(PT_READ_U) && defined(PT_GETREGS) && !defined(USE_GETREGS)
#define USE_GETREGS
#endif

#if !defined(PT_IO) && defined(__linux__)
#define USE_PROC_MEM
#endif

#if defined(__linux__) && !defined(__sparc__)
#define GETREGS_REVERSED
#endif

namespace {

struct PtraceProcess : public dbg::Process
{
   pid_t pid;
#if defined(USE_GETREGS)
   bool registersDirty;
   reg_t registers;
#endif
   int pendingSignal;
   ptrace_op_t lastStep;
#if defined(USE_PROC_MEM)
   int memfd;
#endif

   PtraceProcess()
      : pid(-1), pendingSignal(0), lastStep(PT_STEP)
   {
      MarkRegistersDirty();

#if defined(USE_PROC_MEM)
      memfd = -1;
#endif
   }

   ~PtraceProcess()
   {
      ClearPid();
   }

#if defined(PT_IO)

   // Some *BSDs have a better memory copy interface than the original ptrace...
   //
   void
   MemoryOp(
      int op,
      addr_t addr,
      size_t len,
      void *buf,
      error *err
   )
   {
      struct ptrace_io_desc io_desc;

      memset(&io_desc, 0, sizeof(io_desc));

      switch (op)
      {
      case PT_READ_D:
         io_desc.piod_op = PIOD_READ_D;
         break;
      case PT_WRITE_D:
         io_desc.piod_op = PIOD_WRITE_D;
         break;
#if defined(PT_READ_U)
      case PT_READ_U:
         io_desc.piod_op = PIOD_READ_U;
         break;
      case PT_WRITE_U:
         io_desc.piod_op = PIOD_WRITE_U;
         break;
#endif
      default:
         ERROR_SET(err, unknown, "Unrecognized operation");
      }

      io_desc.piod_offs = (void*)addr;
      io_desc.piod_addr = buf;
      io_desc.piod_len = len;

      if (ptrace(PT_IO, pid, (caddr_t)&io_desc, 0))
         ERROR_SET(err, errno, errno);
   exit:;
   }

#else

   // The traditional interface requires us to do memory copies one int at a time.
   //
   void
   MemoryOp(
      ptrace_op_t op,
      addr_t addr,
      size_t len,
      void *buf,
      error *err
   )
   {
      bool writing = false;

      switch (op)
      {
      case PT_WRITE_D:
#if defined (PT_WRITE_U)
      case PT_WRITE_U:
#endif
         writing = true;
      case PT_READ_D:
#if defined (PT_READ_U)
      case PT_READ_U:
#endif
         break;
      default:
         ERROR_SET(err, unknown, "Unrecognized operation");
      }

#if defined(USE_PROC_MEM)
      if (memfd >= 0 && (op == PT_WRITE_D || op == PT_READ_D))
      {
         ssize_t r;

         if (writing)
            r = pwrite(memfd, buf, len, addr);
         else
         {
            r = pread(memfd, buf, len, addr);
            if (r >= 0 && r < len)
            {
               memset((char*)buf + r, 0, len - r);
            }
         }

         if (r < 0)
            ERROR_SET(err, errno, errno);

         goto exit;
      }
#endif

      while (len)
      {
         ptrace_unit_t i;
         size_t bytes = MIN(len, sizeof(i));

         if (writing)
         {
            // If there are some odd bytes to read, get them now so we don't
            // overwrite the bytes that follow.
            //
            if (bytes < sizeof(i))
            {
#if defined(PT_READ_U)
               ptrace_op_t op2 = (op == PT_WRITE_D) ? PT_READ_D : PT_READ_U;
#else
               ptrace_op_t op2 = PT_READ_D;
#endif
               i = ptrace(op2, pid, (void*)addr, 0);
            }

            // Copy that which needs to be written.
            //
            memcpy(&i, buf, bytes); 

            // Do the write.
            //
            if (ptrace(op, pid, (void*)addr, i))
               ERROR_SET(err, errno, errno);
         }
         else
         {
            // Do the read.
            //
            i = ptrace(op, pid, (void*)addr, 0);

            // Copy back to the buffer.
            //
            memcpy(buf, &i, bytes);
         }

         // Advance to the next word.
         //
         len -= bytes;
         addr += bytes;
         buf = ((char*)buf) + bytes;
      }

   exit:;
   }

   int
   GetBlockSize()
   {
#if defined(USE_PROC_MEM)
      if (memfd >= 0)
         return Process::GetBlockSize();
#endif
      return sizeof(ptrace_unit_t);
   }

#endif

   void
   ReadMemory(addr_t addr, int len, void *buf, error *err)
   {
      MemoryOp(PT_READ_D, addr, len, buf, err);
   }

   void
   WriteMemory(addr_t addr, int len, const void *buf, error *err)
   {
      MemoryOp(PT_WRITE_D, addr, len, (void*)buf, err);
   }

   void
   RegDeref(int regno, void **reg_offset, size_t *reg_size, error *err)
   {
      // Validate the register number...
      //
      if (regno < 0 || regno >= DBG_REGISTER_COUNT)
      {
         ERROR_SET(err, unknown, "Invalid register");
      }

      // Get the offset and size of this register in the struct...
      //
   #define SET_VARIABLES(reg)                                       \
      do                                                            \
      {                                                             \
         *reg_offset = &DBG_ACCESS_REG(reg);                        \
         *reg_size = sizeof(DBG_ACCESS_REG(reg));                   \
      } while (0)
      DBG_EVAL_REGISTER(regno, SET_VARIABLES);

   exit:;
   }

   void
   GetRegister(int regno, void *reg, error *err)
   {
      void *offset = nullptr;
      size_t len = 0;

      RegDeref(regno, &offset, &len, err);
      ERROR_CHECK(err);

#if defined(USE_GETREGS)
      LoadAllRegisters(err);
      ERROR_CHECK(err);

      // Copy into the caller's buffer...
      //
      memcpy(reg, ((char*)&registers) + (size_t)offset, len);
#elif defined(PT_READ_U)
      // Use "struct user"
      //
      MemoryOp(PT_READ_U, offset, len, reg, err);
#else
#error
#endif
   exit:;
   }

   void
   SetRegister(int regno, const void *reg, error *err)
   {
      void *offset = nullptr;
      size_t len = 0;

      RegDeref(regno, &offset, &len, err);
      ERROR_CHECK(err);

#if defined(USE_GETREGS)
      LoadAllRegisters(err);
      ERROR_CHECK(err);

      // Set the register...
      //
      memcpy(((char*)&registers) + (size_t)offset, reg, len);
      
      StoreAllRegisters(err);
      ERROR_CHECK(err);
#elif defined(PT_READ_U)
      MemoryOp(PT_WRITE_U, offset, len, (void*)reg, err);
#else
#error
#endif
   exit:;
   }

   void
   Wait(error *err)
   {
      Wait(true, err);
   }

   void
   Wait(bool block, error *err)
   {
      pid_t child;
      int status;
      char namebuf[32];
      int flags = 0;
      bool pgidSet = false;

      pendingSignal = 0;

      if (!block)
         flags |= WNOHANG;

   retry:
      child = waitpid(pid, &status, flags);
      if (!block && !child)
         goto exit;
      else if (child < 0)
         ERROR_SET(err, errno, errno);
      else if (WIFEXITED(status))
      {
         if (EventCallbacks.Get())
         {
            int code = WEXITSTATUS(status);

            EventCallbacks->OnMessage(err, "Exited with status %d\n", code);
            ERROR_CHECK(err);

            EventCallbacks->OnProcessExited(err);
            ERROR_CHECK(err);
         }

         ClearPid();
      }
      else if (WIFSIGNALED(status))
      {
         if (EventCallbacks.Get())
         {
            int sig = WTERMSIG(status);

            EventCallbacks->OnMessage(
               err,
               "Terminated due to signal %s\n",
               FormatSignal(namebuf, sizeof(namebuf), sig)
            );
            ERROR_CHECK(err);

            EventCallbacks->OnProcessExited(err);
            ERROR_CHECK(err);
         }

         ClearPid();
      }
      else if (WIFSTOPPED(status))
      {
         int sig = WSTOPSIG(status);
         if (sig != SIGTRAP)
         {
            switch (sig)
            {
            case SIGTTIN:
            case SIGTTOU:
               if (tcsetpgrp(0, getpgid(pid)))
                  ERROR_SET(err, errno, errno);
               pgidSet = true;
               // fall through ...
            case SIGCHLD:
               if (ptrace(lastStep, pid, (caddr_t)1, SIGCONT))
                  ERROR_SET(err, errno, errno);
               goto retry;
            case SIGINT:
            case SIGSTOP:
               break;
            default:
               pendingSignal = sig;
            }

            if (EventCallbacks.Get())
            {
               EventCallbacks->OnMessage(
                  err,
                  "Stopped due to signal %s\n",
                  FormatSignal(namebuf, sizeof(namebuf), sig)
               );
               ERROR_CHECK(err);

               EventCallbacks->OnSignal(sig, err);
               ERROR_CHECK(err);
            }
         }
         else if (lastStep == PT_CONTINUE)
         {
            // SIGTRAP after Go().  Likely breakpoint.
            //
            if (Cpu.Get())
            {
               Cpu->OnBreakpointBreak(this, err);
               ERROR_CHECK(err);
            }
         }
      }

      if (pgidSet && tcsetpgrp(0, getpgid(getpid())))
         ERROR_SET(err, errno, errno);

   exit:;
   }

   void
   Step(error *err)
   {
      int r = 0;

      MarkRegistersDirty();

      r = ptrace(lastStep=PT_STEP, pid, (caddr_t)1, pendingSignal);
      if (r)
         ERROR_SET(err, errno, errno);

      Wait(err);
      ERROR_CHECK(err);

   exit:;
   }

   void
   Go(error *err)
   {
      int r = 0;

      MarkRegistersDirty();

      r = ptrace(lastStep=PT_CONTINUE, pid, (caddr_t)1, pendingSignal);
      if (r)
         ERROR_SET(err, errno, errno);

      Wait(err);
      ERROR_CHECK(err);

   exit:;
   }

   void
   Interrupt(error *err)
   {
      if (pid < 0)
         goto exit;
      if (kill(pid, SIGINT))
         ERROR_SET(err, errno, errno);
      Wait(err);
      ERROR_CHECK(err);
   exit:;
   }

   void
   Detach(error *err)
   {
      if (ptrace(PT_DETACH, pid, (caddr_t)1, pendingSignal))
         ERROR_SET(err, errno, errno);

      ClearPid();
   exit:;
   }

   void
   Quit(error *err)
   {
      if (pid < 0)
         goto exit;
      if (ptrace(PT_KILL, pid, (caddr_t)1, 0))
         ERROR_SET(err, errno, errno);
      ClearPid();
   exit:;
   }

   void
   OnAttach(error *err)
   {
      Wait(err);
      ERROR_CHECK(err);

#if defined(USE_PROC_MEM)
      {
         char buf[1024];

         snprintf(buf, sizeof(buf), "/proc/%" PID_T_FMT "/mem", pid);
         memfd = open(buf, O_RDWR);
         if (memfd < 0)
         {
            int r = errno;
            error innerErr;
            error_set_errno(&innerErr, r);
            auto errString = error_get_string(&innerErr);
            log_printf(
               "Failed to open %s%s%s%s, will use slower ptrace interface",
               buf,
               errString ? " (" : "",
               errString ? errString : "",
               errString ? ")" : ""
            );
         }
      }
#endif

      DetectModules(err);
      ERROR_CHECK(err);
   exit:;
   }

   bool
   IsAttached()
   {
      return pid >= 0;
   }

   void
   Attach(const char *string, error *err)
   {
      if (ptrace(PT_ATTACH, pid = atol(string), 0, 0))
         ERROR_SET(err, errno, errno);

      OnAttach(err);
      ERROR_CHECK(err);
   exit:;
   }

   void
   Create(char *const *argv, error *err)
   {
      pid_t pid = 0;

      pid = fork();
      if (!pid)
      {
         closefrom(3);
         setpgid(0, 0);
         int r = ptrace(PT_TRACE_ME, 0, 0, 0);
         if (!r)
            r = execvp(*argv, argv);
         exit(r);
      }
      else if (pid > 0)
      {
         this->pid = pid;
         OnAttach(err);
         ERROR_CHECK(err);
      }
      else
      {
         ERROR_SET(err, errno, errno);
      }

   exit:;
   }

#if defined(USE_GETREGS)

   void
   MarkRegistersDirty()
   {
      registersDirty = true;
   }

   void
   LoadAllRegisters(error *err)
   {
      if (registersDirty)
      {
#ifdef GETREGS_REVERSED
         if (ptrace(PT_GETREGS, pid, 0, &registers))
#else
         if (ptrace(PT_GETREGS, pid, (caddr_t)&registers, 1))
#endif
            ERROR_SET(err, errno, errno);
         registersDirty = false;
      }
   exit:;
   }

   void
   StoreAllRegisters(error *err)
   {
#ifdef GETREGS_REVERSED
      if (ptrace(PT_SETREGS, pid, 0, &registers))
#else
      if (ptrace(PT_SETREGS, pid, (caddr_t)&registers, 1))
#endif
      {
         // If that failed, our reg cache is now dirty.
         //
         MarkRegistersDirty();
         ERROR_SET(err, errno, errno);
      }
   exit:;
   }

#else

   void
   MarkRegistersDirty()
   {
   }

#endif

   void
   ClearPid()
   {
      pid = -1;

#if defined(USE_PROC_MEM)
      if (memfd >= 0)
      {
         close(memfd);
         memfd = -1;
      }
#endif
   }

   //
   // XXX the following is a total hackfest.
   //

#if defined(__linux__)

   static FILE *
   open_procmap(pid_t pid)
   {
      char buf[128];
      FILE *procmap;

      snprintf(buf, sizeof(buf), "/proc/%" PID_T_FMT "/maps", pid);

      procmap = fopen(buf, "r");

      return procmap;
   }

#elif defined(__FreeBSD__) || defined(__OpenBSD__)

#define PROCMAP_HAS_MOUNTPOINT

   static FILE *
   open_procmap(pid_t pid)
   {
      int p[] = {-1, -1};
      int r = 0;
      FILE *procmap = nullptr;

      r = pipe(p);
      if (!r)
      {
         pid_t child = fork();

         if (child == 0)
         {
            char buf[32];

            close(p[0]);
            dup2(p[1], 1);
            closefrom(3);

            snprintf(buf, sizeof(buf), "%" PID_T_FMT, pid);
            r = execlp("procmap", "procmap", "-l", "-p", buf, nullptr);
            exit(r);
         }
         else if (child < 0)
            r = -1;
      }

      if (!r)
      {
         procmap = fdopen(p[0], "r");
         if (procmap)
            p[0] = -1;
      }

      if (p[0] >= 0)
         close(p[0]);
      if (p[1] >= 0)
         close(p[1]);

      return procmap;
   }

#else
#error
#endif

   static char *
   next_token(char **ptr)
   {
      char *r = *ptr;
      if (*r)
      {
         char *p = r;
         while (*p && *p != ' ')
            ++p;
         if (*p)
            *p++ = 0;
         while (*p == ' ')
            ++p;
         *ptr = p;
      }
      return r;
   }

   void
   DetectModules(error *err)
   {
      FILE *procmap = open_procmap(pid);
      char read_buffer[4096];

      if (!procmap)
         ERROR_SET(err, errno, errno);

      while (fgets(read_buffer, sizeof(read_buffer), procmap))
      {
         char *p = read_buffer;
         char *range, *perms, *offset_string, *path;
         uint64_t start_addr = 0, offset = 0;

         size_t len = strlen(read_buffer);
         if (len && read_buffer[len-1] == '\n')
            read_buffer[len-1] = 0;

         range = next_token(&p);
         perms = next_token(&p);
         offset_string = next_token(&p);
         next_token(&p);              // Device
         next_token(&p);              // inode
#ifdef PROCMAP_HAS_MOUNTPOINT
         next_token(&p);              // Mount point (BSD)
#endif
         path = next_token(&p);

         if (!*range || strlen(perms) < 3 || !*offset_string)
            continue;

         // On OpenBSD the filenames come from the namei cache, which is
         // pretty much never warm in my testing.
         //
         if (!strcmp(path, "-unknown-"))
            *path = 0;

         sscanf(range, "%" PRIX64 "-", &start_addr);
         sscanf(offset_string, "%" PRIX64, &offset);

         if (offset || perms[2] != 'x')
            continue;

         if (!*path)
            path = NULL;

         if (EventCallbacks.Get())
         {
            EventCallbacks->OnModuleProbed(start_addr, path, err);
            ERROR_CHECK(err);
         }
      }

   exit:
      if (procmap)
         fclose(procmap);
   }
};

} // end namespace


void
dbg::Create(Process **p, error *err)
{
   common::Pointer<PtraceProcess> r;
   New(r, err);
   ERROR_CHECK(err);
exit:
   if (ERROR_FAILED(err))
      r = nullptr;
   *p = r.Detach();
}
