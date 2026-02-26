#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <kernel/editor.h>
#include <kernel/fs.h>

static void arrow_left(editor_state_t *ed) {
    editor_handle_input(ed, 0x1B);
    editor_handle_input(ed, '[');
    editor_handle_input(ed, 'D');
}

int main(void) {
    editor_state_t ed;
    fs_status_t st;
    char buf[EDITOR_TEXT_CAP];
    size_t len = 0;

    fs_init();
    editor_init(&ed);

    assert(editor_open(&ed, "/home/note.txt", &st));
    assert(st == FS_OK);
    assert(ed.active);
    assert(ed.len == 0);
    assert(ed.cursor == 0);

    editor_handle_input(&ed, 'h');
    editor_handle_input(&ed, 'i');
    editor_handle_input(&ed, '\n');
    editor_handle_input(&ed, 'x');
    assert(ed.dirty);
    assert(ed.len == 4);
    assert(ed.cursor == 4);

    arrow_left(&ed);
    assert(ed.cursor == 3);
    editor_handle_input(&ed, '!');
    assert(ed.len == 5);
    assert(ed.text[3] == '!');
    assert(ed.text[4] == 'x');

    editor_handle_input(&ed, 0x7F);
    assert(ed.len == 4);
    assert(ed.text[0] == 'h');
    assert(ed.text[1] == 'i');
    assert(ed.text[2] == '\n');
    assert(ed.text[3] == 'x');

    editor_handle_input(&ed, 0x0F);
    assert(editor_take_save_request(&ed));
    assert(editor_save(&ed) == FS_OK);
    assert(!ed.dirty);

    assert(fs_read("/home/note.txt", buf, sizeof(buf), &len) == FS_OK);
    assert(len == 4);
    assert(strncmp(buf, "hi\nx", 4) == 0);

    editor_handle_input(&ed, 0x18);
    assert(editor_take_exit_request(&ed));

    assert(editor_open(&ed, "/home/dirty.txt", &st));
    editor_handle_input(&ed, 'a');
    editor_handle_input(&ed, 0x18);
    assert(!editor_take_exit_request(&ed));
    editor_handle_input(&ed, 0x18);
    assert(editor_take_exit_request(&ed));

    printf("editor host tests passed\n");
    return 0;
}
