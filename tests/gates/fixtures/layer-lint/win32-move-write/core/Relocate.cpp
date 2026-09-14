// A Win32 move outside the allowed paths (Stage 3 T7).
namespace rawpage::core {
void offend(const wchar_t* a, const wchar_t* b) { MoveFileExW(a, b, 0); }
}
