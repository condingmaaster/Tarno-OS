// SPDX-License-Identifier: GPL-2.0-or-later
__declspec(dllexport) int marker_b(void) { return 29; }
int __stdcall DllMain(void *h, unsigned why, void *reserved) { (void)h; (void)why; (void)reserved; return 1; }
