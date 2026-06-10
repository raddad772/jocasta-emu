//
// Created by . on 8/4/24.
//

#ifndef JOCASTA_EMUS_KEYMAP_TRANSLATE_H
#define JOCASTA_EMUS_KEYMAP_TRANSLATE_H

#define IMGUI_DISABLE_OBSOLETE_KEYIO
#include "../vendor/myimgui/imgui.h"
#include "helpers/physical_io.h"

enum ImGuiKey jk_to_imgui_gp(enum JKEYS key_id);
enum ImGuiKey jk_to_imgui(enum JKEYS key_id);
enum JKEYS dbcid_to_default(enum JKEYS key_id);

#endif //JOCASTA_EMUS_KEYMAP_TRANSLATE_H
