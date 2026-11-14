// Live on Windows can crash when a plug-in starts a file drag while some other window has the
// focus (JUCE forum topic 65028). Handing Live's main window the focus first avoids it.
// Kept apart from the JUCE code so <windows.h> stays in this one file.
#if defined(_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
 #include <cwchar>

namespace dendrite::ui
{
namespace
{
    bool isLiveTitle (HWND w)
    {
        wchar_t title[256] {};
        GetWindowTextW (w, title, 255);
        return std::wcsstr (title, L"Ableton Live") != nullptr;
    }

    BOOL CALLBACK findLive (HWND w, LPARAM out)
    {
        DWORD pid = 0;
        GetWindowThreadProcessId (w, &pid);
        if (pid == GetCurrentProcessId() && IsWindowVisible (w) && isLiveTitle (w))
        {
            *reinterpret_cast<HWND*> (out) = w;
            return FALSE;
        }
        return TRUE;
    }
} // namespace

void focusHostWindow (void* nativeHandle)
{
    HWND live = nullptr;
    if (nativeHandle != nullptr)
        if (HWND owner = GetAncestor ((HWND) nativeHandle, GA_ROOTOWNER); owner != nullptr && isLiveTitle (owner))
            live = owner;
    if (live == nullptr)
        EnumWindows (findLive, reinterpret_cast<LPARAM> (&live));
    if (live != nullptr)
        SetFocus (live);
}
} // namespace dendrite::ui
#else
namespace dendrite::ui
{
void focusHostWindow (void*) {}
} // namespace dendrite::ui
#endif
