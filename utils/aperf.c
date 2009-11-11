/*
 *  Copyright (C) 2009  Thomas Renninger <trenn@suse.de>, Novell Inc.
 *
 *  Inspired by these projects:
 *    cpuid (by Todd Allen)
 *    msr-tools (by H. Peter Anvin <hpa@zytor.com>)
 *
 *  Licensed under the terms of the GNU GPL License version 2.
 *
 *
 *  What does this program do:
 *
 *  On latest processors exist two MSR registers refered to as:
 *    - MPERF increasing with maxium (P0) frequency in C0
 *    - APERF increasing with current/actual frequency in C0
 *
 *  From this information the average frequency over a time period can be
 *  calculated and this is what this tool does.
 *
 *  A nice falloff feature beside the average frequency is the time
 *  a processor core remained in C0 (working state) or any CX (sleep state)
 *  processor sleep state during the measured time period. This information
 *  can be determined from the fact that MPERF only increases in C0 state.
 *
 *  Note: There were kernels which reset MPERF/APERF registers to 0.
 *        This got reverted by git commit
 *                  18b2646fe3babeb40b34a0c1751e0bf5adfdc64c
 *        which was commited to 2.6.30-rcX mainline kernels
 *        For kernels where the kernel rests MPERF/APERF registers to 0,
 *        this tool will not work. It cannot be detected whether this happened.
 *
 * Possible ToDos/Enhancments:
 *
 *  - Use cpuid assmbler command to retrieve cpuid
 *    -No dependency to the cpuid driver
 *  - Refresh the screen when mulitple cpus are poked and display results
 *    on one screen
 *    -This would introduce a lot more complexity, not sure whether it's
 *       wanted/needed. I'd vote to better not do that.
 *  - Manpage
 *  - Translations
 *  - ...
 */


#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/time.h>
#include <stdint.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>

#include "cpufreq.h"

#define MSR_IA32_APERF 0x000000E8
#define MSR_IA32_MPERF 0x000000E7

#define LINE_LEN 10

struct avg_perf_cpu_info
{
	unsigned long max_freq;
	uint64_t saved_aperf;
	uint64_t saved_mperf;
	uint32_t is_valid:1;
};

static unsigned int count_cpus(void)
{
	FILE *fp;
	char value[LINE_LEN];
	unsigned int ret = 0;
	unsigned int cpunr = 0;

	fp = fopen("/proc/stat", "r");
	if(!fp) {
		printf("Couldn't count the number of CPUs (%s: %s), "
			"assuming 1\n", "/proc/stat", strerror(errno));
		return 1;
	}

	while (!feof(fp)) {
		if (!fgets(value, LINE_LEN, fp))
			continue;
		value[LINE_LEN - 1] = '\0';
		if (strlen(value) < (LINE_LEN - 2))
			continue;
		if (strstr(value, "cpu "))
			continue;
		if (sscanf(value, "cpu%d ", &cpunr) != 1)
			continue;
		if (cpunr > ret)
			ret = cpunr;
	}
	fclose(fp);

	/* cpu count starts from 0, on error return 1 (UP) */
	return (ret+1);
}

static int has_mperf_aperf_support(int cpu)
{
	int fd;
	char msr_file_name[64];
	uint32_t words[4];
	int support;

	sprintf(msr_file_name, "/dev/cpu/%d/cpuid", cpu);
	fd = open(msr_file_name, O_RDONLY);
	if (fd < 0)
		return -1;
	if (lseek(fd, 6, SEEK_CUR) == -1)
		goto err;
	if (read(fd, words, 16) != 16)
		goto err;
	support = words[2] & 0x1;
	close(fd);
	return support;
 err:
	close(fd);
	return -1;
}

/*
 * read_msr
 *
 * Will return 0 on success and -1 on failure.
 * Possible errno values could be:
 * EFAULT -If the read/write did not fully complete
 * EIO    -If the CPU does not support MSRs
 * ENXIO  -If the CPU does not exist
 */

static int read_msr(int cpu, unsigned int idx, unsigned long long *val)
{
	int fd;
	char msr_file_name[64];

	sprintf(msr_file_name, "/dev/cpu/%d/msr", cpu);
	fd = open(msr_file_name, O_RDONLY);
	if (fd < 0)
		return -1;
	if (lseek(fd, idx, SEEK_CUR) == -1)
		goto err;
	if (read(fd, val, sizeof val) != sizeof *val)
		goto err;
	close(fd);
	return 0;
 err:
	close(fd);
	return -1;
}

/*
 * get_aperf_mperf()
 *
 * Returns the current aperf/mperf MSR values of cpu
 */
static int get_aperf_mperf(unsigned int cpu, uint64_t *aperf, uint64_t *mperf)
{
	int retval;

	retval = read_msr(cpu, MSR_IA32_APERF, (unsigned long long*)aperf);
	if (retval < 0)
		return retval;
	
	retval = read_msr(cpu, MSR_IA32_MPERF, (unsigned long long*)mperf);
	if (retval < 0)
		return retval;
	return 0;
}

/*
 * get_average_perf()
 *
 * Returns the average performance (also considers boosted frequencies)
 * 
 * Input:
 *   aperf_diff: Difference of the aperf register over a time period
 *   mperf_diff: Difference of the mperf register over the same time period
 *   max_freq:   Maximum frequency (P0)
 *
 * Returns:
 *   Average performance over the time period
 */
static unsigned long get_average_perf(unsigned long max_freq,
				      uint64_t aperf_diff,
				      uint64_t mperf_diff)
{
	unsigned int perf_percent = 0;
	if (((unsigned long)(-1) / 100) < aperf_diff) {
		int shift_count = 7;
		aperf_diff >>= shift_count;
		mperf_diff >>= shift_count;
	}
	perf_percent = (aperf_diff * 100) / mperf_diff;
	return (max_freq * perf_percent) / 100;
}

/*
 * get_C_state_time()
 *
 * Calculates the time the processor was in C0 and Cx processor sleep states
 *
 * As mperf does only tick in C0 at maximum frequency, this is a nice "falloff"
 * functionality and more accurate than powertop or other kernel timer based
 * C-state measurings (and can be used to verify whether they are correct.
 *
 * Input:
 *   time_diff:  The time passed for which the mperf_diff was calulcated on
 *   mperf_diff: The value the mperf register increased during time_diff
 *   max_freq:   Maximum frequency of the processor (P0) in kHz
 *
 * Output:
 *   C0_time:    The time the processor was in C0
 *   CX_time:    The time the processor was in CX
 *   percent:    Percentage the processor stayed in C0
 */
static int get_C_state_time(struct timeval time_diff, uint64_t mperf_diff,
		     unsigned long max_freq,
		     struct timeval *C0_time, struct timeval *CX_time,
		     unsigned int *percent)
{
	unsigned long long overall_msecs, expected_ticks, c0_time, cx_time;

	overall_msecs = (time_diff.tv_sec * 1000 * 1000  + time_diff.tv_usec)
		/ 1000;

	expected_ticks = max_freq * overall_msecs;
	*percent = (mperf_diff * 100) / expected_ticks;

	cx_time = (expected_ticks - mperf_diff) / max_freq;
	c0_time = mperf_diff / max_freq;

	CX_time->tv_sec  = cx_time / 1000;
	CX_time->tv_usec = cx_time % 1000;
	C0_time->tv_sec  = c0_time / 1000;
	C0_time->tv_usec = c0_time % 1000;
	return 0;
}

static int get_measure_start_info(unsigned int cpu,
				  struct avg_perf_cpu_info *cpu_info)
{
	unsigned long min, max;
	uint64_t aperf, mperf;
	int ret;

	cpu_info->is_valid = 0;
		
	ret = has_mperf_aperf_support(cpu);
	if (ret < 0) {
		fprintf(stderr, "Could not read cpuid, is the cpuid "
			"driver loaded or compiled into the kernel?\n");
		return ret;
	} else if (ret == 0) {
		fprintf(stderr, "CPU(s) do not support mperf/aperf MSR "
			"registers\n");
		return EXIT_FAILURE;
	}
	
	if (cpufreq_get_hardware_limits(cpu, &min, &max)) {
		fprintf(stderr, "Could not get max frequency (P0), a "
			"cpufreq driver must be loaded?\n");
		return EXIT_FAILURE;
	} else
		cpu_info->max_freq = max;
	
	ret = get_aperf_mperf(cpu, &aperf, &mperf);
	if (ret < 0) {
		fprintf(stderr, "Could not read MSRs, is the msr driver loaded"
			" or compiled into the kernel?\n");
		return EXIT_FAILURE;
	} else {
		cpu_info->saved_aperf = aperf;
		cpu_info->saved_mperf = mperf;
	}
	cpu_info->is_valid = 1;
	return 0;
}

static void print_cpu_stats(unsigned int cpu, unsigned long average,
			    struct timeval c0_time, struct timeval cx_time,
			    unsigned int c0_percent)
{
	printf("%.3u\t", cpu);
	printf("%.7lu\t\t\t", average);
	printf("%.2lu sec %.3lu ms\t", c0_time.tv_sec, c0_time.tv_usec);
	printf("%.2lu sec %.3lu ms\t", cx_time.tv_sec, cx_time.tv_usec);
	printf("%.2u", c0_percent);
}


static int do_measuring_on_cpu(int sleep_time, int once, int cpu)
{
	int ret;
	unsigned long average;
	unsigned int c0_percent;
	struct timeval start_time, current_time, diff_time, C0_time, CX_time;
	uint64_t current_aperf, current_mperf, mperf_diff, aperf_diff;
	struct avg_perf_cpu_info cpu_info;

	ret = get_measure_start_info(cpu, &cpu_info);
	if (ret)
		return ret;

	while(1) {
		gettimeofday(&start_time, NULL);
		sleep(sleep_time);
		/* ToDo: just add a second on the timeval struct? */
		gettimeofday(&current_time, NULL);
		timersub(&current_time, &start_time, &diff_time);
		memcpy(&start_time, &current_time,
		       sizeof(struct timeval));

		if (!cpu_info.is_valid)
			continue;
		
		ret = get_aperf_mperf(cpu, &current_aperf, &current_mperf);
		if (ret < 0)
			return EXIT_FAILURE;
		mperf_diff = current_mperf - cpu_info.saved_mperf;
		aperf_diff = current_aperf - cpu_info.saved_aperf;
		
		get_C_state_time(diff_time, mperf_diff,
				 cpu_info.max_freq,
				 &C0_time, &CX_time,
				 &c0_percent);
		average = get_average_perf(cpu_info.max_freq,
					   aperf_diff, mperf_diff);
		cpu_info.saved_mperf = current_mperf;
		cpu_info.saved_aperf = current_aperf;
		print_cpu_stats(cpu, average, C0_time, CX_time,
				c0_percent);

		if (once) {
			printf("\n");
			break;
		} else {
			printf("\r");
			fflush(stdout);
		}
	}
	return 0;
}

static int do_measure_all_cpus(int sleep_time, int once)
{
	int ret;
	unsigned long average;
	unsigned int c0_percent, cpus, cpu;
	struct timeval start_time, current_time, diff_time, C0_time, CX_time;
	uint64_t current_aperf, current_mperf, mperf_diff, aperf_diff;
	struct avg_perf_cpu_info *cpu_list;

	cpus = count_cpus();

	cpu_list = (struct avg_perf_cpu_info*)
		malloc(cpus * sizeof (struct avg_perf_cpu_info));
	
	for (cpu = 0; cpu < cpus; cpu++) {
		ret = get_measure_start_info(cpu, &cpu_list[cpu]);
		if (ret)
			return ret;
	}	

	while(1) {
		gettimeofday(&start_time, NULL);
		sleep(sleep_time);
		/* ToDo: Just add a second on the timeval struct?
		         Would save one gettimeofday, but would not
			 be that accurate anymore
		*/
		gettimeofday(&current_time, NULL);
		timersub(&current_time, &start_time, &diff_time);
		memcpy(&start_time, &current_time,
		       sizeof(struct timeval));

		for (cpu = 0; cpu < cpus; cpu++) {
			if (!cpu_list[cpu].is_valid)
				continue;

			ret = get_aperf_mperf(cpu, &current_aperf,
					      &current_mperf);
			if (ret < 0)
				return EXIT_FAILURE;
			mperf_diff = current_mperf - cpu_list[cpu].saved_mperf;
			aperf_diff = current_aperf - cpu_list[cpu].saved_aperf;

			get_C_state_time(diff_time, mperf_diff,
					 cpu_list[cpu].max_freq,
					 &C0_time, &CX_time,
					 &c0_percent);
			average = get_average_perf(cpu_list[cpu].max_freq,
						   aperf_diff, mperf_diff);
			cpu_list[cpu].saved_mperf = current_mperf;
			cpu_list[cpu].saved_aperf = current_aperf;
			print_cpu_stats(cpu, average, C0_time, CX_time,
					c0_percent);
			printf("\n");
		}
		if (once)
			break;
		printf("\n");
	}
	return 0;
}


/******* Options parsing, main ********/

static struct option long_options[] = {
  { "help",		0, 0, 'h' },
  { "intervall",	1, 0, 'i' },
  { "cpu",		1, 0, 'c' },
  { "once",		0, 0, 'o' },
  { 0, 0, 0, 0 }
};

static void usage(void) {
	printf("cpufreq-aperf [OPTIONS]\n\n"
	       "-c [ --cpu ] CPU               "
	       "The CPU core to measure - default all cores\n"
	       "-i [ --intervall ] seconds     "
	       "Refresh rate - default 1 second\n"
	       "-o [ --once ]                  "
	       "Exit after one intervall\n"
	       "-h [ --help ]                  "
	       "This help text\n"
	       "The msr driver must be loaded for this command to work\n");
}

int main(int argc, char *argv[])
{
	int c, ret, cpu = -1;
	int sleep_time = 1, once = 0;
	while ( (c = getopt_long(argc,argv,"c:ohi:",long_options,
				 NULL)) != -1 ) {
		switch ( c ) {
		case 'o':
			once = 1;
			break;
		case 'c':
			cpu = atoi(optarg);
			break;
		case 'h':
			usage();
			exit(0);
		case 'i':
			sleep_time = atoi(optarg);
			break;
		}
	}
	printf("CPU\tAverage freq(KHz)\tTime in C0\tTime in"
	       " Cx\tC0 percentage\n");

	if (cpu == -1)
		ret = do_measure_all_cpus(sleep_time, once);
	else
		ret = do_measuring_on_cpu(sleep_time, once, cpu);
	return ret;		
}
/******* Options parsing, main ********/
