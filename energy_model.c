#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>


#include "idlestat.h"
#include "topology.h"
#include "list.h"
#include "utils.h"


#define SYSFS_BASE /sys/class/hwmon/hwmon
enum hwmon_sensor {
	A15_ICORE = 0,
	A7_ICORE,
	DCC,
	A15_PCORE,
	A7_PCORE,
	A15_JCORE,
	A7_JCORE,
	MCC,

	/* This must be the last entry */
	HWMON_COUNT
};
static char *hwmon_file[] = {
	"curr1_input",
	"curr1_input",
	"temp1_input",
	"power1_input",
	"power1_input",
	"energy1_input",
	"energy1_input",
	"temp1_input",
};
static char *hwmon_desc[] = {
	"A15 Icore",
	"A7 Icore",
	"DCC",
	"A15 Pcore",
	"A7 Pcore",
	"A15 Jcore",
	"A7 Jcore",
	"MCC",
};
static unsigned long long hwmon_value[sizeof(hwmon_file)] = {0, };

int hwmon_dump(bool reset)
{
	char filename[] = "/sys/class/hwmon/hwmon0/device/pretty_long_filename";
	char buff[] = "123456789000";
	unsigned long long value, diff;
	FILE *f;
	int i;

	for (i = 0; i < HWMON_COUNT; ++i) {
		snprintf(filename+31, sizeof(filename)-31, "%s", hwmon_file[i]);
		filename[22] = '0' + i;

		f = fopen(filename, "r");
		if (!f) {
			fprintf(stderr, "Reading HWMON counter [%s] FAILED (Error: %d, %s)\n",
					filename, errno, strerror(errno));
			return -1;
		}
		/* Read current counter value */
		fgets(buff, sizeof(buff), f);
		value = strtoull(buff, NULL, 10);
		fclose(f);

		print_vrb("HWMON %12s (%-45s): %llu\n", hwmon_desc[i], filename, value);

		/* Initialize first value */
		if (reset) {
			hwmon_value[i] = value;
			continue;
		}

		/* Compute diff since last reading */
		diff = value - hwmon_value[i];
		hwmon_value[i] = diff;

	}

	return 0;
}

static FILE *f = NULL;
static char energy_dump_fn[] = "tc2_energy.dat";

int energy_dump_open()
{
	struct stat stats;

	f = fopen(energy_dump_fn, "a");
	if (!f) {
		fprintf(stderr, "Dump cluster energy FAILED (Error: %d, %s)\n",
				errno, strerror(errno));
		return -1;
	}

	/* Dump hearder if the file is new */
	if (fstat(fileno(f), &stats) == 0) {
		if (stats.st_size == 0)
			fprintf(f, "# %12s %14s %14s %14s %14s %14s %14s %14s %14s %14s %14s %14s\n",
				"A15_Cap", "A15_Idl", "A15_Wkp", "A15_Est", "A15_Mea",
				"A7_Cap", "A7_Idl", "A7_Wkp", "A7_Est", "A7_Mea",
				"TotModel", "TotMeasured");
	}

	return 0;
}

void energy_dump_cluster(double e_cap, double e_idl, double e_wkp, double i_tot, unsigned long long m_tot)
{
	if (!f)
		return;

	fprintf(f, "%14.0f %14.0f %14.0f %14.0f %14llu ",
			e_cap, e_idl, e_wkp,
			i_tot, m_tot);

}

void energy_dump_close(double i_tot, unsigned long long m_tot)
{
	if (!f)
		return;

	fprintf(f, "%14.0f %14llu\n", i_tot, m_tot);
	fclose(f);
}

static char buffer[BUFSIZE];

static struct cluster_energy_info *cluster_energy_table;
static unsigned int clusters_in_energy_file = 0;

static int make_energy_model_template(const char *path, const char *trace_file_path)
{
	FILE *f;
	struct cpuidle_datas *datas;
	struct cpu_physical *s_phy;
	struct cpu_core     *s_core;
	struct cpu_cpu      *s_cpu;
	unsigned int cluster_count = 0;
	unsigned int cluster_number = 0;

	f = fopen(path, "w+");
	if (!f)
		return -1;
	fprintf(f, "# This is a energy model template generated by idlestat\n");
	fprintf(f, "# Lines starting with # or which are blank are ignored\n");
	fprintf(f, "# Replace ? with correct values\n");

	init_cpu_topo_info();
	read_sysfs_cpu_topo();
	datas = idlestat_load(trace_file_path);
	if (!datas)
		return -1;
	establish_idledata_to_topo(datas);

	list_for_each_entry(s_phy, &g_cpu_topo_list.physical_head,
			    list_physical) {
		cluster_count++;
	}
	fprintf(f, "clusters %d\n", cluster_count);

	list_for_each_entry(s_phy, &g_cpu_topo_list.physical_head,
			    list_physical) {
		unsigned int num_cap_states = 0;
		unsigned int num_c_states = 0;
		unsigned int i;

		s_core = list_entry((&s_phy->core_head)->prev, struct cpu_core, list_core);
		s_cpu = list_entry((&s_core->cpu_head)->prev, struct cpu_cpu, list_cpu);
		num_cap_states = s_cpu->pstates->max;
		num_c_states = s_cpu->cstates->cstate_max;

		fprintf(f, "cluster%c: %d cap states %d C states\n\n", cluster_number + 'A', num_cap_states, num_c_states);
		fprintf(f, "P-states:\n");
		fprintf(f, "# speed, cluster power, core power\n");

		for (i = 0; i < s_cpu->pstates->max; i++) {
			struct cpufreq_pstate *p = &s_cpu->pstates->pstate[i];

			fprintf(f, "%d\t\t?\t?\n", p->freq/1000);
		}
		fprintf(f, "\nC-states:\n");
		fprintf(f, "# name, cluster power, core power\n");
		for (i = 0; i < s_cpu->cstates->cstate_max + 1; i++) {
			struct cpuidle_cstate *c = &s_cpu->cstates->cstate[i];

			fprintf(f, "%s\t\t?\t?\n", c->name);
		}
		fprintf(f, "\nwakeup\t\t?\t?\n");
		cluster_number++;
	}
	return 0;
}

int parse_energy_model(const char *path, const char *trace_file_path)
{
	FILE *f;
	char tmp;
	struct cluster_energy_info *clustp;
	unsigned int number_cap_states, number_c_states;
	int current_cluster = -1;
	unsigned int current_pstate;
	unsigned int current_cstate;
	unsigned int clust_p, core_p;
	int ret;

	f = fopen(path, "r");
	if (!f) {
		if (errno == ENOENT) {
			ret = make_energy_model_template(path, trace_file_path);
			exit(ret);
		}
		fprintf(stderr, "%s: failed to open '%s': %m\n", __func__, path);
		return -1;
	}

	fgets(buffer, BUFSIZE, f);

	do {
		if (buffer[0] == '#') continue;
		if (strlen(buffer) == 1) continue;

		if (strstr(buffer, "clusters")) {
			if (clusters_in_energy_file) {
				fprintf(stderr, "%s: number of clusters already specified in %s\n", __func__, path);
				return -1;
			}
			sscanf(buffer, "%*s %d", &clusters_in_energy_file);
			cluster_energy_table = calloc(sizeof(struct cluster_energy_info), clusters_in_energy_file);
			continue;
		}
		if (strstr(buffer, "cluster")) {
			sscanf(buffer, "cluster%c: %d cap states %d C states", &tmp, &number_cap_states, &number_c_states);
			current_cluster = tmp - 'A';
			if (current_cluster >= clusters_in_energy_file) {
				fprintf(stderr, "%s: cluster%c out of range in %s\n", __func__, tmp, path);
				return -1;
			}
			clustp = cluster_energy_table + current_cluster;
			if (clustp->number_cap_states) {
				fprintf(stderr, "%s: number of cap states for cluster%c already specified in %s\n", __func__, tmp, path);
				return -1;
			}
			clustp->number_cap_states = number_cap_states;
			clustp->number_c_states = number_c_states;
			clustp->p_energy = calloc(number_cap_states, sizeof(struct pstate_energy_info));
			clustp->c_energy = calloc(number_c_states, sizeof(struct cstate_energy_info));
			clustp->state = parsed_cluster_info;
			continue;
		}
		if (strstr(buffer, "P-states")) {
			if (current_cluster == -1) {
				fprintf(stderr, "%s: unknown cluster (cap states) in %s\n", __func__, path);
				return -1;
			}
			if (clustp->state < parsed_cluster_info) {
				fprintf(stderr, "%s: number of cap states for cluster%c not specified in %s\n", __func__, current_cluster, path);
				return -1;
			}
			current_pstate = 0;
			clustp->state = parsing_cap_states;
			continue;
		}
		if (strstr(buffer, "C-states")) {
			if (current_cluster == -1) {
				fprintf(stderr, "%s: unknown cluster (c states) in %s\n", __func__, path);
				return -1;
			}
			if (clustp->state < parsed_cluster_info) {
				fprintf(stderr, "%s: number of c states for cluster%c not specified in %s\n", __func__, current_cluster, path);
				return -1;
			}
			current_cstate = 0;
			clustp->state = parsing_c_states;
			continue;
		}
		if (strstr(buffer, "wakeup")) {
			unsigned int clust_w, core_w;

			if (current_cluster == -1) {
				fprintf(stderr, "%s: unknown cluster (wakeup) in %s\n", __func__, path);
				return -1;
			}
			sscanf(buffer, "%*s %d %d", &clust_w, &core_w);
			clustp->wakeup_energy.cluster_wakeup_energy = clust_w;
			clustp->wakeup_energy.core_wakeup_energy = core_w;
			continue;
		}
		if (!clustp) {
			fprintf(stderr, "%s: unknown cluster in %s\n", __func__, path);
			return -1;
			}
		if (clustp->state == parsing_cap_states) {
			struct pstate_energy_info *pp;
			unsigned int speed;

			if (sscanf(buffer, "%d %d %d", &speed, &clust_p, &core_p) != 3) {
				fprintf(stderr, "%s: expected P state (speed cluster core) for cluster%c in %s\n", __func__, current_cluster, path);
				return -1;
			}
			
			if (current_pstate >= clustp->number_cap_states) {
				fprintf(stderr, "%s: too many cap states specified for cluster%c in %s\n", __func__, current_cluster, path);
				return -1;
			}
			pp = &clustp->p_energy[current_pstate++];
			pp->speed = speed;
			pp->cluster_power = clust_p;
			pp->core_power = core_p;
			continue;
		}
		if (clustp->state == parsing_c_states) {
			char name[NAMELEN];
			struct cstate_energy_info *cp;

			if (sscanf(buffer, "%s %d %d", name, &clust_p, &core_p) != 3) {
				fprintf(stderr, "%s: expected C state (name cluster core) for cluster%c in %s\n", __func__, current_cluster, path);
				return -1;
			}

			if (current_cstate >= clustp->number_c_states) {
				fprintf(stderr, "%s: too many C states specified for cluster%c in %s\n", __func__, current_cluster, path);
				return -1;
			}
			cp = &clustp->c_energy[current_cstate++];
			strncpy(cp->cstate_name, name, NAMELEN);
			cp->cluster_idle_power = clust_p;
			cp->core_idle_power = core_p;
			continue;
		}
	} while (fgets(buffer, BUFSIZE, f));

	printf("parsed energy model file\n");
	return 0;
}

static struct cstate_energy_info *find_cstate_energy_info(const unsigned int cluster, const char *name)
{
	struct cluster_energy_info *clustp;
	struct cstate_energy_info *cp;
	int i;

	clustp = cluster_energy_table + cluster;
	cp = &clustp->c_energy[0];
	for (i = 0; i < clustp->number_c_states; i++, cp++) {
		if (!strcmp(cp->cstate_name, name)) return cp;
	}
	return NULL;
}

static struct pstate_energy_info *find_pstate_energy_info(const unsigned int cluster, const unsigned int speed)
{
	struct cluster_energy_info *clustp;
	struct pstate_energy_info *pp;
	int i;

	clustp = cluster_energy_table + cluster;
	pp = &clustp->p_energy[0];
	for (i = 0; i < clustp->number_cap_states; i++, pp++) {
		if (speed == pp->speed) return pp;
	}
	return NULL;
}

#define US_TO_SEC(US) (US / 1e6)

void calculate_energy_consumption(void)
{
	struct cpu_physical *s_phy;
	struct cpu_core     *s_core;
	struct cpu_cpu      *s_cpu;

	/* Overall energy breakdown */
	unsigned long long total_measured = 0.0;
	double total_energy = 0.0;
	double total_cap = 0.0;
	double total_idl = 0.0;
	double total_wkp = 0.0;

	/* Per cluster energy breakdown  */
	unsigned long long cluster_measured;
	double cluster_energy;
	double cluster_cap;
	double cluster_idl;
	double cluster_wkp;

	int i, j;
	unsigned int current_cluster;
	struct cstate_energy_info *cp;
	struct pstate_energy_info *pp;
	struct cluster_energy_info *clustp;


	energy_dump_open();

	/* Contributions are computed per Cluster */

	list_for_each_entry(s_phy, &g_cpu_topo_list.physical_head,
			    list_physical) {
		current_cluster = s_phy->physical_id;
		clustp = cluster_energy_table + current_cluster;

		cluster_measured = 0;
		cluster_energy = 0.0;
		cluster_cap = 0.0;
		cluster_idl = 0.0;
		cluster_wkp = 0.0;

		print_vrb("\n\nCluster%c%25s | %13s | %7s | %7s | %12s | %12s | %12s |\n",
				'A'+current_cluster, "", "[us] Duration", "Power", "Energy", "E_cap", "E_idle", "E_wkup");

		/* All C-States on current Cluster */

		for (j = 0; j < s_phy->cstates->cstate_max + 1; j++) {
			struct cpuidle_cstate *c = &s_phy->cstates->cstate[j];

			if (c->nrdata == 0) {
				/* print_vrb("      C%d +%6d hits for [%s]\n", */
				/* 		j, c->nrdata, c->name); */
				continue;
			}

			cp = find_cstate_energy_info(current_cluster, c->name);
			if (!cp) {
				/* print_vrb("      C%d no energy model for [%s] (%d hits, %f duration)\n", */
				/* 		j, c->name, c->nrdata, c->duration); */
				continue;
			}

			/* Cluster WU energy: defined just for wakeups from C1 */
			if (strcmp(c->name, "C1") == 0) {

				cluster_wkp += c->nrdata * clustp->wakeup_energy.cluster_wakeup_energy;

				print_vrb("      C%d +%7d wkps frm [%4s] | %13s | %7s | %7d | %12s | %12s | %12.0f |\n",
					j, c->nrdata, c->name,
					"", "",
					clustp->wakeup_energy.cluster_wakeup_energy,
					"", "",
					cluster_wkp);
			}

			/* NOTE: this keeps track of just the last C-State
			 * duration... are you assuming this is the C1
			 * residency time ?!? */
			/* The filtered trace is now accounting properly all the
			 * intervals in wich a CPUs is asking for C1 but it is
			 * actually only being in WFI. Since a WFI event is
			 * genarated in that case, it is not more qequired to
			 * compute that funny:
			 *    total_WFI - C1 on which cluster is not in C1
			 */
			cluster_idl += c->duration * cp->cluster_idle_power;

			print_vrb("      C%d +%7d hits for [%4s] | %13.0f | %7d | %7s | %12s | %12.0f | %12s |\n",
					j, c->nrdata, c->name,
					c->duration,
					cp->cluster_idle_power,
					"", "",
					cluster_idl,
					"");

		}

		/* Add current cluster wakeup energy contribution */
		/* This assumes that cluster wakeup hits equal the number of cluster C-States enter */

		/* All C-States and P-States for the CPUs on current Cluster */
		list_for_each_entry(s_core, &s_phy->core_head, list_core) {

			/* On big.LITTLE every Core has just one CPU */

			list_for_each_entry(s_cpu, &s_core->cpu_head,
					    list_cpu) {

				/* All C-States of current CPU */

				for (i = 0; i < s_cpu->cstates->cstate_max + 1; i++) {
					struct cpuidle_cstate *c = &s_cpu->cstates->cstate[i];
					if (c->nrdata == 0) {
						/* print_vrb("Cpu%d, C%d +%6d hits for [%4s]\n", */
						/* 	s_cpu->cpu_id, i, c->nrdata, c->name); */
						continue;
					}
					cp = find_cstate_energy_info(current_cluster, c->name);
					if (!cp) {
						/* print_vrb("Cpu%d, C%d no energy model for [%s] (%d hits, %f duration)\n", */
						/* 	s_cpu->cpu_id, i, c->name, */
						/* 	c->nrdata, c->duration); */
						continue;
					}
					cluster_idl += c->duration * cp->core_idle_power;

					print_vrb("Cpu%d  C%d +%7d hits for [%4s] | %13.0f | %7d | %7s | %12s | %12.0f | %12s |\n",
							s_cpu->cpu_id, i, c->nrdata, c->name,
							c->duration,
							cp->core_idle_power,
							"", "",
							cluster_idl,
							"");

					/* CPU WU energy: defined just for wakeups from WFI (on filtered trace) */
					if (strcmp(c->name, "WFI") == 0) {

						cluster_wkp += c->nrdata * clustp->wakeup_energy.core_wakeup_energy;

						print_vrb("Cpu%d  C%d +%6d wkps frm [%4s] | %13s | %7s | %7d | %12s | %12s | %12.0f |\n",
								s_cpu->cpu_id, i, c->nrdata, c->name,
								"", "",
								clustp->wakeup_energy.core_wakeup_energy,
								"", "",
								cluster_idl);
					}


				}

				/* All P-States of current CPU */

				for (i = 0; i < s_cpu->pstates->max; i++) {
					struct cpufreq_pstate *p = &s_cpu->pstates->pstate[i];

					if (p->count == 0) {
						/* print_vrb("Cpu%d, P%d +%6d hits for [%d]\n", */
						/* 	s_cpu->cpu_id, i, p->count, p->freq/1000); */
						continue;
					}
					pp = find_pstate_energy_info(current_cluster, p->freq/1000);
					if (!pp) {
						/* print_vrb("Cpu%d, P%d no energy model for [%d] (%d hits, %f duration)\n", */
						/* 	s_cpu->cpu_id, i, p->freq/1000, */
						/* 	p->count, p->duration); */
						continue;
					}

					pp->max_core_duration = MAX(p->duration, pp->max_core_duration);

					cluster_cap += p->duration * pp->core_power;

					print_vrb("Cpu%d  P%d +%7d hits for [%4d] | %13.0f | %7d | %7s | %12.0f | %12s | %12s |\n",
							s_cpu->cpu_id, i, p->count, p->freq/1000,
							p->duration, pp->core_power,
							"",
							cluster_cap,
							"", "");
				}
			}
		}
		/*
		 * XXX
		 * No cluster P-state duration info available yet, so estimate this
		 * as the maximum of the durations of its cores at that frequency.
		 */
		for (i = 0; i < clustp->number_cap_states; i++) {
			pp = &clustp->p_energy[i];
			cluster_cap += pp->max_core_duration * pp->cluster_power;

			print_vrb("      P%d casp estimate for [%4d] | %13.0f | %7d | %7s | %12.0f | %12s | %12s |\n",
					clustp->number_cap_states - i - 1, pp->speed,
					pp->max_core_duration,
					pp->cluster_power,
					"",
					cluster_cap,
					"", "");
		}

		/* The scheduler model is normalized to a wake-up rate of 1000 wake-ups per
		 * second. Thus that conversion is required:
		 *
		 * idlestate_wakeup_energy = sched_wakeup_energy * 47742 / (1000 * 1024)
		 *
		 * where:
		 * - 47742: Removes wake-up tracking pre-scaling.
		 * -  1024: Removes << 10
		 * -  1000: Single wake-up instead of 1000/s.
		 */
		cluster_wkp *= 47742;
		cluster_wkp /= (1024 * 1000);


		printf("\n");

		print_vrb("\n\nCluster%c%25s | %13s | %7s | %7s | %12s | %12s | %12s |\n",
				'A'+current_cluster, "", "[us] Duration", "Power", "Energy", "E_cap", "E_idle", "E_wkup");

		/* Convert all [us] components to [s] just here to avoid summing
		 * truncation errors due to small components */
		cluster_cap = US_TO_SEC(cluster_cap);
		cluster_idl = US_TO_SEC(cluster_idl);

		printf("Cluster%c Energy Caps  %14.0f (%e)\n",
				'A'+current_cluster, cluster_cap, cluster_cap);
		total_cap += cluster_cap;

		printf("Cluster%c Energy Idle  %14.0f (%e)\n",
				'A'+current_cluster, cluster_idl, cluster_idl);
		total_idl += cluster_idl;

		printf("Cluster%c Energy Wkps  %14.0f (%e)\n",
				'A'+current_cluster, cluster_wkp, cluster_wkp);
		total_wkp += cluster_wkp;

		cluster_energy = cluster_cap + cluster_idl + cluster_wkp;
		total_energy += cluster_energy;

		cluster_measured = hwmon_value[A15_JCORE + current_cluster];
		total_measured += cluster_measured;

		printf("Cluster%c Energy Index %14.0f (%e) - Energy Measure: %14llu\n",
				'A'+current_cluster,
				cluster_energy, cluster_energy,
				cluster_measured);


		energy_dump_cluster(
				cluster_cap, cluster_idl, cluster_wkp,
				cluster_energy, hwmon_value[A15_JCORE + current_cluster]);

	}

	printf("\n   Total Energy Index %14.0f (%e) - Energy Measure: %14llu\n\n\n",
			total_energy, total_energy, total_measured);

	energy_dump_close(total_energy, total_measured);
}
