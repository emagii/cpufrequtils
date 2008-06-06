/*
 *  (C) 2004  Dominik Brodowski <linux@dominikbrodowski.de>
 *
 *  Licensed under the terms of the GNU GPL License version 2.
 */

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include <sysfs/libsysfs.h>

#include "cpufreq.h"

#define PATH_TO_CPU "/sys/devices/system/cpu/"

/* read access to files which contain one numeric value */

enum {
	CPUINFO_CUR_FREQ,
	CPUINFO_MIN_FREQ,
	CPUINFO_MAX_FREQ,
	SCALING_CUR_FREQ,
	SCALING_MIN_FREQ,
	SCALING_MAX_FREQ,
	MAX_VALUE_FILES
};

static const char *value_files[MAX_VALUE_FILES] = {
	[CPUINFO_CUR_FREQ] = "cpuinfo_cur_freq",
	[CPUINFO_MIN_FREQ] = "cpuinfo_min_freq",
	[CPUINFO_MAX_FREQ] = "cpuinfo_max_freq",
	[SCALING_CUR_FREQ] = "scaling_cur_freq",
	[SCALING_MIN_FREQ] = "scaling_min_freq",
	[SCALING_MAX_FREQ] = "scaling_max_freq",
};


static unsigned long sysfs_get_one_value(unsigned int cpu, unsigned int which)
{
	char file[SYSFS_PATH_MAX];
	struct sysfs_attribute *attr;
 	unsigned long value;
	char *endp;

	if ( which >= MAX_VALUE_FILES )
		return 0;

	snprintf(file, SYSFS_PATH_MAX, PATH_TO_CPU "cpu%u/cpufreq/%s", 
			 cpu, value_files[which]);

	if ( ( attr = sysfs_open_attribute(file) ) == NULL )
		return 0;

	if ( sysfs_read_attribute(attr) || attr->value == NULL || attr->len == 0 )
	{
		sysfs_close_attribute(attr);
		return 0;
	}

	value = strtoul(attr->value, &endp, 0);

	if ( endp == attr->value ||  errno == ERANGE)
	{
		sysfs_close_attribute(attr);
		return 0;
	}

	sysfs_close_attribute(attr);

	return value;
}

/* read access to files which contain one string */

enum {
	SCALING_DRIVER,
	SCALING_GOVERNOR,
	MAX_STRING_FILES
};

static const char *string_files[MAX_STRING_FILES] = {
	[SCALING_DRIVER] = "scaling_driver",
	[SCALING_GOVERNOR] = "scaling_governor",
};


static char * sysfs_get_one_string(unsigned int cpu, unsigned int which)
{
	char file[SYSFS_PATH_MAX];
	struct sysfs_attribute *attr;
	char * result;

	if (which >= MAX_STRING_FILES)
		return NULL;

	snprintf(file, SYSFS_PATH_MAX, PATH_TO_CPU "cpu%u/cpufreq/%s", 
			 cpu, string_files[which]);

	if ( ( attr = sysfs_open_attribute(file) ) == NULL )
		return NULL;

	if ( sysfs_read_attribute(attr) || attr->value == NULL ||
		 attr->len >= SYSFS_PATH_MAX ||
		 ( result = malloc(attr->len + 1) ) == NULL )
	{
		sysfs_close_attribute(attr);
		return NULL;
	}

	memcpy(result, attr->value, attr->len);
	result[attr->len] = '\0';
	if (result[attr->len - 1] == '\n')
		result[attr->len - 1] = '\0';

	sysfs_close_attribute(attr);

	return result;
}

/* write access */

enum {
	WRITE_SCALING_MIN_FREQ,
	WRITE_SCALING_MAX_FREQ,
	WRITE_SCALING_GOVERNOR,
	WRITE_SCALING_SET_SPEED,
	MAX_WRITE_FILES
};

static const char *write_files[MAX_VALUE_FILES] = {
	[WRITE_SCALING_MIN_FREQ] = "scaling_min_freq",
	[WRITE_SCALING_MAX_FREQ] = "scaling_max_freq",
	[WRITE_SCALING_GOVERNOR] = "scaling_governor",
	[WRITE_SCALING_SET_SPEED] = "scaling_setspeed",
};

static int sysfs_write_one_value(unsigned int cpu, unsigned int which,
				 const char *new_value, size_t len)
{
	int ret;
	char file[SYSFS_PATH_MAX];
	struct sysfs_attribute *attr;

	if (which >= MAX_WRITE_FILES)
		return 0;

	snprintf(file, SYSFS_PATH_MAX, PATH_TO_CPU "cpu%u/cpufreq/%s", 
			 cpu, write_files[which]);

	attr = sysfs_open_attribute(file);
	if (!attr)
		return -ENODEV;

	ret = sysfs_write_attribute(attr, new_value, len);

	sysfs_close_attribute(attr);

	return ret;
};


int sysfs_cpu_exists(unsigned int cpu)
{
	char file[SYSFS_PATH_MAX];

	snprintf(file, SYSFS_PATH_MAX, PATH_TO_CPU "cpu%u/", cpu);

	return sysfs_path_is_dir(file);
}


unsigned long sysfs_get_freq_kernel(unsigned int cpu)
{
	return sysfs_get_one_value(cpu, SCALING_CUR_FREQ);
}

unsigned long sysfs_get_freq_hardware(unsigned int cpu)
{
	return sysfs_get_one_value(cpu, CPUINFO_CUR_FREQ);
}

int sysfs_get_hardware_limits(unsigned int cpu, 
			      unsigned long *min, 
			      unsigned long *max)
{
	if ((!min) || (!max))
		return -EINVAL;

	*min = sysfs_get_one_value(cpu, CPUINFO_MIN_FREQ);
	if (!*min)
		return -ENODEV;

	*max = sysfs_get_one_value(cpu, CPUINFO_MAX_FREQ);
	if (!*max)
		return -ENODEV;

	return 0;
}

char * sysfs_get_driver(unsigned int cpu) {
	return sysfs_get_one_string(cpu, SCALING_DRIVER);
}

struct cpufreq_policy * sysfs_get_policy(unsigned int cpu) {
	struct cpufreq_policy *policy;

	policy = malloc(sizeof(struct cpufreq_policy));
	if (!policy)
		return NULL;

	policy->governor = sysfs_get_one_string(cpu, SCALING_GOVERNOR);
	if (!policy->governor) {
		free(policy);
		return NULL;
	}
	policy->min = sysfs_get_one_value(cpu, SCALING_MIN_FREQ);
	policy->max = sysfs_get_one_value(cpu, SCALING_MAX_FREQ);
	if ((!policy->min) || (!policy->max)) {
		free(policy->governor);
		free(policy);
		return NULL;
	}

	return policy;
}

struct cpufreq_available_governors * sysfs_get_available_governors(unsigned int cpu) {
	struct cpufreq_available_governors *first = NULL;
	struct cpufreq_available_governors *current = NULL;
	char file[SYSFS_PATH_MAX];
	struct sysfs_attribute *attr;
	unsigned int pos, i;

	snprintf(file, SYSFS_PATH_MAX, PATH_TO_CPU "cpu%u/cpufreq/scaling_available_governors", cpu);

	if ( ( attr = sysfs_open_attribute(file) ) == NULL )
		return NULL;

	if ( sysfs_read_attribute(attr) || attr->value == NULL )
	{
		sysfs_close_attribute(attr);
		return NULL;
	}

	pos = 0;
	for ( i = 0; i < attr->len; i++ )
	{
		if ( i == attr->len || attr->value[i] == ' ' ||
			 attr->value[i] == '\0' || attr->value[i] == '\n' )
		{
			if ( i - pos < 2 )
				continue;
			if ( current ) {
				current->next = malloc(sizeof *current );
				if ( ! current->next )
					goto error_out;
				current = current->next;
			} else {
				first = malloc( sizeof *first );
				if ( ! first )
					goto error_out;
				current = first;
			}
			current->first = first;
			current->next = NULL;

			current->governor = malloc(i - pos + 1);
			if ( ! current->governor )
				goto error_out;

			memcpy( current->governor, attr->value + pos, i - pos);
			current->governor[i - pos] = '\0';
			pos = i + 1;
		}
	}

	sysfs_close_attribute(attr);
	return first;

 error_out:
	while ( first ) {
		current = first->next;
		if ( first->governor )
			free( first->governor );
		free( first );
		first = current;
	}
	sysfs_close_attribute(attr);
	return NULL;
}


struct cpufreq_available_frequencies * sysfs_get_available_frequencies(unsigned int cpu) {
	struct cpufreq_available_frequencies *first = NULL;
	struct cpufreq_available_frequencies *current = NULL;
	char file[SYSFS_PATH_MAX];
	struct sysfs_attribute *attr;
	char one_value[SYSFS_PATH_MAX];
	unsigned int pos, i;

	snprintf(file, SYSFS_PATH_MAX, PATH_TO_CPU "cpu%u/cpufreq/scaling_available_frequencies", cpu);

	if ( ( attr = sysfs_open_attribute(file) ) == NULL )
		return NULL;

	if ( sysfs_read_attribute(attr) || attr->value == NULL )
	{
		sysfs_close_attribute(attr);
		return NULL;
	}

	pos = 0;
	for ( i = 0; i < attr->len; i++ )
	{
		if ( i == attr->len || attr->value[i] == ' ' ||
			 attr->value[i] == '\0' || attr->value[i] == '\n' )
		{
			if ( i - pos < 2 )
				continue;
			if ( i - pos >= SYSFS_PATH_MAX )
				goto error_out;
			if ( current ) {
				current->next = malloc(sizeof *current );
				if ( ! current->next )
					goto error_out;
				current = current->next;
			} else {
				first = malloc(sizeof *first );
				if ( ! first )
					goto error_out;
				current = first;
			}
			current->first = first;
			current->next = NULL;

			memcpy(one_value, attr->value + pos, i - pos);
			one_value[i - pos] = '\0';
			if ( sscanf(one_value, "%lu", &current->frequency) != 1 )
				goto error_out;

			pos = i + 1;
		}
	}

	sysfs_close_attribute(attr);
	return first;

 error_out:
	while ( first ) {
		current = first->next;
		free(first);
		first = current;
	}
	sysfs_close_attribute(attr);
	return NULL;
}

struct cpufreq_affected_cpus * sysfs_get_affected_cpus(unsigned int cpu) {
	struct cpufreq_affected_cpus *first = NULL;
	struct cpufreq_affected_cpus *current = NULL;
	char file[SYSFS_PATH_MAX];
	struct sysfs_attribute *attr;
	char one_value[SYSFS_PATH_MAX];
	unsigned int pos, i;

	snprintf(file, SYSFS_PATH_MAX, PATH_TO_CPU "cpu%u/cpufreq/affected_cpus", cpu);

	if ( ( attr = sysfs_open_attribute(file) ) == NULL )
		return NULL;

	if ( sysfs_read_attribute(attr) || attr->value == NULL )
	{
		sysfs_close_attribute(attr);
		return NULL;
	}

	pos = 0;
	for ( i = 0; i < attr->len; i++ )
	{
		if ( i == attr->len  || attr->value[i] == ' ' ||
			 attr->value[i] == '\0' || attr->value[i] == '\n' )
		{
			if ( i - pos  < 1 )
				continue;
			if ( i - pos >= SYSFS_PATH_MAX )
				goto error_out;
			if ( current ) {
				current->next = malloc(sizeof *current);
				if ( ! current->next )
					goto error_out;
				current = current->next;
			} else {
				first = malloc(sizeof *first);
				if ( ! first )
					goto error_out;
				current = first;
			}
			current->first = first;
			current->next = NULL;

			memcpy(one_value, attr->value + pos, i - pos);
			one_value[i - pos] = '\0';

			if ( sscanf(one_value, "%u", &current->cpu) != 1 )
				goto error_out;

			pos = i + 1;
		}
	}

	sysfs_close_attribute(attr);
	return first;

 error_out:
	while (first) {
		current = first->next;
		free(first);
		first = current;
	}
	sysfs_close_attribute(attr);
	return NULL;
}

static int verify_gov(char *new_gov, char *passed_gov)
{
	unsigned int i, j=0;

	if (!passed_gov || (strlen(passed_gov) > 19))
		return -EINVAL;

	strncpy(new_gov, passed_gov, 20);
	for (i=0;i<20;i++) {
		if (j) {
			new_gov[i] = '\0';
			continue;
		}
		if ((new_gov[i] >= 'a') && (new_gov[i] <= 'z')) {
			continue;
		}
		if ((new_gov[i] >= 'A') && (new_gov[i] <= 'Z')) {
			continue;
		}
		if (new_gov[i] == '-') {
			continue;
		}
		if (new_gov[i] == '_') {
			continue;
		}
		if (new_gov[i] == '\0') {
			j = 1;
			continue;
		}
		return -EINVAL;
	}
	new_gov[19] = '\0';
	return 0;
}

int sysfs_modify_policy_governor(unsigned int cpu, char *governor)
{
	char new_gov[SYSFS_PATH_MAX];

	if (!governor)
		return -EINVAL;

	if (verify_gov(new_gov, governor))
		return -EINVAL;

	return sysfs_write_one_value(cpu, WRITE_SCALING_GOVERNOR, new_gov, strlen(new_gov));
};

int sysfs_modify_policy_max(unsigned int cpu, unsigned long max_freq)
{
	char value[SYSFS_PATH_MAX];

	snprintf(value, SYSFS_PATH_MAX, "%lu", max_freq);

	return sysfs_write_one_value(cpu, WRITE_SCALING_MAX_FREQ, value, strlen(value));
};


int sysfs_modify_policy_min(unsigned int cpu, unsigned long min_freq)
{
	char value[SYSFS_PATH_MAX];

	snprintf(value, SYSFS_PATH_MAX, "%lu", min_freq);

	return sysfs_write_one_value(cpu, WRITE_SCALING_MIN_FREQ, value, strlen(value));
};


int sysfs_set_policy(unsigned int cpu, struct cpufreq_policy *policy)
{
	char min[SYSFS_PATH_MAX];
	char max[SYSFS_PATH_MAX];
	char gov[SYSFS_PATH_MAX];
	int ret;

	if (!policy || !(policy->governor))
		return -EINVAL;

	if (policy->max < policy->min)
		return -EINVAL;

	if (verify_gov(gov, policy->governor))
		return -EINVAL;

	snprintf(min, SYSFS_PATH_MAX, "%lu", policy->min);
	snprintf(max, SYSFS_PATH_MAX, "%lu", policy->max);

	ret = sysfs_write_one_value(cpu, WRITE_SCALING_MAX_FREQ, max, strlen(max));
	if (ret)
		return ret;

	ret = sysfs_write_one_value(cpu, WRITE_SCALING_MIN_FREQ, min, strlen(min));
	if (ret)
		return ret;

	return sysfs_write_one_value(cpu, WRITE_SCALING_GOVERNOR, gov, strlen(gov));
}

int sysfs_set_frequency(unsigned int cpu, unsigned long target_frequency) {
	struct cpufreq_policy *pol = sysfs_get_policy(cpu);
	char userspace_gov[] = "userspace";
	char freq[SYSFS_PATH_MAX];
	int ret;

	if (!pol)
		return -ENODEV;

	if (strncmp(pol->governor, userspace_gov, 9) != 0) {
		ret = sysfs_modify_policy_governor(cpu, userspace_gov);
		if (ret) {
			cpufreq_put_policy(pol);
			return (ret);
		}
	}

	cpufreq_put_policy(pol);

	snprintf(freq, SYSFS_PATH_MAX, "%lu", target_frequency);

	return sysfs_write_one_value(cpu, WRITE_SCALING_SET_SPEED, freq, strlen(freq));
}
