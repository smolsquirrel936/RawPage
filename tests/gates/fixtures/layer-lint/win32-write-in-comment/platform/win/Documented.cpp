// NEGATIVE CONTROL for the Stage 3 T7 patterns: naming the calls in prose stays legal.
//   ReplaceFileW(target, replacement, backup, 0, nullptr, nullptr)
//   MoveFileExW(from, to, MOVEFILE_WRITE_THROUGH)
//   DeleteFileW(backup)
namespace rawpage::platform::win {
void documented() {}   // not ReplaceFileW( either
}
