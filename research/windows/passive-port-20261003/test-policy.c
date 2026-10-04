/* SPDX-License-Identifier: Apache-2.0
 * Compile/run default policy in RAM; /D override must fail compilation.
 */
#include "passive-port-20261003/bc250_passive_policy.h"
#if BC250_PASSIVE_GPU_RUNTIME_ENABLED != 0U
#error GPU runtime must remain forbidden
#endif
int main(void)
{
    return BC250_PASSIVE_GPU_RUNTIME_ENABLED != 0U;
}
