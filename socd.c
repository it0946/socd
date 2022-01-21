#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <linux/uinput.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <dirent.h>
#include <termios.h>

#ifndef release

// if debugging this will print the keystates on a seperate thread
#include <pthread.h>

void *print_keystates(void *pi);

#endif

// for indexing the keystates
#define UP 0
#define LEFT 1
#define DOWN 2
#define RIGHT 3

// Convenience macro for error handling. arguments should be self explanatory
#define result_msg(call, fmt, args...) \
    do { if ((call) < 0) { fprintf(stderr, fmt, args); exit(1); } } while (0)

// convenience wrapper for result_msg on generic calls
#define result(call) \
    result_msg(call, "call failed at line %d: %s", __LINE__, strerror(errno))

// for storing virtual keystates
// - pressed is a bool, 1: pressed 0: released
// - which is what the corresponding key for this is in `linux/ 
struct keystate { char pressed; int which; };

// The global state of the program
// - wr_target: write target. this is where the program will be sending input events
// - write_fd: file descriptor for wr_target
// - rd_target: read target. this is what the program will read inputs from.
//  275 is from  the max unix filename plus lenght of BY_PATH which is the longer of the two
// - read_fd: file descriptor for rd_target
// - rl_keystates: real physical states of the keys on the keyboard
// - vr_keystates: the virtually emulated states of the keys
static struct {
    char *wr_target, rd_target[275], running;
    int write_fd, read_fd, rl_keystates[4];
    struct keystate vr_keystates[4];
} context = {
    .running = 1,
    .wr_target = "/dev/uinput",
    .rd_target = { 0 },
    .rl_keystates = { 0 },
    .vr_keystates = {
        { 0, KEY_W },
        { 0, KEY_A },
        { 0, KEY_S },
        { 0, KEY_D },
    }
};

// Most likely works
const char *BY_ID = "/dev/input/by-id/";
// Some devices may not have the above so this might work instead
const char *BY_PATH = "/dev/input/by-path/";

int get_keyboard(void);
int prompt_user(int max);

void sigint_handler(int sig);
void emit(int type, int code, int value);
// emit all key events with states read from vr_keystates
void emit_all(void);
void setup_write(void);

inline void handle_key_down(const struct input_event *ev);
inline void handle_key_up(const struct input_event *ev);

int main(int argc, char **argv) {
    // arguments are currently unused
    (void)argc;
    (void)argv;

    // For a graceful shutdown to make sure the device is destroyed
    if (signal(SIGINT, sigint_handler) == SIG_ERR) {
        fprintf(stderr, "error: Failed to set sigint_handler: %s\n", strerror(errno));
        exit(1);
    }

    if (geteuid() != 0) {
        fprintf(stderr, "error: This program requires sudo to access keyboard inputs\n");
        exit(1);
    }

    // todo: have to get the custom keys in this gap

    if (get_keyboard()) {
        fprintf(stderr, "error: Failed to get keyboards\n");
        exit(1);
    }

    setup_write();
    
    // setup read
    result_msg(
        context.read_fd = open(context.rd_target, O_RDONLY),
        "failed to open %s: %s\n", context.rd_target, strerror(errno)
    );

    char kbd_name[256];
    ioctl(context.read_fd, EVIOCGNAME(sizeof(kbd_name)), kbd_name);
    
    struct termios t_attrs;
    tcgetattr(STDIN_FILENO, &t_attrs);

    // Enable raw input mode
    t_attrs.c_lflag &= ~(ECHO | ICANON);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &t_attrs);

    printf("Reading inputs from: %s. Press ctrl + c to quit\n", kbd_name);

    #ifndef release

    // debug printing on another thread if release isn't defined
    pthread_t tid;
    int print_interval = 1;
    
    pthread_create(&tid, NULL, print_keystates, &print_interval);
    
    #endif

    const int SIZE = sizeof(struct input_event);

    struct input_event ev[64];
    memset(ev, 0, sizeof(ev));

    while (context.running) { 
        // if reading from keyboard returns less than the size of one input_event it failed
        // this is also blocking to avoid freezing the computer
        if (read(context.read_fd, ev, SIZE * 64) < SIZE) {
            fprintf(stderr, "failed to read input: %s\n", strerror(errno));
            break;
        }

        // we only want the registered movement keys, and this makes sure no other key makes it to `emit_all()`
        if (ev[1].code != context.vr_keystates[UP].which
            && ev[1].code != context.vr_keystates[LEFT].which
            && ev[1].code != context.vr_keystates[DOWN].which
            && ev[1].code != context.vr_keystates[RIGHT].which
        ) continue;

        // 1: press, 0: release
        if (ev[1].value == 1)
            handle_key_down(&ev[1]);
        else
            handle_key_up(&ev[1]);

        // Any required input event will be processed by this point
        // so this will register all inputs from `vr_keystates`
        emit_all();
    }

    #ifndef release
    pthread_join(tid, NULL);
    #endif

    puts("Stopping.");

    // cleanup
    result(ioctl(context.write_fd, UI_DEV_DESTROY));
    close(context.write_fd);
    close(context.read_fd);

    // Disable raw input mode
    t_attrs.c_lflag |= (ECHO | ICANON);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &t_attrs);
}

void sigint_handler(int sig) {
    // incase the signal is sent twice
    signal(sig, sigint_handler);
    context.running = 0;
}

void setup_write() {
    result_msg(
        context.write_fd = open(context.wr_target, O_WRONLY | O_NONBLOCK), 
        "failed to open %s: %s\n", context.wr_target, strerror(errno)
    );

    result(ioctl(context.write_fd, UI_SET_EVBIT, EV_KEY));

    // enable writing the desired keys
    result(ioctl(context.write_fd, UI_SET_KEYBIT, context.vr_keystates[UP].which));
    result(ioctl(context.write_fd, UI_SET_KEYBIT, context.vr_keystates[LEFT].which));
    result(ioctl(context.write_fd, UI_SET_KEYBIT, context.vr_keystates[DOWN].which));
    result(ioctl(context.write_fd, UI_SET_KEYBIT, context.vr_keystates[RIGHT].which));

    struct uinput_setup setup;
    memset(&setup, 0, sizeof(setup));

    strncpy(setup.name, "socd_cleaner", 13);
    
    setup.id.bustype = BUS_USB;
    
    // random values 
    setup.id.vendor = 0x1234;
    setup.id.product = 0x5678;

    result_msg(
        ioctl(context.write_fd, UI_DEV_SETUP, &setup),
        "failed to setup device: %s\n", strerror(errno)
    );

    result_msg(
        ioctl(context.write_fd, UI_DEV_CREATE),
        "failed to create device: %s\n", strerror(errno)
    );
}

void handle_key_down(const struct input_event *ev) {
    // release opposite virtual key
    #define release_ok(key) \
        if (context.rl_keystates[key]) context.vr_keystates[key].pressed = 0

    // If the key is pressed it takes priority over the virtual keys,
    // so if a any virtual key will be set to 0

    switch (ev->code) {
    case KEY_W:
        release_ok(DOWN);
        context.rl_keystates[UP] = 1;
        break;
    case KEY_A:
        release_ok(RIGHT);
        context.rl_keystates[LEFT] = 1;
        break;
    case KEY_S:
        release_ok(UP);
        context.rl_keystates[DOWN] = 1;
        break;
    case KEY_D:
        release_ok(LEFT);
        context.rl_keystates[RIGHT] = 1;
        break;
    }

    #undef release_ok
}

void handle_key_up(const struct input_event *ev) {
    // press opposite virtual key
    #define press_ok(key) \
        if (context.rl_keystates[key]) context.vr_keystates[key].pressed = 1

    // If a key is released make sure that if the opposite is held
    // set the equivalent virtual key to true to re-register the input.

    switch (ev->code) {
    case KEY_W:
        press_ok(DOWN);
        context.rl_keystates[UP] = 0;
        context.vr_keystates[UP].pressed = 0;
        break;
    case KEY_A:
        press_ok(RIGHT);
        context.rl_keystates[LEFT] = 0;
        context.vr_keystates[LEFT].pressed = 0;
        break;
    case KEY_S:
        press_ok(UP);
        context.rl_keystates[DOWN] = 0;
        context.vr_keystates[DOWN].pressed = 0;
        break;
    case KEY_D:
        press_ok(LEFT);
        context.rl_keystates[RIGHT] = 0;
        context.vr_keystates[RIGHT].pressed = 0;
        break;
    }

    #undef press_ok
}

// Emit key events to the system
void emit(int type, int code, int value) {
    struct input_event event;

    // keycode
    event.code = code;
    // what type of event eg. EV_KEY, EV_SYN
    event.type = type,
    // pressed down or not
    event.value = value;

    event.time.tv_sec = 0;
    event.time.tv_usec = 0;

    write(context.write_fd, &event, sizeof(event));
}

void emit_all() {
    // todo: this isn't the most ideal solution
    for (int i = 0; i <= 3; ++i) {
        emit(EV_KEY, context.vr_keystates[i].which, 0);
        emit(EV_SYN, SYN_REPORT, 0);
        emit(EV_KEY, context.vr_keystates[i].which, context.vr_keystates[i].pressed);
        emit(EV_SYN, SYN_REPORT, 0);
    }
}

int get_keyboard() {
    DIR *d;
    struct dirent *dir;

    if ((d = opendir(BY_ID)) != NULL)
        strcpy(context.rd_target, BY_ID);
    else if ((d = opendir(BY_PATH)) != NULL)
        strcpy(context.rd_target, BY_PATH);
    else 
        return 1;

    char *possible_devices[8];
    int j = -1, selected = 0;
    
    // Iterate throught entries of the chosen directory
    // and select all possible keyboards into `possible_devices`
    while ((dir = readdir(d)) != NULL && j < 8) {
        char *tmp = dir->d_name;
        int len = strlen(tmp);
        if (len < 10) continue;

        // keyboards end with "-event-kbd", but the
        // actual keyboard shouldn't have "-ifxx" before "-event-kbd"
        if (strncmp(tmp + len - 10, "-event-kbd", 10) != 0
            || strncmp(tmp + len - 15, "-if", 3) == 0) continue;

        j += 1;
        possible_devices[j] = tmp;
    }

    if (j == -1) return 1;

    // If more than one possible keyboard is found prompt the user for the one they prefer
    if (j > 0) {
        puts("More than one possible keyboard found");
        for (int i = 0; i <= j; ++i) {
            printf(" %d. %s\n", i + 1, possible_devices[i]);
        }

        printf("\nPick one by typing a number from 1-%d:\n", j + 1);

        selected = prompt_user(j);
    }

    // full path to the keyboard
    strcat(context.rd_target, possible_devices[selected]);

    closedir(d);

    return 0;
}

int prompt_user(int max) {
    char c;

    while (1) {
        read(STDIN_FILENO, &c, 1);
        if (c <= '0' || c >= '9' || c - '0' - 1 > max) continue;
        
        break;
    };

    // from ascii code to equivalent int
    return c - '0' - 1;
}

#ifndef release

void *print_keystates(void *ptr) {
    int interval = *(int*)ptr;

    while (context.running) {
        sleep(interval);
        printf("\nkeystates { w: %d, a: %d, s: %d, d: %d }\nvirtual keystates { w: %d, a: %d, s: %d, d: %d }\n",
            context.rl_keystates[0], context.rl_keystates[1],
            context.rl_keystates[2], context.rl_keystates[3],
            context.vr_keystates[0].pressed, context.vr_keystates[1].pressed,
            context.vr_keystates[2].pressed, context.vr_keystates[3].pressed
        );
    }

    return NULL;
}

#endif
