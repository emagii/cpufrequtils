LIBS = -L../.libs/ -lm -lcpufreq

OBJS = main.o parse.o system.o benchmark.o
CFLAGS += -D_GNU_SOURCE

ifeq ($(strip $(V)),false)
	CC=@../build/ccdv gcc
else
	CC=gcc
endif

cpufreq-bench: $(OBJS)
	$(CC) -o $@ $(CFLAGS) $(OBJS) $(LIBS)

all: cpufreq-bench

install:
	mkdir -p $(DESTDIR)/$(sbindir)
	mkdir -p $(DESTDIR)/$(docdir)
	mkdir -p $(DESTDIR)/$(confdir)
	install -m 755 cpufreq-bench $(DESTDIR)/$(sbindir)/cpufreq-bench
	install -m 644 README $(DESTDIR)/$(docdir)/README
	install -m 644 example.cfg $(DESTDIR)/$(confdir)/cpufreq-bench.conf

clean:
	rm -f *.o
	rm -f cpufreq-bench
