// A Win32 rename by handle outside the allowed paths (Stage 4 guide I-31): SetFileInformationByHandle
// renames and deletes files, and before it joined the list it passed clean anywhere in the tree.
namespace rawpage::ui {
void offend(void* file, void* info) { SetFileInformationByHandle(file, FileRenameInfo, info, 0); }
}
