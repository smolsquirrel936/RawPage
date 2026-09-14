// A Win32 delete outside the allowed paths (Stage 3 T7).
namespace rawpage::app {
void offend(const wchar_t* a) { DeleteFileW(a); }
}
