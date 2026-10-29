/* Minimal desktop stub of wut's nn::ac (auto-connect) surface.
 * Real build uses devkitPro wut. */
#ifndef STUB_NN_AC_H
#define STUB_NN_AC_H
namespace nn { namespace ac {
int Initialize(void);
int Connect(void);
void Finalize(void);
} }
#endif
