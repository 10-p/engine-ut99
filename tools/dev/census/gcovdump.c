/* gcovdump.c -- census only (see BUILD.md "Coverage census"). Linked into a --coverage build so a
   game stopped by a signal still writes its counters: UT99 installs no signal handlers, the test tools stop
   games with SIGTERM, and GCC only dumps from an exit handler. A crash dumps too, then re-raises.
     cc -c -O2 -fPIC tools/dev/census/gcovdump.c -o <dir>/gcovdump.o
     cmake --preset native64 -B ../build-native64-cov ... "-DCMAKE_EXE_LINKER_FLAGS=--coverage <dir>/gcovdump.o" */
#include <signal.h>
#include <unistd.h>

extern void __gcov_dump(void);

static void on_stop(int sig)
{
	__gcov_dump();
	_exit(128 + sig);
}

static void on_crash(int sig)
{
	__gcov_dump();
	signal(sig, SIG_DFL);
	raise(sig);
}

__attribute__((constructor)) static void census_install(void)
{
	signal(SIGTERM, on_stop);
	signal(SIGINT,  on_stop);
	signal(SIGHUP,  on_stop);
	signal(SIGSEGV, on_crash);
	signal(SIGBUS,  on_crash);
	signal(SIGABRT, on_crash);
	signal(SIGFPE,  on_crash);
}
