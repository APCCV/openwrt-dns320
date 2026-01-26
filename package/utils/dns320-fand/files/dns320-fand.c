// C library headers
#define _XOPEN_SOURCE 500
#include <ftw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <getopt.h>

// Linux headers
#include <fcntl.h> // Contains file controls like O_RDWR
#include <errno.h> // Error integer and strerror() function
#include <termios.h> // Contains POSIX terminal control definitions
#include <unistd.h> // write(), read(), close()

// Parameters
int hysteresis = 1;
int frequency = 60;
int verbose = 0;
int test = 0;
int ignorec = 0;
int ignorel = 0;
int ignorer = 0;
int one_shot = 0;
char* sys_path = "/sys/devices/platform";
char* temp_port = "/dev/ttyS1";

// Thresholds
int HDD_LOW=40;
int HDD_HIGH=45;
int CPU_LOW=45;
int CPU_HIGH=50;


// File paths
char fan_path[128];
char disk1_temp_path[128];
char disk2_temp_path[128];

int get_cpu_temp() {
    // Allocate memory for read buffer, set size according to your needs
    char read_buf [64];

    // Open the serial port. Change device path as needed (currently set to an standard FTDI USB-UART cable type device)
    int serial_port = open(temp_port, O_RDWR);

    // Create new termios struct, we call it 'tty' for convention
    struct termios tty;

    // Read in existing settings, and handle any error
    if (tcgetattr(serial_port, &tty) != 0) {
        // printf("Error %i from tcgetattr: %s\n", errno, strerror(errno));
        return -1;
    }

    // Serial port config
    tty.c_cflag &= ~PARENB; // Clear parity bit, disabling parity (most common)
    tty.c_cflag &= ~CSTOPB; // Clear stop field, only one stop bit used in communication (most common)
    tty.c_cflag &= ~CSIZE; // Clear all bits that set the data size
    tty.c_cflag |= CS8; // 8 bits per byte (most common)
    // tty.c_cflag &= ~CRTSCTS; // Disable RTS/CTS hardware flow control (most common)
    tty.c_cflag |= CREAD | CLOCAL; // Turn on READ & ignore ctrl lines (CLOCAL = 1)

    tty.c_lflag &= ~ICANON;
    tty.c_lflag &= ~ECHO; // Disable echo
    tty.c_lflag &= ~ECHOE; // Disable erasure
    tty.c_lflag &= ~ECHONL; // Disable new-line echo
    tty.c_lflag &= ~ISIG; // Disable interpretation of INTR, QUIT and SUSP
    tty.c_iflag &= ~(IXON | IXOFF | IXANY); // Turn off s/w flow ctrl
    tty.c_iflag &= ~(IGNBRK|BRKINT|PARMRK|ISTRIP|INLCR|IGNCR|ICRNL); // Disable any special handling of received bytes

    tty.c_oflag &= ~OPOST; // Prevent special interpretation of output bytes (e.g. newline chars)
    tty.c_oflag &= ~ONLCR; // Prevent conversion of newline to carriage return/line feed
    // tty.c_oflag &= ~OXTABS; // Prevent conversion of tabs to spaces (NOT PRESENT ON LINUX)
    // tty.c_oflag &= ~ONOEOT; // Prevent removal of C-d chars (0x004) in output (NOT PRESENT ON LINUX)

    tty.c_cc[VTIME] = 10;    // Wait for up to 1s (10 deciseconds), returning as soon as any data is received.
    tty.c_cc[VMIN] = 0;

    // Set in/out baud rate to be 19200
    cfsetispeed(&tty, B19200);
    cfsetospeed(&tty, B19200);

    // Save tty settings, also checking for error
    if (tcsetattr(serial_port, TCSANOW, &tty) != 0)
        return -2;

    // Flush IO
    if (tcflush(serial_port, TCIOFLUSH) != 0)
        return -3;

    // Write to serial port
    unsigned char msg[] = { '\xf0', '\xf0', '\x10', '\x04', '\x02', '\x00', '\x00', '\x00', '\x00', '\x00', '\x00', '\x00', '\x00', '\xf6', '\xf6', '\xf6' };
    write(serial_port, msg, sizeof(msg));

    // Give it 25ms to think
    usleep(25*1000);

    // Read bytes. The behaviour of read() (e.g. does it block?,
    // how long does it block for?) depends on the configuration
    // settings above, specifically VMIN and VTIME
    int num_bytes = read(serial_port, &read_buf, sizeof(read_buf));

    // n is the number of bytes read. n may be 0 if no bytes were received, and can also be -1 to signal an error.
    if (num_bytes != 16)
        return -4;

    close(serial_port);
    return read_buf[15] - 0xb6; // success
};

int read_from_file(char* file_path, char* contents) {
    *contents = 0;
    FILE *file = fopen(file_path, "r");
    if (file != NULL) {
        fgets(contents, 256, file);
        fclose(file);
        return 0;
    }
    return -1;
}

int write_to_file(char* file_path, char* message) {
    FILE *file = fopen(file_path, "w");
    if (file != NULL) {
        fprintf(file, "%s", message);
        fclose(file);
        return 0;
    }
    return -1;
}

static int find_files(const char *fpath, const struct stat *sb, int tflag, struct FTW *ftwbuf)
{
    if (strstr(fpath, "sata")!=NULL && strstr(fpath, "temp1_input")!=NULL) {
        if (strlen(disk1_temp_path)==0)
            strcpy(disk1_temp_path, fpath);
        else
            strcpy(disk2_temp_path, fpath);
    }
    if (strstr(fpath, "gpio_fan")!=NULL && strstr(fpath, "fan1_target")!=NULL) {
        strcpy(fan_path, fpath);
    }
    return 0;           /* To tell nftw() to continue */
}

void print_usage() {
    printf("DNS-320 A1 Fan Control Daemon.\n");
    printf("Usage:\n");
    printf("  -v, --verbose                   print behaviour to stdout\n");
    printf("  -t, --test                      don't update fan speed\n");
    printf("  -f, --ffrequency <seconds>      sleep time betweeen polls\n");
    printf("  -h, --cpu-high <temperature>    high CPU temp threshold\n");
    printf("  -l, --cpu-low <temperature>     low CPU temp threshold\n");
    printf("  -d, --disk-high <temperature>   high HDD temp threshold\n");
    printf("  -e, --disk-low <temperature>    low HDD temp threshold\n");
    printf("  -i, --ignore [cpu,left,right]   ignore CPU, left and/or right temp\n");
    printf("  -h, --hysteresis <degrees>      lag applied to thresholds\n");
    printf("  -1, --one-shot                  run once and exit\n");
    printf("  -?, --help                      display help\n");
}

int main(int argc, char *argv[]) {
    *fan_path = 0;
    *disk1_temp_path = 0;
    *disk2_temp_path = 0;
    int current_fan = 0;
    int cpu_temp = 0;
    int disk1_temp = 0;
    int disk2_temp = 0;
    char buffer[256];
    int new_fan = 0;
    char target[8];

    //Specifying the expected options
    //The two options l and b expect numbers as argument
    static struct option long_options[] = {
        {"verbose",    no_argument,       0,  'v' },
        {"test",       no_argument,       0,  't' },
        {"frequency",  required_argument, 0,  'f' },
        {"cpu-high",   required_argument, 0,  'h' },
        {"cpu-low",    required_argument, 0,  'l' },
        {"disk-high",  required_argument, 0,  'd' },
        {"disk-low",   required_argument, 0,  'e' },
        {"ignore",     required_argument, 0,  'i' },
        {"hysteresis", required_argument, 0,  's' },
        {"one-shot",   no_argument,       0,  '1' },
        {"help",       no_argument,       0,  '?' },
        {0,            0,                 0,  0   }
    };

    int long_index = 0;
    int opt = 0;
    while ((opt = getopt_long(argc, argv,"vtf:h:l:d:e:i:s:1?",
                   long_options, &long_index )) != -1) {
        switch (opt) {
             case 'v' : verbose = 1;
                 break;
             case 't' : test = 1;
                 break;
             case 'f' : frequency = atoi(optarg);
                 break;
             case 'h' : CPU_HIGH = atoi(optarg);
                 break;
             case 'l' : CPU_LOW = atoi(optarg);
                 break;
             case 'd' : HDD_HIGH = atoi(optarg);
                 break;
             case 'e' : HDD_LOW = atoi(optarg);
                 break;
             case 's' : hysteresis = atoi(optarg);
                 break;
             case 'i' :
                 ignorec = (strstr(optarg, "c")!=NULL);
                 ignorel = (strstr(optarg, "l")!=NULL);
                 ignorer = (strstr(optarg, "r")!=NULL);
                 break;
             case '1' : one_shot = 1;
                 break;
             case '?' :
             default: print_usage();
                 exit(EXIT_FAILURE);
        }
    }

    if (verbose) {
        printf("Testing: %d, Frequency: %d, Ignore c/l/r: %d/%d/%d, CPU High/Low: %d/%d, HDD High/Low: %d/%d, Hyst: %d.\n",
            test, frequency, ignorec, ignorel, ignorer, CPU_HIGH, CPU_LOW, HDD_HIGH, HDD_LOW, hysteresis);
    }

    if (CPU_LOW+hysteresis>=CPU_HIGH-hysteresis || HDD_LOW+hysteresis>=HDD_HIGH-hysteresis || hysteresis<0 || CPU_LOW<=0 || HDD_LOW<=0 || frequency<5 || (ignorec && ignorel && ignorer)) {
        printf("Invalid configuration.\n");
        exit(EXIT_FAILURE);
    }

    // Find sysfs paths
    int flags = 0;
    flags |= FTW_PHYS;
    if (nftw(sys_path, find_files, 16, flags) == -1) {
        perror("nftw");
        exit(EXIT_FAILURE);
    }

    do {
        // get CPU temp
        if (!ignorec) cpu_temp = get_cpu_temp();
        if (verbose) printf("CPU temp: %d (ignored: %d)\n", cpu_temp, ignorec);

        // get fan status
        read_from_file(fan_path, buffer);
        sscanf(buffer, "%d", &current_fan);
        if (verbose) printf("Fan: %d\n", current_fan);

        // get HDDs temp
        if (!ignorel ) {
            read_from_file(disk1_temp_path, buffer);
            sscanf(buffer, "%d", &disk1_temp);
            disk1_temp /= 1000;
        }
        if (verbose) printf("Disk1: %d (ignored: %d)\n", disk1_temp, ignorel);
        if (!ignorer ) {
            read_from_file(disk2_temp_path, buffer);
            sscanf(buffer, "%d", &disk2_temp);
            disk2_temp /= 1000;
        }
        if (verbose) printf("Disk2: %d (ignored: %d)\n", disk2_temp, ignorer);

        // Logic to set fan speed
        new_fan = current_fan;
        if (current_fan == 0) {
            if ((!ignorec && cpu_temp>CPU_LOW+hysteresis) || (!ignorel && disk1_temp>HDD_LOW+hysteresis) || (!ignorer && disk2_temp>HDD_LOW+hysteresis))
                new_fan = 3000;
            if ((!ignorec && cpu_temp>CPU_HIGH+hysteresis) || (!ignorel && disk1_temp>HDD_HIGH+hysteresis) || (!ignorer && disk2_temp>HDD_HIGH+hysteresis))
                new_fan = 6000;
        }
        else if (current_fan == 3000) {
            if ((!ignorec && cpu_temp<CPU_LOW-hysteresis) && (!ignorel && disk1_temp<HDD_LOW-hysteresis) && (!ignorer && disk2_temp<HDD_LOW-hysteresis))
                new_fan = 0;
            if ((!ignorec && cpu_temp>CPU_HIGH+hysteresis) || (!ignorel && disk1_temp>HDD_HIGH+hysteresis) || (!ignorer && disk2_temp>HDD_HIGH+hysteresis))
                new_fan = 6000;
        }
        else if (current_fan == 6000) {
            if ((!ignorec && cpu_temp<CPU_HIGH-hysteresis) && (!ignorel && disk1_temp<HDD_HIGH-hysteresis) && (!ignorer && disk2_temp<HDD_HIGH-hysteresis))
                new_fan = 3000;
            if ((!ignorec && cpu_temp<CPU_LOW-hysteresis) && (!ignorel && disk1_temp<HDD_LOW-hysteresis) && (!ignorer && disk2_temp<HDD_LOW-hysteresis))
                new_fan = 0;
        }
        if (verbose) printf("New fan: %d\n", new_fan);
        if (current_fan != new_fan) {
            sprintf(target, "%d", new_fan);
            if (verbose) printf("Target: %s\n", target);
            if (!test)
                write_to_file(fan_path, target);
            else
                if (verbose) printf("Test - no target update\n");
        }
        if (!one_shot) sleep(frequency);
    } while(!one_shot);

    exit(EXIT_SUCCESS);
}
