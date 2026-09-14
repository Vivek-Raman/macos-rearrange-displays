#include <ApplicationServices/ApplicationServices.h>
#include <ctype.h>
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

static int layout_name_is_valid(const char *name) {
    if (*name == '\0') return 0;
    for (; *name != '\0'; name++) {
        if (!(isalnum((unsigned char)*name) || *name == '-' || *name == '_')) return 0;
    }
    return 1;
}

static int config_path(char *result, size_t result_size, const char *config_dir, const char *layout_name) {
    const char *name = strncmp(layout_name, "layout-", 7) == 0 ? layout_name + 7 : layout_name;
    if (!layout_name_is_valid(name)) return 0;
    int written = snprintf(result, result_size, "%s/layout-%s.conf", config_dir, name);
    return written >= 0 && (size_t)written < result_size;
}

static int compare_layout_names(const void *left, const void *right) {
    return strcmp(left, right);
}

static int list_layouts(const char *config_dir) {
    DIR *directory = opendir(config_dir);
    if (directory == NULL) {
        perror("Could not read layout directory");
        return 1;
    }

    char names[256][NAME_MAX + 1];
    size_t count = 0;
    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL) {
        const char *filename = entry->d_name;
        size_t length = strlen(filename);
        if (length <= 12 || strncmp(filename, "layout-", 7) != 0 ||
            strcmp(filename + length - 5, ".conf") != 0) {
            continue;
        }
        size_t name_length = length - 12;
        if (name_length > NAME_MAX || count == sizeof(names) / sizeof(names[0])) continue;
        memcpy(names[count], filename + 7, name_length);
        names[count][name_length] = '\0';
        if (layout_name_is_valid(names[count])) count++;
    }
    closedir(directory);

    if (count == 0) {
        printf("No saved layouts found.\n");
        return 0;
    }
    qsort(names, count, sizeof(names[0]), compare_layout_names);
    for (size_t i = 0; i < count; i++) printf("%s\n", names[i]);
    return 0;
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

static int print_usage(void) {
    fprintf(stderr,
            "\nUsage:\n"
            "  rearrange-displays.sh --apply NAME\n"
            "  rearrange-displays.sh --capture NAME [--dry-run]\n"
            "  rearrange-displays.sh --list\n");
    return 1;
}

static void print_command_argument(FILE *stream, const char *argument) {
    int needs_quotes = *argument == '\0' || strpbrk(argument, " \t\n'\\\"") != NULL;
    if (!needs_quotes) {
        fputs(argument, stream);
        return;
    }
    fputc('\'', stream);
    for (; *argument != '\0'; argument++) {
        if (*argument == '\'') fputs("'\\\"'\\\"'", stream);
        else fputc(*argument, stream);
    }
    fputc('\'', stream);
}

static void print_invalid_command_to(FILE *stream, int argc, char *argv[]) {
    int first_argument = argc >= 3 && strcmp(argv[1], "--config-dir") == 0 ? 3 : 1;
    fputs("Invalid command: rearrange-displays.sh", stream);
    for (int i = first_argument; i < argc; i++) {
        fputc(' ', stream);
        print_command_argument(stream, argv[i]);
    }
    fputc('\n', stream);
}

static int print_invalid_command(int argc, char *argv[]) {
    print_invalid_command_to(stdout, argc, argv);
    print_invalid_command_to(stderr, argc, argv);
    return print_usage();
}

int main(int argc, char *argv[]) {
    if (argc < 3 || strcmp(argv[1], "--config-dir") != 0) {
        return print_invalid_command(argc, argv);
    }
    const char *config_dir = argv[2];
    const char *command = argc > 3 ? argv[3] : "";
    if (strcmp(command, "--list") == 0) {
        if (argc != 4) {
            return print_invalid_command(argc, argv);
        }
        return list_layouts(config_dir);
    }
    if (strcmp(command, "--capture") == 0) {
        if (argc < 5 || argc > 6) {
            return print_invalid_command(argc, argv);
        }
        const char *layout_name = argv[4];
        int dry_run = argc > 5 && strcmp(argv[5], "--dry-run") == 0;
        if (!config_path((char[PATH_MAX]){0}, PATH_MAX, config_dir, layout_name)) {
            return print_invalid_command(argc, argv);
        }
        if (argc > 5 && !dry_run) {
            return print_invalid_command(argc, argv);
        }
        return capture_layout(config_dir, layout_name, dry_run);
    }
    if (strcmp(command, "--apply") != 0 || argc != 5) {
        return print_invalid_command(argc, argv);
    }

    const char *layout_name = argv[4];
    char layout_file[PATH_MAX];
    if (!config_path(layout_file, sizeof(layout_file), config_dir, layout_name)) {
        return print_invalid_command(argc, argv);
    }
    if (apply_layout(layout_file, layout_name) != 0) return 1;
    printf("Applied display layout %s\n", layout_name);
    return 0;
}
