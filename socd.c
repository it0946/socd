#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <linux/uinput.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <signal.h>

#ifndef release

// if debugging this will print the keystates on a seperate thread
#include <pthread.h>

void *print_keystates(void *pi);

#endif

// for indexing the keystates
#define W 0
#define A 1
#define S 2
#define D 3

// Convenience macro for error handling. arguments should be self explanatory
#define result_msg(call, fmt, args...) \
    do { if ((call) < 0) { fprintf(stderr, fmt, args); exit(1); } } while (0)

// convenience wrapper for result_msg on generic calls
#define result(call) \
    result_msg(call, "call failed at line %d: %s", __LINE__, strerror(errno))

// for storing virtual keystates
// - pressed is a bool, 1: pressed 0: released
// - which is what the corresponding key for this is in `linux/ 
struct keystate { char pressed, which; };

// The global state of the program
static struct {
    char *wr_target, *rd_target;
    int running, write_fd, read_fd, rl_keystates[4];
    struct keystate vr_keystates[4];
} context = {
    .running = 1,
    // write target. this is where the program will be sending input events
    .wr_target = "/dev/uinput",
    // read target. this is what the program will read inputs from
    .rd_target = "/dev/input/by-id/usb-ASUSTeK_Computer_Inc._N-KEY_Device-event-kbd",
    // real keystates
    .rl_keystates = { 0 },
    // virtual keystates
    .vr_keystates = {
        { 0, KEY_W },
        { 0, KEY_A },
        { 0, KEY_S },
        { 0, KEY_D },
    }
};

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
        fprintf(stderr, "failed to set sigint_handler: %s\n", strerror(errno));
        exit(1);
    }

    if (geteuid() != 0) {
        fprintf(stderr, "error: This program requires sudo to access keyboard inputs\n");
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
    
    printf("Reading inputs from: %s\n", kbd_name);

    #ifndef release

    // debug printing on another thread if release isn't defined
    pthread_t tid;
    int print_interval = 1;
    
    pthread_create(&tid, NULL, print_keystates, &print_interval);
    
    #endif

    const int size = sizeof(struct input_event);

    struct input_event ev[64];
    memset(ev, 0, sizeof(ev));

    while (context.running) { 
        // if reading from keyboard returns less than the size of one input_event it failed
        // this is also blocking to avoid freezing the computer
        if (read(context.read_fd, ev, size * 64) < size) {
            fprintf(stderr, "failed to read input: %s\n", strerror(errno));
            break;
        }

        // we only want wasd, abd this makes sure no other key makes it to `emit_all()`
        if (ev[1].code != KEY_W && ev[1].code != KEY_A && ev[1].code != KEY_S && ev[1].code != KEY_D)
            continue;

        // 1: press, 0: release
        if (ev[1].value == 1)
            handle_key_down(&ev[1]);
        else
            handle_key_up(&ev[1]);

        // Any required input event will be processed by this point
        // so this will register all inputs from `vr_keystates`
        emit_all();
    }

    puts("stopping.");

    #ifndef release
    pthread_join(tid, NULL);
    #endif

    result(ioctl(context.write_fd, UI_DEV_DESTROY));
    close(context.write_fd);
    close(context.read_fd);
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

    result(ioctl(context.write_fd, UI_SET_KEYBIT, KEY_W));
    result(ioctl(context.write_fd, UI_SET_KEYBIT, KEY_A));
    result(ioctl(context.write_fd, UI_SET_KEYBIT, KEY_S));
    result(ioctl(context.write_fd, UI_SET_KEYBIT, KEY_D));

    struct uinput_setup setup;
    memset(&setup, 0, sizeof(setup));

    strncpy(setup.name, "socd_cleaner", 13);
    
    setup.id.bustype = BUS_USB;
    
    // random values 
    setup.id.vendor = 0x1234;
    setup.id.product = 0x5678;
    //

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
    // more convenience
    #define release_ok(key) \
        if (context.rl_keystates[key]) context.vr_keystates[key].pressed = 0

    // If the key is pressed it takes priority over the virtual keys,
    // so if a any virtual key will be set to 0

    switch (ev->code) {
    case KEY_W:
        release_ok(S);
        context.rl_keystates[W] = 1;
        break;
    case KEY_A:
        release_ok(D);
        context.rl_keystates[A] = 1;
        break;
    case KEY_S:
        release_ok(W);
        context.rl_keystates[S] = 1;
        break;
    case KEY_D:
        release_ok(A);
        context.rl_keystates[D] = 1;
        break;
    }

    #undef release_ok
}

void handle_key_up(const struct input_event *ev) {
    // even more convenience
    #define press_ok(key) \
        if (context.rl_keystates[key]) context.vr_keystates[key].pressed = 1

    // If a key is released make sure that if the opposite is held
    // set the equivalent virtual key to true to re-register the input.

    switch (ev->code) {
    case KEY_W:
        press_ok(S);
        context.rl_keystates[W] = 0;
        context.vr_keystates[W].pressed = 0;
        break;
    case KEY_A:
        press_ok(D);
        context.rl_keystates[A] = 0;
        context.vr_keystates[A].pressed = 0;
        break;
    case KEY_S:
        press_ok(W);
        context.rl_keystates[S] = 0;
        context.vr_keystates[S].pressed = 0;
        break;
    case KEY_D:
        press_ok(A);
        context.rl_keystates[D] = 0;
        context.vr_keystates[D].pressed = 0;
        break;
    }

    #undef press_ok
}

// Emit key events to the system
void emit(int type, int code, int value) {
    struct input_event event;

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
    for (int i = 0; i <= 3; ++i) {
        emit(EV_KEY, context.vr_keystates[i].which, 0);
        emit(EV_SYN, SYN_REPORT, 0);
        emit(EV_KEY, context.vr_keystates[i].which, context.vr_keystates[i].pressed);
        emit(EV_SYN, SYN_REPORT, 0);
    }
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
