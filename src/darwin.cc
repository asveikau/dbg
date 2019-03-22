/*
 Copyright (C) 2019 Andrew Sveikauskas

 Permission to use, copy, modify, and distribute this software for any
 purpose with or without fee is hereby granted, provided that the above
 copyright notice and this permission notice appear in all copies.
*/

#include <dbg/process.h>
#include <dbg/arch.h>
#include <dbg/misc.h>

#include <common/c++/new.h>
#include <common/misc.h>

#include <vector>

#include <sys/event.h>
#include <sys/time.h>
#include <sys/ptrace.h>

#include <mach/mach.h>
#include <mach/thread_act.h>
#include <mach/mach_vm.h>
#include <mach/vm_map.h>
#include <mach/vm_region.h>
#include <spawn.h>

using dbg::addr_t;
using dbg::reg_t;
using dbg::FormatSignal;

extern "C"
{
   boolean_t
   mach_exc_server(mach_msg_header_t *, mach_msg_header_t *);
}

namespace {

struct DarwinProcess;

#if !__has_feature(cxx_thread_local)
#define thread_local __thread
#endif

// This shall be set to an instance while mach_exc_server() is
// running, since the Mach messaging glue doesn't let us tag ports
// with arbitrary caller data, but we need a this pointer.
//
static thread_local
DarwinProcess *currentProcess = nullptr;

struct ThreadList
{
   thread_act_port_array_t threads;
   mach_msg_type_number_t count;

   ThreadList() : threads(nullptr), count(0) {}
   ThreadList(const ThreadList &) = delete;
   ~ThreadList()
   {
      Reset();
   }

   void
   Reset()
   {
      for (int i=0; i<count; ++i)
      {
         auto thread = threads[i];
         if (thread != MACH_PORT_NULL)
            mach_port_deallocate(mach_task_self(), thread);
      }

      if (threads)
      {
         vm_deallocate(mach_task_self(), (vm_address_t)threads, count*sizeof(*threads));

         threads = nullptr;
         count = 0;
      }
   }

   void
   Get(mach_port_t task, error *err)
   {
      kern_return_t r;

      Reset();

      r = task_threads(task, &threads, &count);
      if (r)
         ERROR_SET(err, darwin, r);

   exit:;
   }
};

struct PreviousExceptionState
{
   mach_port_t            task;
   exception_mask_t       masks[EXC_TYPES_COUNT];
   mach_port_t            ports[EXC_TYPES_COUNT];
   exception_behavior_t   behaviors[EXC_TYPES_COUNT];
   thread_state_flavor_t  flavors[EXC_TYPES_COUNT];
   mach_msg_type_number_t count;

   PreviousExceptionState()
   {
      Reset();
   }

   void
   Reset()
   {
      task = MACH_PORT_NULL;
      count = 0;
   }

   void
   Capture(mach_port_t task, error *err)
   {
      kern_return_t r;

      r = task_get_exception_ports(
         task,
         EXC_MASK_ALL,
         masks,
         &count,
         ports,
         behaviors,
         flavors
      );
      if (r)
         ERROR_SET(err, darwin, r);

      this->task = task;
   exit:;
   }

   void
   Restore(error *err)
   {
      kern_return_t r;

      for (int i=0; i<count; ++i)
      {
         r = task_set_exception_ports(task, masks[i], ports[i], behaviors[i], flavors[i]);
         if (r)
            ERROR_SET(err, darwin, r);
      }

      Reset();
   exit:;
   }
};

struct DarwinProcess : public dbg::Process
{
   pid_t pid;
   mach_port_t server;
   mach_port_t task;
   mach_port_t currentThread;
   mach_port_t portset;
   bool registersDirty;
   reg_t registers;
   PreviousExceptionState oldExceptions;
   int kq;
   bool traced;

   DarwinProcess() :
      pid(-1),
      server(MACH_PORT_NULL),
      task(MACH_PORT_NULL),
      currentThread(MACH_PORT_NULL),
      portset(MACH_PORT_NULL),
      registersDirty(true),
      kq(-1),
      traced(false)
   {
   }

   ~DarwinProcess()
   {
      ClosePorts();
   }

   void
   ClosePorts()
   {
      if (kq > -1)
      {
         close(kq);
         kq = -1;
      }

      auto ports =
      {
         &server,
         &currentThread,
         &portset,
         &task,
      };
      for (auto p : ports)
      {
         if (*p)
         {
            mach_port_deallocate(mach_task_self(), *p);
            *p = MACH_PORT_NULL;
         }
      }

      pid = -1;
   }

   void
   Create(char *const *argv, error *err)
   {
      pid_t pid = -1;
      posix_spawnattr_t sa;
      posix_spawn_file_actions_t fa;
      bool attr = false;
      bool actions = false;
      int flags = POSIX_SPAWN_START_SUSPENDED;
      sigset_t empty;
      sigset_t full;

      if (posix_spawnattr_init(&sa))
         ERROR_SET(err, errno, errno);

      attr = true;

#if 0 // XXX need to figure out how to handle SIGTTIN/SIGTTOU before doing this
      flags |= POSIX_SPAWN_SETPGROUP;

      if (posix_spawnattr_setpgroup(&sa, 0))
         ERROR_SET(err, errno, errno);
#endif

      flags |= (POSIX_SPAWN_SETSIGDEF | POSIX_SPAWN_SETSIGMASK);

      sigemptyset(&empty);
      sigfillset(&full);

      if (posix_spawnattr_setsigmask(&sa, &empty))
         ERROR_SET(err, errno, errno);

      if (posix_spawnattr_setsigdefault(&sa, &full))
         ERROR_SET(err, errno, errno);

      flags |= POSIX_SPAWN_CLOEXEC_DEFAULT;

      if (posix_spawn_file_actions_init(&fa))
         ERROR_SET(err, errno, errno);

      actions = true;

      for (int i=0; i<3; i++)
      {
         if (posix_spawn_file_actions_adddup2(&fa, i, i))
            ERROR_SET(err, errno, errno);
      }

      if (posix_spawnattr_setflags(&sa, flags))
         ERROR_SET(err, errno, errno);

      if (posix_spawnp(&pid, argv[0], &fa, &sa, argv, nullptr))
         ERROR_SET(err, errno, errno);

      Attach(pid, true, err);
      if (ERROR_FAILED(err))
         kill(pid, SIGTERM);
      ERROR_CHECK(err);

   exit:
      if (actions)
         posix_spawn_file_actions_destroy(&fa);
      if (attr)
         posix_spawnattr_destroy(&sa);
   }

   void
   Attach(const char *string, error *err)
   {
      Attach(atol(string), false, err);
   }

   void
   Attach(pid_t pid, bool suspended, error *err)
   {
      kern_return_t r;
      ThreadList th;
      bool suspendedHere = false;

      r = task_for_pid(mach_task_self(), pid, &task);
      if (r)
         ERROR_SET(err, darwin, r);

      if (!suspended)
      {
         r = task_suspend(task);
         if (r)
            ERROR_SET(err, darwin, r);

         suspendedHere = true;
      }

      if (!server)
      {
         r = mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &server);
         if (r)
            ERROR_SET(err, darwin, r);

         r = mach_port_insert_right(mach_task_self(), server, server, MACH_MSG_TYPE_MAKE_SEND);
         if (r)
            ERROR_SET(err, darwin, r);
      }

      oldExceptions.Capture(task, err);
      ERROR_CHECK(err);

      r = task_set_exception_ports(task, EXC_MASK_ALL, server, EXCEPTION_DEFAULT | MACH_EXCEPTION_CODES, THREAD_STATE_NONE);
      if (r)
         ERROR_SET(err, darwin, r);

      th.Get(task, err);
      ERROR_CHECK(err);

      if (th.count <= 0)
         ERROR_SET(err, unknown, "No threads?");

      currentThread = th.threads[0];
      th.threads[0] = MACH_PORT_NULL;

      if (ptrace(PT_ATTACHEXC, pid, 0, 0))
         ERROR_SET(err, errno, errno);

      traced = true;
      this->pid = pid;

      Wait(err);
      ERROR_CHECK(err);

      // PT_ATTACHEXEC will have resumed us again.
      //
      r = task_suspend(task);
      if (r)
         ERROR_SET(err, darwin, r);

   exit:
      if (ERROR_FAILED(err))
      {
         error innerErr;

         oldExceptions.Restore(&innerErr);

         if (traced)
         {
            ptrace(PT_DETACH, pid, 0, 0);
            traced = false;
         }

         if (suspendedHere)
            task_resume(task);

         this->pid = -1;
      }
   }

   void
   ReadMemory(addr_t addr, int len, void *buf, error *err)
   {
      kern_return_t r;
      mach_vm_size_t bytesIn = len;

      r = mach_vm_read_overwrite(task, addr, len, (mach_vm_address_t)buf, &bytesIn);
      if (r)
         ERROR_SET(err, darwin, r);
      if (bytesIn < len)
         memset((char*)buf + bytesIn, 0, len - bytesIn);
   exit:;
   }

   void
   WriteMemory(addr_t addr, int len, const void *buf, error *err)
   {
      kern_return_t r;
      bool retried = false;
      addr_t aligned = 0;
      vm_size_t alignedSize = 0;
      vm_size_t pageSize = 0;
      addr_t p = 0;

      struct region
      {
         mach_vm_address_t addr;
         mach_vm_size_t size;
         vm_prot_t prot;
      };
      std::vector<region> oldRegions;

   retry:
      r = mach_vm_write(task, (mach_vm_address_t)addr, (mach_vm_address_t)buf, len);
      if (r)
      {
         if (retried)
            ERROR_SET(err, darwin, r);

         auto oldError = r;

         //
         // If the write failed, page-align the memory range, then ask the VM about
         // protection bits.
         //

         r = host_page_size(mach_host_self(), &pageSize);
         if (r)
            ERROR_SET(err, darwin, r);
         if (!pageSize)
            ERROR_SET(err, unknown, "Unexpected page size");

         aligned = addr / pageSize * pageSize;
         alignedSize = ((addr + len + pageSize - 1) / pageSize * pageSize) - aligned;

         p = aligned;

         for (int i=0, nPages = alignedSize/pageSize; p < (aligned + alignedSize) && i<nPages; )
         {
            region rgn = {p, pageSize, 0};
#if defined(__amd64__)
            vm_region_basic_info_64 info = {0};
            vm_region_flavor_t flavor = VM_REGION_BASIC_INFO_64;
            mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
#elif defined(__i386__)
            vm_region_basic_info info = {0};
            vm_region_flavor_t flavor = VM_REGION_BASIC_INFO;
            mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT;
#endif
            mach_port_t objectName = MACH_PORT_NULL;

            r = mach_vm_region(task, &rgn.addr, &rgn.size, flavor, (vm_region_info_t)&info, &count, &objectName);
            if (objectName)
               mach_port_deallocate(mach_task_self(), objectName);
            if (r)
               ERROR_SET(err, darwin, r);

            rgn.prot = info.protection;

            p += rgn.size;
            i += (rgn.size + pageSize - 1) / pageSize;

            // Is this region non-writeable?
            //
            if (!(rgn.prot & VM_PROT_WRITE))
            {
               // If the range is bigger than the page-aligned range we're interested in,
               // shrink it.
               //
               auto addr = MAX(aligned, rgn.addr);
               auto size = MIN(aligned + alignedSize, rgn.addr + rgn.size) - addr;

               rgn.addr = addr;
               rgn.size = size;

               // Remember for next pass.
               //
               try
               {
                  oldRegions.push_back(rgn);
               }
               catch (std::bad_alloc)
               {
                  ERROR_SET(err, nomem);
               }
            }
         }

         // No edits?  Bail.
         //
         if (!oldRegions.size())
            ERROR_SET(err, darwin, oldError);

         for (auto rgn : oldRegions)
         {
            auto newProt = ((rgn.prot) & ~VM_PROT_EXECUTE) | VM_PROT_WRITE;  // preserve W^X.

            r = mach_vm_protect(task, rgn.addr, rgn.size, false, newProt);
            if (r)
               ERROR_SET(err, darwin, r);
         }

         // Retry the write.
         //
         retried = true;
         goto retry;
      }

      // Revert protection modifications.
      //
      for (auto rgn : oldRegions)
      {
         r = mach_vm_protect(task, rgn.addr, rgn.size, false, rgn.prot);
         if (r)
            ERROR_SET(err, darwin, r);
      }
   exit:;
   }

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
         kern_return_t r;
         mach_msg_type_number_t sc = THREAD_STATE_COUNT;

         r = thread_get_state(
            currentThread,
            THREAD_STATE_FLAVOR,
            (thread_state_t)&registers,
            &sc
         );
         if (r)
            ERROR_SET(err, darwin, r);
         registersDirty = false;
      }
   exit:;
   }

   void
   StoreAllRegisters(error *err)
   {
      kern_return_t r;
      mach_msg_type_number_t sc = THREAD_STATE_COUNT;

      r = thread_set_state(
         currentThread,
         THREAD_STATE_FLAVOR,
         (thread_state_t)&registers,
         sc
      );
      if (r)
      {
         // If that failed, our reg cache is now dirty.
         //
         MarkRegistersDirty();
         ERROR_SET(err, darwin, r);
      }
      registersDirty = false;
   exit:;
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

      LoadAllRegisters(err);
      ERROR_CHECK(err);

      // Copy into the caller's buffer...
      //
      memcpy(reg, ((char*)&registers) + (size_t)offset, len);
   exit:;
   }

   void
   SetRegister(int regno, const void *reg, error *err)
   {
      void *offset = nullptr;
      size_t len = 0;

      RegDeref(regno, &offset, &len, err);
      ERROR_CHECK(err);

      LoadAllRegisters(err);
      ERROR_CHECK(err);

      // Set the register...
      //
      memcpy(((char*)&registers) + (size_t)offset, reg, len);
      
      StoreAllRegisters(err);
      ERROR_CHECK(err);
   exit:;
   }

   void
   Interrupt(error *err)
   {
      kern_return_t r;
      task_basic_info info = {0};
      mach_msg_type_number_t count = TASK_BASIC_INFO_COUNT;

      if (!task)
         goto exit;

      r = task_suspend(task);
      if (r)
         ERROR_SET(err, darwin, r);

      r = task_info(task, TASK_BASIC_INFO, (task_info_t)&info, &count);
      if (r)
         ERROR_SET(err, darwin, r);

      if (info.suspend_count > 1)
      {
         // Oops, it was already suspended.  Revert the count bump.
         //
         r = task_resume(task);
         if (r)
            ERROR_SET(err, darwin, r);
      }

   exit:;
   }

#if defined(__amd64__) || defined(__i386__)
   const uintptr_t stepFlag = 0x100;

   void
   Step(error *err)
   {
      kern_return_t r;
      uintptr_t flags = 0;
      ThreadList th;
      bool hadStep = false;

      //
      // We will set the TRAP flag in the FLAGS register, pause other threads,
      // and resume the program.  When we get an exception, we'll clear TRAP.
      //

      GetRegister(DBG_FLAGS, &flags, err);
      ERROR_CHECK(err);

      if (!(hadStep = (flags & stepFlag)))
      {
         flags |= stepFlag;
         SetRegister(DBG_FLAGS, &flags, err);
         ERROR_CHECK(err);
      }

      th.Get(task, err);
      ERROR_CHECK(err);
      for (int i=0; i<th.count; ++i)
      {
         if (th.threads[i] != currentThread)
         {
            r = thread_suspend(th.threads[i]);
         }
      }

      MarkRegistersDirty();

      r = task_resume(task);
      if (r)
         ERROR_SET(err, darwin, r);

      Wait(err);
      ERROR_CHECK(err);

      r = task_suspend(task);
      if (r)
         ERROR_SET(err, darwin, r);

      for (int i=0; i<th.count; ++i)
      {
         if (th.threads[i] != currentThread)
         {
            r = thread_resume(th.threads[i]);
            ERROR_CHECK(err);
         }
      }

      if (!hadStep)
      {
         GetRegister(DBG_FLAGS, &flags, err);
         ERROR_CHECK(err);
         flags &= ~stepFlag;
         SetRegister(DBG_FLAGS, &flags, err);
         ERROR_CHECK(err);
      }

   exit:;
   }
#endif

   void
   Wait(error *err)
   {
      struct kevent evs[4], *ev = evs;
      int n = 0;
      bool kqInitHere = false;
      bool keventSucceeded = false;

      if (kq < 0)
      {
         kq = kqueue();
         if (kq < 0)
            ERROR_SET(err, errno, errno);

         kqInitHere = true;

         // I have one machine that can't upgrade past El Capitan, and on that OS it seems
         // EVFILT_MACHPORT can't take a port directly, only a port set.  This is fixed
         // by High Sierra.
         //
         if (!portset)
         {
            kern_return_t r;

            r = mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_PORT_SET, &portset);
            if (r)
               ERROR_SET(err, darwin, r);

            r = mach_port_insert_member(mach_task_self(), server, portset);
            if (r)
            {
               mach_port_deallocate(mach_task_self(), portset);
               portset = MACH_PORT_NULL;
               ERROR_SET(err, darwin, r);
            }
         }

         EV_SET(ev, pid, EVFILT_PROC, EV_ADD | EV_ENABLE, NOTE_EXIT, 0, 0);
         ++ev;
         EV_SET(ev, portset, EVFILT_MACHPORT, EV_ADD | EV_ENABLE, 0, 0, 0);
         ++ev;
      }

      n = kevent(kq, evs, ev - evs, ev, ARRAY_SIZE(evs) - (ev - evs), nullptr);
      if (n < 0)
         ERROR_SET(err, errno, errno);
      keventSucceeded = true;
      for (; n--; ++ev)
      {
         if ((ev->flags & EV_ERROR))
            ERROR_SET(err, errno, ev->data);
         if (ev->filter == EVFILT_PROC)
         {
            if ((ev->fflags & NOTE_EXIT))
            {
               ClosePorts();

               if (!EventCallbacks.Get())
                  continue;

               EventCallbacks->OnMessage(err, "Exited with status %d\n", ev->data);
               ERROR_CHECK(err);

               EventCallbacks->OnProcessExited(err);
               ERROR_CHECK(err);
            }
         }
         else if (ev->filter == EVFILT_MACHPORT)
         {
            ProcessMachPort(true, err);
         }
         ERROR_CHECK(err);
      }

   exit:
      if (ERROR_FAILED(err) &&
          kqInitHere &&
          !keventSucceeded &&
          kq > -1)
      {
         close(kq);
         kq = -1;
      }
   }

   bool
   ProcessMachPort(bool block, error *err)
   {
      bool recvd = false;
      currentProcess = this;
      int timeout = block ? MACH_MSG_TIMEOUT_NONE : 1;
      const int bufsz = 128;
      union
      {
         mach_msg_header_t hdr;
         char buf[bufsz];
      } req, repl;
      mach_msg_return_t r = 0;
      r = mach_msg(
         &req.hdr,
         MACH_RCV_MSG | (block ? 0 : MACH_RCV_TIMEOUT),
         0,
         sizeof(req),
         server,
         timeout,
         MACH_PORT_NULL
      );
      if (!block && r == MACH_RCV_TIMED_OUT)
         goto exit;
      if (r)
         ERROR_SET(err, unknown, "mach_msg failed");
      if (!mach_exc_server(&req.hdr, &repl.hdr))
         ERROR_SET(err, unknown,"mach_exc_server failed");
      mach_msg(
         &repl.hdr,
         MACH_SEND_MSG,
         repl.hdr.msgh_size,
         0,
         MACH_PORT_NULL,
         MACH_MSG_TIMEOUT_NONE,
         MACH_PORT_NULL
      );
      recvd = true;
   exit:
      currentProcess = nullptr;
      return recvd;
   }

   void
   Detach(error *err)
   {
      kern_return_t r;

      if (!task)
         goto exit;

      if (traced)
      {
         ptrace(PT_DETACH, pid, 0, 0);
         traced = false;
      }

      oldExceptions.Restore(err);
      ERROR_CHECK(err);

      r = task_resume(task);
      if (r)
         ERROR_SET(err, darwin, r);

      ClosePorts();
   exit:;
   }

   void
   Go(error *err)
   {
      kern_return_t r;

      MarkRegistersDirty();

      r = task_resume(task);
      if (r)
         ERROR_SET(err, darwin, r);

      Wait(err);
      ERROR_CHECK(err);
   exit:;
   }

   void
   Quit(error *err)
   {
      if (pid < 0)
         goto exit;
      if (kill(pid, SIGTERM))
         ERROR_SET(err, errno, errno);
   exit:;
   }

   kern_return_t
   CatchMachExceptionRaise(
      mach_port_t thread,
      exception_type_t exception,
      mach_exception_data_t code,
      mach_msg_type_number_t nCodes
   )
   {
      error err;

      switch (exception)
      {
      case EXC_SOFTWARE:
         if (code[0] == EXC_SOFT_SIGNAL)
         {
            int sig = code[1];
            int sigToDeliver = sig;
            bool notify = true;

            switch (sig)
            {
            case SIGSTOP:
               notify = false;
               // fall through
            case SIGINT:
               sigToDeliver = 0;
               break;
            }

            if (notify && EventCallbacks.Get())
            {
               char namebuf[32];

               EventCallbacks->OnMessage(
                  &err,
                  "Got signal %s\n",
                  FormatSignal(namebuf, sizeof(namebuf), sig)
               );
               ERROR_CHECK(&err);

               EventCallbacks->OnSignal(sig, &err);
               ERROR_CHECK(&err);
            }

            if (ptrace(PT_THUPDATE, pid, (caddr_t)(intptr_t)thread, sigToDeliver))
               ERROR_SET(&err, errno, errno);
         }
         break;
#if defined (__amd64__) || defined(__i386__)
      case EXC_BREAKPOINT:
         if (code[0] == 2)
         {
            uintptr_t flags = 0;
            GetRegister(DBG_FLAGS, &flags, &err);
            ERROR_CHECK(&err);

            if (Cpu.Get())
            {
               Cpu->OnBreakpointBreak(this, &err);
               ERROR_CHECK(&err);
            }

            if (!(flags & stepFlag))
            {
               task_suspend(task);
            }
         }
         break;
#else
#warning Breakpoint not ported
#endif
      default:
         if (EventCallbacks.Get())
         {
            char buf[32];

            EventCallbacks->OnMessage(
               &err,
               "Got exception %s\n",
               FormatException(buf, sizeof(buf), exception)
            );
         }
      }

   exit:
      return KERN_SUCCESS;
   }

   static const char *
   FormatException(char *buf, size_t sz, exception_type_t exn)
   {
      auto name = ExceptionString(exn);

      if (name)
         snprintf(buf, sz, "%d (%s)", exn, name);
      else
         snprintf(buf, sz, "%d", exn);

      return buf;
   }

   static const char *
   ExceptionString(exception_type_t exn)
   {
#define CASE(X) case X: return #X
      switch (exn)
      {
      CASE(EXC_ARITHMETIC);
      CASE(EXC_BAD_ACCESS);
      CASE(EXC_BAD_INSTRUCTION);
      CASE(EXC_BREAKPOINT);
      CASE(EXC_CORPSE_NOTIFY);
      CASE(EXC_CRASH);
      CASE(EXC_EMULATION);
      CASE(EXC_GUARD);
      CASE(EXC_MACF_MAX);
      CASE(EXC_MACF_MIN);
      CASE(EXC_MACH_SYSCALL);
      CASE(EXC_RESOURCE);
      CASE(EXC_RPC_ALERT);
      CASE(EXC_SOFTWARE);
      CASE(EXC_SOFT_SIGNAL);
      CASE(EXC_SYSCALL);
      default: return nullptr;
      }
#undef CASE
   }
};

} // end namespace

extern "C"
{
kern_return_t
catch_mach_exception_raise(
   mach_port_t exception_port,
   mach_port_t thread,
   mach_port_t task,
   exception_type_t exception,
   mach_exception_data_t code,
   mach_msg_type_number_t nCodes
);

kern_return_t
catch_mach_exception_raise_state(
   mach_port_t exception_port,
   mach_port_t thread,
   mach_port_t task,
   exception_type_t exception,
   mach_exception_data_t code,
   mach_msg_type_number_t nCodes
);

kern_return_t catch_mach_exception_raise_state_identity(
   mach_port_t exception_port,
   mach_port_t thread,
   mach_port_t task,
   exception_type_t exception,
   mach_exception_data_t code,
   mach_msg_type_number_t nCodes
);
}

kern_return_t
catch_mach_exception_raise(
   mach_port_t exception_port,
   mach_port_t thread,
   mach_port_t task,
   exception_type_t exception,
   mach_exception_data_t code,
   mach_msg_type_number_t nCodes
)
{
   kern_return_t r = KERN_SUCCESS;

   if (currentProcess && currentProcess->task == task)
   {
      r = currentProcess->CatchMachExceptionRaise(
         thread,
         exception,
         code,
         nCodes
      );
   }

   return r;
}

kern_return_t
catch_mach_exception_raise_state(
   mach_port_t exception_port,
   mach_port_t thread,
   mach_port_t task,
   exception_type_t exception,
   mach_exception_data_t code,
   mach_msg_type_number_t nCodes
)
{
   return MACH_RCV_INVALID_TYPE;
}

kern_return_t
catch_mach_exception_raise_state_identity(
   mach_port_t exception_port,
   mach_port_t thread,
   mach_port_t task,
   exception_type_t exception,
   mach_exception_data_t code,
   mach_msg_type_number_t nCodes
)
{
   return MACH_RCV_INVALID_TYPE;
}

void
dbg::Create(Process **p, error *err)
{
   common::Pointer<DarwinProcess> r;
   common::New(r, err);
   ERROR_CHECK(err);
exit:
   if (ERROR_FAILED(err))
      r = nullptr;
   *p = r.Detach();
}
