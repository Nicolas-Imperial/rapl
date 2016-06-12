/*
Copyright (c) 2012, Intel Corporation

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

 * Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
 * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
 * Neither the name of Intel Corporation nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* Written by Martin Dimitrov, Carl Strickland */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <sys/time.h>
#include <time.h>
#include <signal.h>
#include <pthread.h>
#include <omp.h>

#include "rapl.h"
// #include "matrix_test.h"

char         *progname;
char         *cmd_exe;
int 	      command_given = 0;
int 	      run_internal = 0;
const char   *version = "2.2";
uint64_t      num_node = 0;
uint64_t      delay_us = 1000000;
double        duration = 0.0;
// double        delay_unit = 1000000.0;
static volatile int finished = 0;





//////////////////////////////////////////////////////////////////////////////////////////////////

/*
  For more complicated algorithms it would probably be better to
  move all pre setup stuff to another file and link it together in the makefile.
*/


int** MatrixA;
int** MatrixB;
int** MatrixC;

long  MatrixSize = 1024;



/* Example multiplication of Matrix. 
 Which we DO want to be included in the profiling.
*/


int normal_threaded( int** MatrixA, int** MatrixB, int** MatrixResult, int MatrixSize )
{
	int i, j, k;
	omp_set_dynamic(0);
	omp_set_num_threads(4);


#pragma omp parallel for collapse(2) private (k)
	for (i=0;i<MatrixSize ;i++) {
		for (j=0;j<MatrixSize ;j++) {
			MatrixResult[i][j]=0;
			for (k=0;k<MatrixSize ;k++) {
				MatrixResult[i][j]=MatrixResult[i][j]+MatrixA[i][k]*MatrixB[k][j];
			}
		}
	}
	return 0;
}

void fill_matrix( int** MatrixA, int** MatrixB, int length)
{
	int row, column;
	for(row = 0; row<length; row++)
	{
		for(column = 0; column<length; column++)
		{

			MatrixB[row][column] = (MatrixA[row][column] = rand() %5);
			//matrix2[row][column] = rand() % 2;//ba hazfe in khat 50% afzayeshe soorat khahim dasht
		}

	}
}

/* Example Initialization of Matrix. 
 Which we don't want to be included in the profiling.
 So call it before starting sampling in 
 do_print_energy_info() */

void matMultInit(){


	MatrixA = (int**)(malloc(MatrixSize*(sizeof(int*))));
	MatrixB = (int**)(malloc(MatrixSize*(sizeof(int*))));
	MatrixC = (int**)(malloc(MatrixSize*(sizeof(int*))));

	int i;
	for (i = 0; i < MatrixSize; i++)
	{
		MatrixA[i] = (int*)(malloc(MatrixSize*(sizeof(int))));;
		MatrixB[i] = (int*)(malloc(MatrixSize*(sizeof(int))));;
		MatrixC[i] = (int*)(malloc(MatrixSize*(sizeof(int))));;
	}

	fill_matrix(MatrixA,MatrixB,MatrixSize);

}


///////////////////////////////////////////////////////////////////////////////////////////////////////








/*This function is what gets profiled right now. Anything you put in here
  will contribute to the profiling data.*/

void *MyFunc(void *threadid){

        /* If you want to run an executable from command line use this
        cmd_exe will contain the command provided with -c option
        i.e. power.x -c "my_exe 1024"
        */
	if(command_given) {
	    int ok = system(cmd_exe);
	    if(ok < 0)
	    {
		fprintf(stderr, "Could not start child process with command \"%s\"\n", cmd_exe);
            }
	}
	/* Otherwise we must be running an internal algorithm. Which will be called here. */
	else if(run_internal){
	    // normal(MatrixA,MatrixB,MatrixC,MatrixSize);
	    // Strassen(MatrixSize,MatrixA,MatrixB,MatrixC);
	    normal_threaded(MatrixA,MatrixB,MatrixC,MatrixSize);
	    // tiled(MatrixA,MatrixB,MatrixC,MatrixSize);
	    // tiled_threaded(MatrixA,MatrixB,MatrixC,MatrixSize);
	}
        else {
	    printf("Confused. \n");	
	}

	finished = 1;
}



double
get_rapl_energy_info(uint64_t power_domain, uint64_t node)
{
	int          err;
	double       total_energy_consumed;

	switch (power_domain) {
	case PKG:
		err = get_pkg_total_energy_consumed(node, &total_energy_consumed);
		break;
	case PP0:
		err = get_pp0_total_energy_consumed(node, &total_energy_consumed);
		break;
	case PP1:
		err = get_pp1_total_energy_consumed(node, &total_energy_consumed);
		break;
	case DRAM:
		err = get_dram_total_energy_consumed(node, &total_energy_consumed);
		break;
	default:
		err = MY_ERROR;
		break;
	}

	return total_energy_consumed;
}

void
convert_time_to_string(struct timeval tv, char* time_buf)
{
	time_t sec;
	int msec;
	struct tm *timeinfo;
	char tmp_buf[9];

	sec = tv.tv_sec;
	timeinfo = localtime(&sec);
	msec = tv.tv_usec/1000;

	strftime(tmp_buf, 9, "%H:%M:%S", timeinfo);
	sprintf(time_buf, "%s:%d",tmp_buf,msec);
}

double
convert_time_to_sec(struct timeval tv)
{
	double elapsed_time = (double)(tv.tv_sec) + ((double)(tv.tv_usec)/1000000);
	return elapsed_time;
}


void
do_print_energy_info()
{
	int i = 0;
	int domain = 0;
	uint64_t node = 0;
	double new_sample;
	double delta;
	double power;

	double prev_sample[num_node][RAPL_NR_DOMAIN];
	double power_watt[num_node][RAPL_NR_DOMAIN];
	double cum_energy_J[num_node][RAPL_NR_DOMAIN];
	double cum_energy_mWh[num_node][RAPL_NR_DOMAIN];

	char time_buffer[32];
	struct timeval tv;
	int msec;
	uint64_t tsc;
	uint64_t freq;
	double start, end, interval_start;
	double total_elapsed_time;
	double interval_elapsed_time;


	/* For example Initialize the matrices here */ 
	matMultInit();


	/* don't buffer if piped */
	setbuf(stdout, NULL);

	/* Print header */
	fprintf(stdout, "System Time,RDTSC,Elapsed Time (sec),");
	for (i = node; i < num_node; i++) {
		fprintf(stdout, "IA Frequency_%d (MHz),",i);
		if(is_supported_domain(RAPL_PKG))
			fprintf(stdout,"Processor Power_%d (Watt),Cumulative Processor Energy_%d (Joules),Cumulative Processor Energy_%d (mWh),", i,i,i);
		if(is_supported_domain(RAPL_PP0))
			fprintf(stdout, "IA Power_%d (Watt),Cumulative IA Energy_%d (Joules),Cumulative IA Energy_%d(mWh),", i,i,i);
		if(is_supported_domain(RAPL_PP1))
			fprintf(stdout, "GT Power_%d (Watt),Cumulative GT Energy_%d (Joules),Cumulative GT Energy_%d(mWh)", i,i,i);
		if(is_supported_domain(RAPL_DRAM))
			fprintf(stdout, "DRAM Power_%d (Watt),Cumulative DRAM Energy_%d (Joules),Cumulative DRAM Energy_%d(mWh),", i,i,i);
	}
	fprintf(stdout, "\n");

	/* Read initial values */
	for (i = node; i < num_node; i++) {
		for (domain = 0; domain < RAPL_NR_DOMAIN; ++domain) {
			if(is_supported_domain(domain)) {
				prev_sample[i][domain] = get_rapl_energy_info(domain, i);
			}
		}
	}

	gettimeofday(&tv, NULL);
	start = convert_time_to_sec(tv);
	end = start;


	/*
	 One thread is launched to run the computation. The current thread will do the 
	 profiling in the while loop until this thread returns from the function. i.e. 
	 finished == 1;
	*/
	/* If duration is given on command line just profile the processor
           for the given amount of time(whatever it is doing). No specific algorithm or executble is used.
        */
	
	if(duration == 0.0) {
	    pthread_t threads[1];
	    finished = 0;
	    int tid = pthread_create(&threads[0], NULL, MyFunc, (void *)0);
	}


	/* Begin sampling */
	while (1) {

		usleep(delay_us);

		gettimeofday(&tv, NULL);
		interval_start = convert_time_to_sec(tv);
		interval_elapsed_time = interval_start - end;

		for (i = node; i < num_node; i++) {
			for (domain = 0; domain < RAPL_NR_DOMAIN; ++domain) {
				if(is_supported_domain(domain)) {
					new_sample = get_rapl_energy_info(domain, i);
					delta = new_sample - prev_sample[i][domain];

					/* Handle wraparound */
					if (delta < 0) {
						delta += MAX_ENERGY_STATUS_JOULES;
					}

					prev_sample[i][domain] = new_sample;

					// Use the computed elapsed time between samples (and not
					// just the sleep delay, in order to more accourately account for
					// the delay between samples
					power_watt[i][domain] = delta / interval_elapsed_time;
					cum_energy_J[i][domain] += delta;
					cum_energy_mWh[i][domain] = cum_energy_J[i][domain] / 3.6; // mWh
				}
			}
		}

		gettimeofday(&tv, NULL);
		end = convert_time_to_sec(tv);
		total_elapsed_time = end - start;
		convert_time_to_string(tv, time_buffer);

		read_tsc(&tsc);
		fprintf(stdout,"%s,%" PRIu64 ",%.4lf,", time_buffer, tsc, total_elapsed_time);
		for (i = node; i < num_node; i++) {
			get_pp0_freq_mhz(i, &freq);
			fprintf(stdout, "%" PRIu64 ",", freq);
			for (domain = 0; domain < RAPL_NR_DOMAIN; ++domain) {
				if(is_supported_domain(domain)) {
					fprintf(stdout, "%.4lf,%.4lf,%.4lf,",
							power_watt[i][domain], cum_energy_J[i][domain], cum_energy_mWh[i][domain]);
				}
			}
		}
		fprintf(stdout, "\n");


		/* if we have duration given then we are not running any specific algorithm. 
		check to see if time is up. */
		if(duration != 0) {
        	    if(total_elapsed_time >= duration)
		    break;
		}
		/*Otherwise see if the computation is done*/
		else {
		   if (finished == 1)
			break;
		}

	}

	end = clock();

	/* Print summary */
	fprintf(stdout, "\nTotal Elapsed Time(sec)=%.4lf\n\n", total_elapsed_time);
	for (i = node; i < num_node; i++) {
		if(is_supported_domain(RAPL_PKG)){
			fprintf(stdout, "Total Processor Energy_%d(Joules)=%.4lf\n", i, cum_energy_J[i][RAPL_PKG]);
			fprintf(stdout, "Total Processor Energy_%d(mWh)=%.4lf\n", i, cum_energy_mWh[i][RAPL_PKG]);
			fprintf(stdout, "Average Processor Power_%d(Watt)=%.4lf\n\n", i, cum_energy_J[i][RAPL_PKG]/total_elapsed_time);
		}
		if(is_supported_domain(RAPL_PP0)){
			fprintf(stdout, "Total IA Energy_%d(Joules)=%.4lf\n", i, cum_energy_J[i][RAPL_PP0]);
			fprintf(stdout, "Total IA Energy_%d(mWh)=%.4lf\n", i, cum_energy_mWh[i][RAPL_PP0]);
			fprintf(stdout, "Average IA Power_%d(Watt)=%.4lf\n\n", i, cum_energy_J[i][RAPL_PP0]/total_elapsed_time);
		}
		if(is_supported_domain(RAPL_PP1)){
			fprintf(stdout, "Total GT Energy_%d(Joules)=%.4lf\n", i, cum_energy_J[i][RAPL_PP1]);
			fprintf(stdout, "Total GT Energy_%d(mWh)=%.4lf\n", i, cum_energy_mWh[i][RAPL_PP1]);
			fprintf(stdout, "Average GT Power_%d(Watt)=%.4lf\n\n", i, cum_energy_J[i][RAPL_PP1]/total_elapsed_time);
		}
		if(is_supported_domain(RAPL_DRAM)){
			fprintf(stdout, "Total DRAM Energy_%d(Joules)=%.4lf\n", i, cum_energy_J[i][RAPL_DRAM]);
			fprintf(stdout, "Total DRAM Energy_%d(mWh)=%.4lf\n", i, cum_energy_mWh[i][RAPL_DRAM]);
			fprintf(stdout, "Average DRAM Power_%d(Watt)=%.4lf\n\n", i, cum_energy_J[i][RAPL_DRAM]/total_elapsed_time);
		}
	}
	read_tsc(&tsc);
	fprintf(stdout,"TSC=%" PRIu64 "\n", tsc);
}

void
usage()
{
	fprintf(stdout, "\nIntel(r) Power Gadget %s\n", version);
	fprintf(stdout, "\nUsage: \n");
	fprintf(stdout, "%s [-e [sampling delay (ms) ] optional] -d [duration (sec)]\n", progname);
	fprintf(stdout, "%s [-e [sampling delay (ms) ] optional] -c [command_to_run]\n", progname);
	fprintf(stdout, "%s [-e [sampling delay (ms) ] optional] -i. To run internal algorithm\n", progname);
	fprintf(stdout, "\nExample: %s -e 1000 -d 10\n", progname);
	fprintf(stdout, "\nExample: %s -e 1000 -i\n", progname);
	fprintf(stdout, "\nExample: %s -e 1000 -c \"myprogram -myflag\"\n", progname);
	fprintf(stdout, "\n");
}


int
cmdline(int argc, char **argv)
{
	int             opt;
	uint64_t    delay_ms_temp = 1000;

	progname = argv[0];

	while ((opt = getopt(argc, argv, "e:d:c:i")) != -1) {
		switch (opt) {
		case 'e':
			delay_ms_temp = atoi(optarg);
			if(delay_ms_temp > 50) {
				delay_us = delay_ms_temp * 1000;
			} else {
				fprintf(stdout, "Sampling delay must be greater than 50 ms.\n");
				return -1;
			}
			break;
		case 'd':
			duration = atof(optarg);
			if(duration <= 0.0){
				fprintf(stdout, "Duration must be greater than 0 seconds.\n");
				return -1;
			}
			break;
		case 'i':
			run_internal = 1;
			break;
		case 'c':
			cmd_exe = argv[argc - 1];
			command_given = 1;
			break;
		case 'h':
			usage();
			exit(0);
			break;
		default:
			usage();
			return -1;
		}
	}
	return 0;
}

void sigint_handler(int signum)
{
	terminate_rapl();
	exit(0);
}

int
main(int argc, char **argv)
{
	int i = 0;
	int ret = 0;

	/* Clean up if we're told to exit */
	signal(SIGINT, sigint_handler);

	if (argc < 2) {
		usage();
		// terminate_rapl();
		return 0;
	}

	// First init the RAPL library
	if (0 != init_rapl()) {
		fprintf(stdout, "Init failed!\n");
		terminate_rapl();
		return MY_ERROR;
	}
	num_node = get_num_rapl_nodes_pkg();

	ret = cmdline(argc, argv);
	if (ret) {
		terminate_rapl();
		return ret;
	}

	do_print_energy_info();

	terminate_rapl();

	pthread_exit(NULL);
}
