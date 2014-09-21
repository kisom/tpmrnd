/*
 * Copyright (c) 2014 by Kyle Isom <kyle@tyrfingr.is>.
 *
 * Permission to use, copy, modify, and distribute this software for
 * any purpose with or without fee is hereby granted, provided that
 * the above copyright notice and this permission notice appear in
 * all copies.

 * THE SOFTWARE IS PROVIDED "AS IS" AND INTERNET SOFTWARE CONSORTIUM
 * DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING
 * ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT
 * SHALL INTERNET SOFTWARE CONSORTIUM BE LIABLE FOR ANY SPECIAL,
 * DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER
 * RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR
 * IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */


#include <err.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>
#include <trousers/tss.h>
#include <trousers/trousers.h>


#define RETURN_FAILURE		EXIT_FAILURE
#define RETURN_SUCCESS		EXIT_SUCCESS
#define ERROR_OUT(msg)		if (daemonised) {\
					syslog(LOG_ERR, msg); \
				} else { \
					fprintf(stderr, msg); \
					fprintf(stderr, "\n"); \
				}
#define ERROR_OUT1(msg, arg)	if (daemonised) { \
					syslog(LOG_ERR, msg, arg); \
				} else { \
					fprintf(stderr, msg, arg); \
					fprintf(stderr, "\n"); \
				}
#define PRINT_MSG(msg)		if (daemonised) { \
					syslog(LOG_INFO, msg); \
				} else { \
					printf(msg); \
					printf("\n"); \
				}
#define PRINT_MSG1(msg, arg)	if (daemonised) { \
					syslog(LOG_INFO, msg, arg); \
				} else { \
					printf(msg, arg); \
					printf("\n"); \
				}


const size_t	RAND_CHUNK = 32;
static time_t	update_delay = 86400; /* one day */
static int	daemonised = 0;


/*
 * tpm_rand reads a number of random bytes from the TPM's RNG.
 */
static uint8_t *
tpm_rand(size_t n)
{
	TSS_HCONTEXT         ctx = 0;
	TSS_HTPM             tpm = 0;
	TSS_RESULT           res = 0;
	BYTE		*blob = NULL;
	uint8_t		*data = NULL;

	res = Tspi_Context_Create(&ctx);
	if (TSS_SUCCESS != res) {
		goto exit;
	}

	res = Tspi_Context_Connect(ctx, NULL);
	if (TSS_SUCCESS != res) {
		goto exit;
	}

	res = Tspi_Context_GetTpmObject(ctx, &tpm);
	if (TSS_SUCCESS != res) {
		goto exit;
	}

	res = Tspi_TPM_GetRandom(tpm, (UINT32)n, &blob);
	if (TSS_SUCCESS != res) {
		ERROR_OUT1("failed to read random data from TPM: %s", Trspi_Error_String(res))
		goto exit;
	}

	data = calloc(n, sizeof(*data)*n);
	if (NULL == data) {
		res = TSS_E_OUTOFMEMORY;
		ERROR_OUT("failed to allocate memory for random data buffer")
		goto exit;
	}

	memcpy(data, blob, n);
	res = TSS_SUCCESS;

exit:
	Tspi_Context_FreeMemory(ctx, NULL);
	Tspi_Context_Close(ctx);
	if (TSS_SUCCESS == res) {
		return data;
	}
	return NULL;
}


/*
 * write_rand retrieves a chunk of random data from the TPM using
 * tpm_rand, and writes it to /dev/random.
 */
static int
write_rand(void)
{
	int	 res = RETURN_FAILURE;
	uint8_t	*data = NULL;
	FILE	*devrand = NULL;

	data = tpm_rand(RAND_CHUNK);
	if (NULL == data) {
		goto exit;
	}

	devrand = fopen("/dev/random", "w");
	if (NULL == devrand) {
		ERROR_OUT("failed to open /dev/random for writing")
		ERROR_OUT1("%s", strerror(errno))
		goto exit;
	}

	if (RAND_CHUNK != fwrite(data, sizeof(data[0]), RAND_CHUNK, devrand)) {
		ERROR_OUT("failed to write full random chunk to /dev/random")
		goto exit;
	}

	res = RETURN_SUCCESS;
	PRINT_MSG1("wrote %lu byte random chunk to /dev/random",
	    (long unsigned)RAND_CHUNK)

exit:
	free(data);
	fclose(devrand);
	return res;
}


/*
 * run sets up a constantly running loop, waking up every minute to
 * determine if it should write a new chunk of random data to /dev/random.
 */
static void
run(void)
{
	time_t	current = 0;
	time_t	next = 0;

	next = time(NULL);

	while (1) {
		current = time(NULL);
		if (current >= next) {
			write_rand();
			PRINT_MSG1("updated at %lu", (long unsigned)current)
			next = current + update_delay;
		}
		sleep(60);
	}
}


/*
 * shutdown is the signal handler that is activated when tpmrnd runs as
 * a daemon and the SIGUSR1 signal is received.
 */
static void
shutdown(int flags)
{
	if (SIGUSR1 != flags) {
		ERROR_OUT1("invalid signal received on handler: %d", flags)
		return;
	}
	PRINT_MSG("shutdown signal received");
	exit(EXIT_SUCCESS);
}


/*
 * tpmrnd periodically writes randomness from the TPM to /dev/random.
 */
int
main(int argc, char *argv[])
{

	int	opt;
	int	should_daemonise = 1;

	while ((opt = getopt(argc, argv, "fs:")) != -1) {
		switch (opt) {
		case 'f':
			should_daemonise = 0;
			break;
		case 's':
			update_delay = (time_t)strtol(optarg, NULL, 0);
			break;
		default:
			errx(EXIT_FAILURE, "invalid commandline option");
		}
	}

	if (should_daemonise) {
		daemonised = 1;
		openlog("tpmrnd", LOG_CONS, LOG_AUTHPRIV);
		if (daemon(0, 0)) {
			ERROR_OUT("failed to daemonise");
			ERROR_OUT("shutting down");
			exit(EXIT_FAILURE);
		}

		signal(SIGTTOU, SIG_IGN);
		signal(SIGTTIN, SIG_IGN);
		signal(SIGTSTP, SIG_IGN);
		signal(SIGUSR1, shutdown);
	}
	PRINT_MSG("starting up");
	run();
}
