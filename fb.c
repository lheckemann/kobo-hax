#include <stdint.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/mxcfb.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>


struct fb {
    int fd;
    struct fb_fix_screeninfo fix_screeninfo;
    struct fb_var_screeninfo var_screeninfo;
    uint16_t *mem;
} framebuffer;

int ioctl_p(int fd, int call, void* ptr) {
    int result;
    asm volatile (
            "mov a1, %[fd]\n"
            "mov a2, %[call]\n"
            "mov a3, %[ptr]\n"
            "mov r7, #54\n"
            "svc #0\n"
            "mov v1, %[result]\n"
            : [result]"=r"(result)
            : [fd]"r"(fd), [ptr]"r"(ptr), [call]"r"(call)
            : "%a1", "%a2", "%a3", "%r7", "%v1"
    );
    return result;
}

void print_field(const char const * name, const unsigned int value) {
    printf("%20s: %10u\n", name, value);
}

void print_bitfield(const struct fb_bitfield const * field) {
    printf("\toffset=%05u, length=%05u, msb_right=%05u\n", field->offset, field->length, field->msb_right);
}

void print_var_screeninfo(const struct fb_var_screeninfo const * info) {
    print_field("xres", info->xres);
    print_field("yres", info->yres);
    print_field("xres_virtual", info->xres_virtual);
    print_field("yres_virtual", info->yres_virtual);
    print_field("xoffset", info->xoffset);
    print_field("yoffset", info->yoffset);
    print_field("bits_per_pixel", info->bits_per_pixel);
    print_field("grayscale", info->grayscale);
    print_field("nonstd", info->nonstd);
    print_field("activate", info->activate);

    puts("\n");
    printf("Bitfield red\n");
    print_bitfield(&info->red);
    printf("Bitfield green\n");
    print_bitfield(&info->green);
    printf("Bitfield blue\n");
    print_bitfield(&info->blue);
    printf("Bitfield transp\n");
    print_bitfield(&info->transp);
}

void draw_rect(struct fb * fb, uint16_t colour, uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2) {
    for (uint32_t x = x1; x < x2; x++) {
        for (uint32_t y = y1; y < y2; y++) {
            fb->mem[y * fb->fix_screeninfo.line_length / 2 + x] = colour;
        }
    }
}


void update_rect(struct fb * fb, const struct mxcfb_rect * const region) {
    struct mxcfb_update_data update = {
        .update_region = *region,
        .waveform_mode = 2,
        .update_mode = UPDATE_MODE_FULL,
        .update_marker = 1,
        .temp = TEMP_USE_AMBIENT,
        .flags = 0,
    };
    ioctl(fb->fd, MXCFB_SEND_UPDATE, &update);
}

#define BATTERY_WIDTH (100)
#define BATTERY_HEIGHT (300)
#define BATTERY_BUTTON_HEIGHT (20)
#define BUTTON_WIDTH (40)
#define BATTERY_BORDER_WIDTH (5)
#define BATTERY_OFFSET_X (300)
#define BATTERY_OFFSET_Y (300)
uint32_t battery_level;
void draw_battery(struct fb * fb) {
    static const struct mxcfb_rect update = {
        .top = BATTERY_OFFSET_Y,
        .left = BATTERY_OFFSET_X,
        .width = BATTERY_WIDTH,
        .height = BATTERY_HEIGHT,
    };

    battery_level = get_battery_level();
    for (uint32_t x = 0; x < BATTERY_WIDTH; x++) {
        for (uint32_t y = 0; y < BATTERY_HEIGHT; y++) {
            fb->mem[(y + BATTERY_OFFSET_Y) * fb->fix_screeninfo.line_length / 2 + (BATTERY_OFFSET_X + x)] =
                // Battery "button" (positive pole)
                (y <= BATTERY_BUTTON_HEIGHT && x > (BATTERY_WIDTH - BUTTON_WIDTH)/2 && x < (BATTERY_WIDTH + BUTTON_WIDTH)/2) ? 0 :
                // Battery top border
                (y >= BATTERY_BUTTON_HEIGHT && y < BATTERY_BUTTON_HEIGHT + BATTERY_BORDER_WIDTH) ? 0 :
                // Battery left and right borders
                (y >= BATTERY_BUTTON_HEIGHT && (x < BATTERY_BORDER_WIDTH || x > BATTERY_WIDTH - BATTERY_BORDER_WIDTH))  ? 0 :
                // Battery bottom border
                (y >= BATTERY_HEIGHT - BATTERY_BORDER_WIDTH) ? 0 :
                // Battery fill status
                (y > (100-battery_level) * (BATTERY_HEIGHT - BATTERY_BUTTON_HEIGHT - 2*BATTERY_BORDER_WIDTH) / 100 + BATTERY_BUTTON_HEIGHT + BATTERY_BORDER_WIDTH ) ? 0x8410 :
                0xffff;
        }
    }
    update_rect(fb, &update);
}

int get_battery_level() {
    char result[4];
    int fd = open("/sys/class/power_supply/mc13892_bat/capacity", O_RDONLY);
    read(fd, result, 4);
    close(fd);
    return atoi(result);
}

#define MIN(x, y) (x < y ? x : y)
#define MAX(x, y) (x < y ? y : x)

struct mxcfb_rect rect_union(const struct mxcfb_rect const * a, const struct mxcfb_rect const * b) {
    uint32_t top = MIN(a->top, b->top);
    uint32_t left = MIN(a->left, b->left);
    uint32_t right = MAX(a->left + a->width, b->left + b->width);
    uint32_t bottom = MAX(a->top + a->height, b->top + b->height);
    struct mxcfb_rect result = {
        .top = top,
        .left = left,
        .width = right - left,
        .height = bottom - top
    };
    return result;
}

int init_fb(struct fb * fb) {
    int errno_save;
    fb->fd = open("/dev/fb0", O_RDWR);
    if (errno_save = errno) {
        perror("open /dev/fb0");
        return errno_save;
    }
    ioctl(fb->fd, FBIOGET_FSCREENINFO, &fb->fix_screeninfo);
    if (errno_save = errno) {
        perror("FBIOGET_FSCREENINFO");
        return errno_save;
    }
    ioctl(fb->fd, FBIOGET_VSCREENINFO, &fb->var_screeninfo);
    if (errno_save = errno) {
        perror("FBIOGET_VSCREENINFO");
        return errno_save;
    }
    fb->mem = mmap(NULL,
        fb->var_screeninfo.xres_virtual * fb->var_screeninfo.yres_virtual * fb->var_screeninfo.bits_per_pixel / 8,
        PROT_READ | PROT_WRITE,
        MAP_SHARED,
        fb->fd,
        0);
    if (errno_save = errno) {
        perror("mapping fb");
    }
}

int main() {
    init_fb(&framebuffer);
    while (1) {
        draw_battery(&framebuffer);
        printf("Battery %u%%\n", battery_level);
        sleep(10);
    }
}
