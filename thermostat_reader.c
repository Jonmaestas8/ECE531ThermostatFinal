#include <stdio.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <syslog.h>
#include <curl/curl.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

/*
    Daemon application for connection to AWS EC2 instance.
    Reports an update to the EC2 instance which handles the 
    logic of the code and reports the state.

    Reads the state from AWS instance and writes out to file.
*/

static const char* STATE_URL = "http://13.57.204.147:8080/state";
static const char* TEMP_URL = "http://13.57.204.147:8080/temp";
static const char* UPDATE = "http://13.57.204.147:8080/update";
static const char* TEMP_FILENAME = "/tmp/temp";
static const char* STATE_FILENAME = "/tmp/status";
static const char* CURL_OUTPUT = "out.txt";
static const char* GET = "GET";
static const int DELAY = 3;

#define OK              0
#define ERR_SETSID      1
#define SIGTERM         2
#define SIGHUP          3
#define ERR_FORK        4
#define ERR_CHDIR       5
#define WRONG_EXIT      6
#define INIT_ERR        7
#define REQ_ERR         8
#define NO_FILE         19
#define DAEMON_NAME     "Thermostat Reader Daemon"

 struct curl_string {
   char *response;
   size_t size;
 };
 
 struct curl_string chunk = {0};

static void _signal_handler(const int signal) {
    switch (signal) {
        case SIGHUP:
            break;
        case SIGTERM:
            syslog(LOG_INFO, "SIGTERM, program shutting down");
            closelog();
            exit(OK);
            break;
        default:
            syslog(LOG_INFO, "unspecified signal handler type");
    }
}

static size_t _write_fn(void *data, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct curl_string *mem = (struct curl_string *)userp;

    char *ptr = realloc(mem->response, mem->size + realsize + 1);
    if(ptr == NULL) {
        return 0;
    }

    mem->response = ptr;
    memcpy(&(mem->response[mem->size]), data, realsize);
    mem->size += realsize;
    mem->response[mem->size] = 0;

    return realsize;
}

static char* _send_request(char *url, char *message, char *type, bool verb) {
    CURL *curl = curl_easy_init();
    if (curl) {
        CURLcode res;
        FILE* outputFile = fopen(CURL_OUTPUT, "wb");
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, type);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, outputFile);

        if (strcmp(type, GET) == 0) {
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, _write_fn);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
        }

        if (verb) {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, message);
        } else {
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        }
        res = curl_easy_perform(curl);

        if (res != CURLE_OK) {
            return REQ_ERR;
        }

        curl_easy_cleanup(curl);
    } else {
        return NULL;
    }
    return chunk.response;
}

static int _daemonize(void) {
    openlog(DAEMON_NAME, LOG_PID | LOG_NDELAY | LOG_NOWAIT, LOG_DAEMON);

    syslog(LOG_INFO, "Staring IoT client final daemon");

    pid_t pid = fork();
    // check to see if fork was successful
    if (pid < 0) {
        syslog(LOG_ERR, strerror(pid));
        return ERR_FORK;
    }

    if (pid > 0) {
        exit(OK);
    }

    if (setsid() < -1) {
        syslog(LOG_ERR, strerror(pid));
        return ERR_SETSID;
    }

    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    umask(S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);

    if (chdir("/") < 0) {
        syslog(LOG_ERR, strerror(pid));
        return ERR_CHDIR;
    }

    signal(SIGTERM, _signal_handler);
    signal(SIGHUP, _signal_handler);

    return OK;
}

static bool _file_exists(const char* filename) {
    struct stat buffer;
    return (stat(filename, &buffer) == 0) ? true : false;
}

static void _read_temp(void) {
    char *buffer = NULL;
    size_t size = 0;

    /* Open your_file in read-only mode */
    FILE *fp = fopen(TEMP_FILENAME, "r");
    fseek(fp, 0, SEEK_END);
    size = ftell(fp);
    rewind(fp);
    buffer = malloc((size + 1) * sizeof(*buffer)); 
    fread(buffer, size, 1, fp);
    buffer[size] = '\0';
    _send_request(UPDATE, buffer, "POST", true);
}

static int _write_state(char *state) {
    FILE *fp = fopen(STATE_FILENAME, "w");
    if (fp == NULL) {
        printf("unable to open file for writing\n");
        return WRONG_EXIT;
    }
    fputs(state, fp);
    fclose(fp);
    return OK;
}

static void _handle_state_get(void) {
    char *state = _send_request(STATE_URL, NULL, GET, false);
    if (strcmp(state, "true") == 0) {
        syslog(LOG_INFO, "turning on heater");
        _write_state("ON");
    } else if (strcmp(state, "false") == 0) {
        syslog(LOG_INFO, "turning off heater");
        _write_state("OFF");
    }

    chunk.response = NULL;
    chunk.size = NULL;
}

static int _handle_work(void) {

    if (!_file_exists(TEMP_FILENAME) || !_file_exists(STATE_FILENAME) ) {
        syslog(LOG_ERR, "No temp or state files to read/write.");
        return NO_FILE;
    }

    while (true) {
        _read_temp();
        _handle_state_get();        
        sleep(DELAY);
    }
    
    return WRONG_EXIT;
}

void _help() {
	printf("Help\nUsage:\n\t./iot_projd -h or iot_projd --help Displays help,\n");
	printf("./iot_projd Runs daemon program that comunicates with a cloud database (still having trouble parsing JSON files) to turn heater on/off\n");
}

int main(int argc, char **argv) {    
	if(argc == 1) {
		syslog(LOG_INFO, "Running the daemon\n");
		_daemonize();
        _handle_work();
	}
	else if (argc > 1) {
		syslog(LOG_INFO, "Checking arguments\n");
		int i;
		for(i = 1; i < argc; i++) {
			if((strcmp(argv[i], "-h") == 0) || (strcmp(argv[i], "--help") == 0)) {
				_help();
			}
		}
	}

    return WRONG_EXIT;
}