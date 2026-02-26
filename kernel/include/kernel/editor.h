#ifndef WALU_EDITOR_H
#define WALU_EDITOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <kernel/fs.h>

#define EDITOR_PATH_CAP 128
#define EDITOR_STATUS_CAP 96
#define EDITOR_TEXT_CAP 4096

typedef struct {
    bool active;
    bool dirty;
    bool save_requested;
    bool exit_requested;
    bool discard_armed;
    uint8_t esc_state;
    char path[EDITOR_PATH_CAP];
    char status[EDITOR_STATUS_CAP];
    char text[EDITOR_TEXT_CAP];
    size_t len;
    size_t cursor;
} editor_state_t;

void editor_init(editor_state_t *st);
void editor_set_status(editor_state_t *st, const char *msg);
bool editor_open(editor_state_t *st, const char *path, fs_status_t *out_status);
void editor_handle_input(editor_state_t *st, uint8_t byte);
bool editor_take_save_request(editor_state_t *st);
bool editor_take_exit_request(editor_state_t *st);
fs_status_t editor_save(editor_state_t *st);

#endif
