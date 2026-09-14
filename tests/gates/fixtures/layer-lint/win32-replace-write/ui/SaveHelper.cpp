// A Win32 replace outside the allowed paths (Stage 3 T7).
namespace rawpage::ui {
void offend(const wchar_t* a, const wchar_t* b) { ReplaceFileW(a, b, nullptr, 0, nullptr, nullptr); }
}
