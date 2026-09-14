#include <ApplicationServices/ApplicationServices.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct {
    char display_uuid[64];
    int32_t x;
    int32_t y;
} Placement;

static int uuid_for_display(CGDirectDisplayID display, char *result, size_t result_size) {
    CFUUIDRef uuid = CGDisplayCreateUUIDFromDisplayID(display);
    if (uuid == NULL) return 0;
    CFStringRef text = CFUUIDCreateString(NULL, uuid);
    CFRelease(uuid);
    if (text == NULL) return 0;
    int success = CFStringGetCString(text, result, result_size, kCFStringEncodingUTF8);
    CFRelease(text);
    return success;
}

static CGDirectDisplayID display_for_uuid(const char *wanted, int *found) {
    CGDirectDisplayID displays[32];
    uint32_t count = 0;
    *found = 0;
    if (CGGetOnlineDisplayList(32, displays, &count) != kCGErrorSuccess) return 0;
    for (uint32_t i = 0; i < count; i++) {
        char uuid[64];
        if (uuid_for_display(displays[i], uuid, sizeof(uuid)) && strcasecmp(uuid, wanted) == 0) {
            *found = 1;
            return displays[i];
        }
    }
    return 0;
}

static int list_displays(void) {
    CGDirectDisplayID displays[32];
    uint32_t count = 0;
    if (CGGetOnlineDisplayList(32, displays, &count) != kCGErrorSuccess || count == 0) {
        fprintf(stderr, "No connected displays found.\n");
        return 1;
    }
    for (uint32_t i = 0; i < count; i++) {
        char uuid[64];
        if (!uuid_for_display(displays[i], uuid, sizeof(uuid))) continue;
        CGRect bounds = CGDisplayBounds(displays[i]);
        printf("UUID: %s  size: %.0fx%.0f  origin: (%.0f, %.0f)\n", uuid,
               bounds.size.width, bounds.size.height, bounds.origin.x, bounds.origin.y);
    }
    return 0;
}

static int config_path(char *result, size_t result_size, const char *config_dir, const char *layout_name) {
    int written = snprintf(result, result_size, "%s/layout-%c.conf", config_dir, layout_name[0] | 32);
    return written >= 0 && (size_t)written < result_size;
}

static int capture_layout(const char *config_dir, const char *layout_name, int dry_run) {
    CGDirectDisplayID displays[32];
    uint32_t count = 0;
    if (CGGetOnlineDisplayList(32, displays, &count) != kCGErrorSuccess || count == 0) {
        fprintf(stderr, "No connected displays found.\n");
        return 1;
    }

    char path[PATH_MAX], temporary_path[PATH_MAX];
    if (!config_path(path, sizeof(path), config_dir, layout_name) ||
        snprintf(temporary_path, sizeof(temporary_path), "%s.tmp", path) >= (int)sizeof(temporary_path)) {
        fprintf(stderr, "Config path is too long.\n");
        return 1;
    }
    FILE *file = dry_run ? stdout : fopen(temporary_path, "w");
    if (file == NULL) {
        perror("Could not write layout config");
        return 1;
    }
    fputs("# Display UUID, x origin, y origin\n", file);
    for (uint32_t i = 0; i < count; i++) {
        char uuid[64];
        if (!uuid_for_display(displays[i], uuid, sizeof(uuid))) {
            if (!dry_run) {
                fclose(file);
                unlink(temporary_path);
            }
            fprintf(stderr, "Could not get a persistent UUID for one display.\n");
            return 1;
        }
        CGRect bounds = CGDisplayBounds(displays[i]);
        fprintf(file, "%s %.0f %.0f\n", uuid, bounds.origin.x, bounds.origin.y);
    }
    if (dry_run) return 0;
    if (fclose(file) != 0 || rename(temporary_path, path) != 0) {
        perror("Could not save layout config");
        return 1;
    }
    printf("Captured layout %s in %s\n", layout_name, path);
    return 0;
}

static size_t load_layout(const char *path, Placement placements[], size_t capacity) {
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        fprintf(stderr, "Layout config is missing: %s. Run --capture first.\n", path);
        return 0;
    }
    char line[256];
    size_t count = 0;
    while (fgets(line, sizeof(line), file) != NULL) {
        if (line[0] == '#' || line[0] == '\n') continue;
        if (count == capacity || sscanf(line, "%63s %d %d", placements[count].display_uuid,
                                        &placements[count].x, &placements[count].y) != 3) {
            fclose(file);
            fprintf(stderr, "Invalid layout config: %s\n", path);
            return 0;
        }
        count++;
    }
    fclose(file);
    if (count == 0) fprintf(stderr, "Layout config is empty: %s\n", path);
    return count;
}

static int apply_layout(const char *path, const char *name) {
    Placement layout[32];
    size_t layout_count = load_layout(path, layout, 32);
    if (layout_count == 0) return 1;

    CGDisplayConfigRef configuration;
    if (CGBeginDisplayConfiguration(&configuration) != kCGErrorSuccess) {
        fprintf(stderr, "Could not begin the display configuration.\n");
        return 1;
    }
    for (size_t i = 0; i < layout_count; i++) {
        int found = 0;
        CGDirectDisplayID display = display_for_uuid(layout[i].display_uuid, &found);
        if (!found || CGConfigureDisplayOrigin(configuration, display, layout[i].x, layout[i].y) != kCGErrorSuccess) {
            CGCancelDisplayConfiguration(configuration);
            fprintf(stderr, "Could not position display %s.\n", layout[i].display_uuid);
            return 1;
        }
    }
    if (CGCompleteDisplayConfiguration(configuration, kCGConfigurePermanently) != kCGErrorSuccess) {
        fprintf(stderr, "macOS rejected display layout %s.\n", name);
        return 1;
    }
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 3 || strcmp(argv[1], "--config-dir") != 0) {
        fprintf(stderr, "Use the rearrange-displays.sh launcher.\n");
        return 1;
    }
    const char *config_dir = argv[2];
    const char *command = argc > 3 ? argv[3] : "";
    if (strcmp(command, "--list") == 0) return list_displays();
    if (strcmp(command, "--capture") == 0) {
        const char *layout_name = argc > 4 ? argv[4] : "A";
        int dry_run = argc > 5 && strcmp(argv[5], "--dry-run") == 0;
        if (strcmp(layout_name, "A") != 0 && strcmp(layout_name, "B") != 0) {
            fprintf(stderr, "Use --capture A or --capture B.\n");
            return 1;
        }
        if (argc > 5 && !dry_run) {
            fprintf(stderr, "Use --capture A [--dry-run] or --capture B [--dry-run].\n");
            return 1;
        }
        return capture_layout(config_dir, layout_name, dry_run);
    }

    const char *home = getenv("HOME");
    if (home == NULL) return 1;
    char state_dir[PATH_MAX], state_file[PATH_MAX], temporary_file[PATH_MAX], layout_file[PATH_MAX];
    snprintf(state_dir, sizeof(state_dir), "%s/Library/Application Support/rearrange-displays", home);
    snprintf(state_file, sizeof(state_file), "%s/current-layout", state_dir);
    snprintf(temporary_file, sizeof(temporary_file), "%s/current-layout.tmp", state_dir);
    char previous = 0;
    FILE *state = fopen(state_file, "r");
    if (state != NULL) { previous = (char)fgetc(state); fclose(state); }
    const char *next = previous == 'A' ? "B" : "A";
    if (!config_path(layout_file, sizeof(layout_file), config_dir, next) || apply_layout(layout_file, next) != 0) return 1;

    if (mkdir(state_dir, 0700) != 0 && errno != EEXIST) { perror("Could not create state directory"); return 1; }
    state = fopen(temporary_file, "w");
    if (state == NULL || fputs(next, state) == EOF || fclose(state) != 0 || rename(temporary_file, state_file) != 0) {
        perror("Could not save layout state");
        return 1;
    }
    printf("Applied display layout %s\n", next);
    return 0;
}
