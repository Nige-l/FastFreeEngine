#include "editor_app.h"

int main() {
    ffe::editor_app::EditorApp editor;

    if (!editor.init()) {
        return 1;
    }

    editor.run();
    editor.shutdown();
    return 0;
}
