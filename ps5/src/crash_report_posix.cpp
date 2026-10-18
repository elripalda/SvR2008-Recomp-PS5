// WWE SmackDown vs. Raw 2008 PS5 - crash reports (from the SvR 2011 port; see
// crash_report.h).
//
// On a fatal signal (SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT), writes
// <dir>/crash_<time>.txt - the signal, the faulting address, the native
// stack (symbolized where the library exports a name) and the crashed guest
// thread's PowerPC registers - and the same lines to the log, then hands the
// signal on (the previous handler, or the default: the process dies as it
// would have, and Android records its own tombstone).
//
// Runs on its own signal stack (a stack overflow can still be reported). Not
// strictly async-signal-safe (the log and stdio), but the process is ending:
// the best effort is worth it.

#include "crash_report.h"

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>

#include <dlfcn.h>
#include <unistd.h>
#if defined(__PROSPERO__)
#include <pthread_np.h>
#define gettid() pthread_getthreadid_np()  // [ps5 port]
#endif
#include <unwind.h>

#include <rex/logging.h>
#if defined(__PROSPERO__)
#include <rex/ps5_diag.h>
#endif
#include <rex/ppc/context.h>
#include <rex/system/thread_state.h>
#include <rex/system/xthread.h>

namespace svr2008 {

namespace {

std::filesystem::path g_dir;
std::atomic<bool> g_reported{false};
void (*g_hook)() = nullptr;
constexpr int kSignals[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT};
struct sigaction g_previous[sizeof(kSignals) / sizeof(kSignals[0])];

struct Frames {
  uintptr_t pc[48];
  int count = 0;
};

_Unwind_Reason_Code Collect(_Unwind_Context* context, void* arg) {
  auto* frames = static_cast<Frames*>(arg);
  const uintptr_t pc = _Unwind_GetIP(context);
  if (pc && frames->count < 48) frames->pc[frames->count++] = pc;
  return frames->count < 48 ? _URC_NO_REASON : _URC_END_OF_STACK;
}

const char* SignalName(int sig) {
  switch (sig) {
    case SIGSEGV: return "SIGSEGV (bad memory access)";
    case SIGBUS: return "SIGBUS (bad memory access)";
    case SIGILL: return "SIGILL (illegal instruction)";
    case SIGFPE: return "SIGFPE (arithmetic error)";
    case SIGABRT: return "SIGABRT (abort)";
    default: return "signal";
  }
}

// One line to the report file and the log.
void Line(FILE* f, const std::string& text) {
  if (f) std::fprintf(f, "%s\n", text.c_str());
  REXLOG_ERROR("crash: {}", text);
}

void Report(int sig, siginfo_t* info, void* ucontext) {
  if (g_reported.exchange(true) || g_dir.empty()) return;
  if (g_hook) g_hook();
  char stamp[32];
  const std::time_t t = std::time(nullptr);
  std::tm tm = {};
  localtime_r(&t, &tm);
  std::strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", &tm);
  std::error_code ec;
  std::filesystem::create_directories(g_dir, ec);
  const std::filesystem::path text = g_dir / (std::string("crash_") + stamp + ".txt");
  FILE* f = std::fopen(text.c_str(), "w");
  char buf[512];
  std::snprintf(buf, sizeof(buf), "SvR 2008 crash: %s (thread %d), address %p", SignalName(sig), gettid(),
                info ? info->si_addr : nullptr);
  Line(f, buf);
#if defined(__PROSPERO__)
  // The faulting thread's registers (the PS5's mcontext sits at +64 of the
  // ucontext: rex exception_handler_posix.cpp), the return address on its
  // stack and its frame chain - _Unwind_Backtrace stops at the signal frame.
  // "anchor" is rex::ps5diag::NowMs's address: link-time address = value -
  // anchor + NowMs's address in the ELF.
  if (ucontext) {
    const uint64_t* g = reinterpret_cast<const uint64_t*>(static_cast<const uint8_t*>(ucontext) + 64);
    const uint64_t rip = g[20], rsp = g[23], rbp = g[9];
    std::snprintf(buf, sizeof(buf), "rip %llx rsp %llx rbp %llx anchor %llx | rax %llx rbx %llx rcx %llx rdx %llx "
                  "rsi %llx rdi %llx r12 %llx r13 %llx r14 %llx r15 %llx",
                  (unsigned long long)rip, (unsigned long long)rsp, (unsigned long long)rbp,
                  (unsigned long long)rex::ps5diag::Anchor(), (unsigned long long)g[7], (unsigned long long)g[8],
                  (unsigned long long)g[4], (unsigned long long)g[3], (unsigned long long)g[2], (unsigned long long)g[1],
                  (unsigned long long)g[12], (unsigned long long)g[13], (unsigned long long)g[14], (unsigned long long)g[15]);
    Line(f, buf);
    auto plausible = [](uint64_t a) { return a >= 0x10000 && a < 0x800000000000ull && (a & 7) == 0; };
    std::string words = "stack:";
    for (int i = 0; i < 16 && plausible(rsp); ++i) {
      char w[24];
      std::snprintf(w, sizeof(w), " %llx", (unsigned long long)reinterpret_cast<const uint64_t*>(rsp)[i]);
      words += w;
    }
    Line(f, words);
    std::string chain = "frames:";
    uint64_t fp = rbp;
    for (int i = 0; i < 24 && plausible(fp); ++i) {
      const uint64_t* frame = reinterpret_cast<const uint64_t*>(fp);
      char w[24];
      std::snprintf(w, sizeof(w), " %llx", (unsigned long long)frame[1]);
      chain += w;
      if (frame[0] <= fp) break;
      fp = frame[0];
    }
    Line(f, chain);
  }
#endif
  Frames frames;
  _Unwind_Backtrace(Collect, &frames);
  for (int i = 0; i < frames.count; ++i) {
    Dl_info dl = {};
    const uintptr_t pc = frames.pc[i];
    if (dladdr(reinterpret_cast<void*>(pc), &dl) && dl.dli_fname) {
      const char* lib = std::strrchr(dl.dli_fname, '/');
      lib = lib ? lib + 1 : dl.dli_fname;
      if (dl.dli_sname) {
        std::snprintf(buf, sizeof(buf), "  %2d %s!%s+0x%zx", i, lib, dl.dli_sname,
                      size_t(pc - uintptr_t(dl.dli_saddr)));
      } else {
        std::snprintf(buf, sizeof(buf), "  %2d %s+0x%zx", i, lib, size_t(pc - uintptr_t(dl.dli_fbase)));
      }
    } else {
      std::snprintf(buf, sizeof(buf), "  %2d 0x%zx", i, size_t(pc));
    }
    Line(f, buf);
  }
  if (auto* thread = rex::system::XThread::GetCurrentThread();
      thread && thread->thread_state() && thread->thread_state()->context()) {
    const PPCContext& c = *thread->thread_state()->context();
    std::snprintf(buf, sizeof(buf), "guest thread %X: lr %08X ctr %08X r1 %08X r3 %08X r4 %08X r5 %08X",
                  thread->thread_id(), uint32_t(c.lr), c.ctr.u32, c.r1.u32, c.r3.u32, c.r4.u32, c.r5.u32);
    Line(f, buf);
  }
  if (f) std::fclose(f);
  REXLOG_ERROR("crash: report in {}", text.string());
  rex::FlushLogging();
}

void OnSignal(int sig, siginfo_t* info, void* ucontext) {
  Report(sig, info, ucontext);
  // Hand it on: the previous handler, else the default action.
  for (size_t i = 0; i < sizeof(kSignals) / sizeof(kSignals[0]); ++i) {
    if (kSignals[i] != sig) continue;
    const struct sigaction& prev = g_previous[i];
    if ((prev.sa_flags & SA_SIGINFO) && prev.sa_sigaction) {
      prev.sa_sigaction(sig, info, ucontext);
      return;
    }
    if (prev.sa_handler != SIG_DFL && prev.sa_handler != SIG_IGN && prev.sa_handler) {
      prev.sa_handler(sig);
      return;
    }
  }
  signal(sig, SIG_DFL);
  raise(sig);
}

}  // namespace

void SetCrashHook(void (*hook)()) { g_hook = hook; }

void InstallCrashReporter(const std::filesystem::path& dir) {
  g_dir = dir;
  // Its own stack: a stack overflow still reports.
  static char alt_stack[64 * 1024];
  stack_t ss = {};
  ss.ss_sp = alt_stack;
  ss.ss_size = sizeof(alt_stack);
  sigaltstack(&ss, nullptr);
  struct sigaction sa = {};
  sa.sa_sigaction = OnSignal;
  sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
  sigemptyset(&sa.sa_mask);
  for (size_t i = 0; i < sizeof(kSignals) / sizeof(kSignals[0]); ++i) sigaction(kSignals[i], &sa, &g_previous[i]);
}

}  // namespace svr2008
